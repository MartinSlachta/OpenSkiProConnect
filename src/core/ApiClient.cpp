#include "ApiClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QWebSocket>

ApiClient::ApiClient(QObject* parent)
    : QObject(parent)
    , m_ws(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this))
    , m_nam(new QNetworkAccessManager(this))
    , m_retry(new QTimer(this))
{
    m_retry->setSingleShot(true);
    m_retry->setInterval(2000);
    connect(m_retry, &QTimer::timeout, this, &ApiClient::open);
    connect(m_ws, &QWebSocket::connected, this, [this] {
        m_seq = 0;
        m_connected = true;
        Q_EMIT log(tr("connected to %1:%2").arg(m_host).arg(m_port));
        Q_EMIT connectedChanged(true);
        sendSubscribe(QStringList(m_subscriptions.cbegin(), m_subscriptions.cend()), true);
    });
    connect(m_ws, &QWebSocket::disconnected, this, [this] {
        if (m_connected) {
            m_connected = false;
            Q_EMIT log(tr("disconnected"));
            Q_EMIT connectedChanged(false);
        }
        if (m_started)
            m_retry->start();
    });
    connect(m_ws, &QWebSocket::textMessageReceived, this, &ApiClient::onText);
}

void ApiClient::setServer(const QString& host, quint16 port)
{
    if (host == m_host && port == m_port)
        return;
    m_host = host;
    m_port = port;
    if (m_started) {
        m_ws->abort();
        open();
    }
}

void ApiClient::start()
{
    m_started = true;
    open();
}

void ApiClient::stop()
{
    m_started = false;
    m_retry->stop();
    m_ws->close();
}

void ApiClient::open()
{
    if (!m_started || m_ws->state() != QAbstractSocket::UnconnectedState)
        return;
    m_ws->open(QUrl(QStringLiteral("ws://%1:%2/ws").arg(m_host).arg(m_port)));
}

void ApiClient::onText(const QString& text)
{
    const QJsonObject msg = QJsonDocument::fromJson(text.toUtf8()).object();
    const qint64 seq = msg.value(QStringLiteral("seq")).toInteger();
    if (seq != m_seq + 1) {
        // a lost message: the snapshot of a new connection restores everything
        Q_EMIT log(tr("message %1 after %2 - reconnecting").arg(seq).arg(m_seq));
        m_ws->abort();
        return;
    }
    m_seq = seq;
    Q_EMIT message(msg.value(QStringLiteral("type")).toString(), msg.value(QStringLiteral("data")).toObject());
}

void ApiClient::subscribe(const QStringList& names)
{
    QStringList add;
    for (const QString& n : names)
        if (!m_subscriptions.contains(n)) {
            m_subscriptions.insert(n);
            add << n;
        }
    if (m_connected)
        sendSubscribe(add, true);
}

void ApiClient::unsubscribe(const QStringList& names)
{
    QStringList rem;
    for (const QString& n : names)
        if (m_subscriptions.remove(n))
            rem << n;
    if (m_connected)
        sendSubscribe(rem, false);
}

void ApiClient::sendSubscribe(const QStringList& names, bool subscribe)
{
    if (names.isEmpty())
        return;
    QJsonObject data;
    data.insert(QStringLiteral("resources"), QJsonArray::fromStringList(names));
    QJsonObject msg;
    msg.insert(QStringLiteral("type"), subscribe ? QStringLiteral("subscribe") : QStringLiteral("unsubscribe"));
    msg.insert(QStringLiteral("data"), data);
    m_ws->sendTextMessage(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
}

void ApiClient::get(const QString& name, std::function<void(int, const QJsonObject&)> callback)
{
    QString path = name;
    if (path.startsWith(QLatin1String("run/")))
        path = QStringLiteral("runs/") + path.mid(4);
    QNetworkRequest req(QUrl(QStringLiteral("http://%1:%2/api/v1/%3").arg(m_host).arg(m_port).arg(path)));
    req.setTransferTimeout(10000);
    QNetworkReply* r = m_nam->get(req);
    connect(r, &QNetworkReply::finished, this, [r, callback] {
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        callback(status, QJsonDocument::fromJson(r->readAll()).object());
        r->deleteLater();
    });
}
