#include "FisLiveSender.h"
#include "FisLiveXml.h"

#include <QDate>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QTime>
#include <QTimer>

namespace {
constexpr int kRetryMs = 5000;
constexpr int kKeepaliveMs = 5 * 60 * 1000; // 5-10 min of inactivity (specification 15)
} // namespace

FisLiveSender::FisLiveSender(QObject* parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_retry(new QTimer(this))
    , m_keepalive(new QTimer(this))
{
    m_retry->setSingleShot(true);
    m_retry->setInterval(kRetryMs);
    connect(m_retry, &QTimer::timeout, this, &FisLiveSender::connectNow);
    m_keepalive->setSingleShot(true);
    m_keepalive->setInterval(kKeepaliveMs);
    connect(m_keepalive, &QTimer::timeout, this, [this] {
        if (isConnected() && m_queue.isEmpty())
            send(FisLiveXml::keepalive());
    });
    connect(m_socket, &QTcpSocket::connected, this, [this] {
        Q_EMIT log(tr("connected to %1:%2").arg(m_settings.host).arg(m_settings.port));
        for (Item& i : m_queue)
            i.written = false; // not acknowledged: again, in order
        flush();
        Q_EMIT stateChanged();
    });
    connect(m_socket, &QTcpSocket::disconnected, this, [this] {
        Q_EMIT log(tr("disconnected (%1 message(s) not acknowledged)").arg(m_queue.size()));
        Q_EMIT stateChanged();
        if (m_running)
            m_retry->start();
    });
    connect(m_socket, &QTcpSocket::errorOccurred, this, [this] {
        Q_EMIT log(tr("connection error: %1").arg(m_socket->errorString()));
        Q_EMIT stateChanged();
        if (m_running && m_socket->state() == QAbstractSocket::UnconnectedState)
            m_retry->start();
    });
    connect(m_socket, &QTcpSocket::readyRead, this, &FisLiveSender::onReadyRead);
}

FisLiveSender::~FisLiveSender()
{
    m_socket->disconnect(this);
}

void FisLiveSender::setSettings(const Settings& s)
{
    const bool target = s.host != m_settings.host || s.port != m_settings.port;
    const bool race = s.codex != m_settings.codex;
    m_settings = s;
    if (race) {
        m_queue.clear();
        m_sequence = 0;
        loadQueue();
    }
    if (target && m_running) {
        m_socket->abort();
        connectNow();
    }
}

void FisLiveSender::setFolder(const QString& folder)
{
    m_folder = folder;
    m_log.close();
    loadQueue();
}

void FisLiveSender::setKeepaliveMs(int ms)
{
    m_keepalive->setInterval(ms);
}

QString FisLiveSender::queuePath() const
{
    if (m_folder.isEmpty() || m_settings.codex.isEmpty())
        return QString();
    return QDir(m_folder).filePath(QStringLiteral("fislive-%1-queue.json").arg(m_settings.codex));
}

void FisLiveSender::start()
{
    m_running = true;
    connectNow();
    Q_EMIT stateChanged();
}

void FisLiveSender::stop()
{
    m_running = false;
    m_retry->stop();
    m_keepalive->stop();
    m_socket->disconnectFromHost();
    Q_EMIT stateChanged();
}

bool FisLiveSender::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

void FisLiveSender::connectNow()
{
    if (!m_running || m_socket->state() != QAbstractSocket::UnconnectedState)
        return;
    m_socket->connectToHost(m_settings.host, m_settings.port);
}

void FisLiveSender::send(const QByteArray& body)
{
    Item i;
    i.sequence = ++m_sequence;
    i.body = body;
    m_queue << i;
    saveQueue();
    flush();
    Q_EMIT stateChanged();
}

void FisLiveSender::flush()
{
    if (!isConnected())
        return;
    for (Item& i : m_queue) {
        if (i.written)
            continue;
        const QByteArray message =
            FisLiveXml::envelope(m_settings.codex, m_settings.password, i.sequence, FisLiveXml::isoUtcNow(), i.body);
        m_socket->write(message);
        i.written = true;
        ++m_sentCount;
        writeLog(">>", message);
    }
    m_keepalive->start();
}

void FisLiveSender::onReadyRead()
{
    m_incoming += m_socket->readAll();
    // complete replies only (<livetiming sequence="..."> ... </livetiming> or />)
    const int end = int(qMax(m_incoming.lastIndexOf("</livetiming>"), m_incoming.lastIndexOf("/>")));
    if (end < 0)
        return;
    const QByteArray done = m_incoming.left(end + (m_incoming.mid(end).startsWith("</livetiming>") ? 13 : 2));
    m_incoming.remove(0, done.size());
    writeLog("<<", done);
    for (int seq : FisLiveXml::replySequences(done)) {
        for (int k = 0; k < m_queue.size(); ++k)
            if (m_queue.at(k).sequence == seq) {
                m_queue.removeAt(k);
                ++m_ackCount;
                break;
            }
    }
    if (done.contains("<error"))
        Q_EMIT log(tr("FIS replied with an error: %1").arg(QString::fromUtf8(done).simplified()));
    saveQueue();
    Q_EMIT stateChanged();
}

void FisLiveSender::saveQueue()
{
    const QString path = queuePath();
    if (path.isEmpty())
        return;
    QJsonArray a;
    for (const Item& i : m_queue) {
        QJsonObject o;
        o.insert(QStringLiteral("sequence"), i.sequence);
        o.insert(QStringLiteral("body"), QString::fromUtf8(i.body));
        a << o;
    }
    QJsonObject root;
    root.insert(QStringLiteral("lastSequence"), m_sequence);
    root.insert(QStringLiteral("queue"), a);
    QDir().mkpath(m_folder);
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void FisLiveSender::loadQueue()
{
    const QString path = queuePath();
    QFile f(path);
    if (path.isEmpty() || !f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    m_sequence = qMax(m_sequence, root.value(QStringLiteral("lastSequence")).toInt());
    m_queue.clear();
    for (const auto& v : root.value(QStringLiteral("queue")).toArray()) {
        Item i;
        i.sequence = v.toObject().value(QStringLiteral("sequence")).toInt();
        i.body = v.toObject().value(QStringLiteral("body")).toString().toUtf8();
        m_queue << i;
    }
    if (!m_queue.isEmpty())
        Q_EMIT log(tr("%1 message(s) of an earlier session not acknowledged - they are sent again").arg(m_queue.size()));
}

void FisLiveSender::writeLog(const char* direction, const QByteArray& data)
{
    if (m_folder.isEmpty())
        return;
    const QString path = QDir(m_folder).filePath(
        QStringLiteral("fislive-%1-%2.xml").arg(m_settings.codex.isEmpty() ? QStringLiteral("nocodex") : m_settings.codex,
                                                QDate::currentDate().toString(Qt::ISODate)));
    if (m_log.fileName() != path) {
        m_log.close();
        QDir().mkpath(m_folder);
        m_log.setFileName(path);
        m_log.open(QIODevice::WriteOnly | QIODevice::Append);
    }
    if (!m_log.isOpen())
        return;
    m_log.write("<!-- " + QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")).toUtf8() + " " + direction + " -->\n");
    m_log.write(FisLiveXml::masked(data) + "\n");
    m_log.flush();
}
