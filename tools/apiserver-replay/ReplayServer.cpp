#include "ReplayServer.h"

#include <QDateTime>
#include <QFile>
#include <QHttpHeaders>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QHttpServerWebSocketUpgradeResponse>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTimer>
#include <QWebSocket>

ReplayServer::ReplayServer(QObject* parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
{
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, [this] {
        stepTo(m_pos + 1);
        scheduleNext();
    });
}

ReplayServer::~ReplayServer()
{
    for (Client* c : std::as_const(m_clients))
        delete c;
}

bool ReplayServer::load(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    m_messages.clear();
    bool firstConnection = true;
    int conn = -1;
    while (!f.atEnd()) {
        const QJsonObject line = QJsonDocument::fromJson(f.readLine()).object();
        if (line.isEmpty() || !line.contains(QStringLiteral("msg")))
            continue;
        const int c = line.value(QStringLiteral("conn")).toInt();
        if (conn >= 0 && c != conn)
            firstConnection = false; // a reconnection: its snapshot repeats what is known
        conn = c;
        const QJsonObject msg = line.value(QStringLiteral("msg")).toObject();
        Message m;
        m.rx = line.value(QStringLiteral("rx")).toInteger();
        m.type = msg.value(QStringLiteral("type")).toString();
        m.data = msg.value(QStringLiteral("data")).toObject();
        if (m.type == QLatin1String("hello")) {
            if (firstConnection)
                m_hello = m.data;
            continue;
        }
        m_messages << m;
    }
    m_pos = 0;
    // the first status / live / resources are the state at the start of the recording
    while (m_pos < m_messages.size() && m_messages.at(m_pos).type != QLatin1String("competitor")
           && m_messages.at(m_pos).type != QLatin1String("changed"))
        apply(m_messages.at(m_pos++));
    return !m_messages.isEmpty();
}

bool ReplayServer::start(quint16 port, QString* error)
{
    m_http = std::make_unique<QHttpServer>();
    m_http->setMissingHandler(this, [this](const QHttpServerRequest& req, QHttpServerResponder& resp) { onRequest(req, resp); });
    m_http->addWebSocketUpgradeVerifier(this, [](const QHttpServerRequest& req) {
        return req.url().path() == QLatin1String("/ws") ? QHttpServerWebSocketUpgradeResponse::accept()
                                                        : QHttpServerWebSocketUpgradeResponse::passToNext();
    });
    connect(m_http.get(), &QAbstractHttpServer::newWebSocketConnection, this, &ReplayServer::onNewWebSocket);
    m_tcp = new QTcpServer(m_http.get());
    if (!m_tcp->listen(QHostAddress::Any, port) || !m_http->bind(m_tcp)) {
        if (error)
            *error = m_tcp->errorString();
        m_http.reset();
        m_tcp = nullptr;
        return false;
    }
    return true;
}

quint16 ReplayServer::port() const
{
    return m_tcp ? m_tcp->serverPort() : 0;
}

void ReplayServer::apply(const Message& m)
{
    if (m.type == QLatin1String("status"))
        m_status = m.data;
    else if (m.type == QLatin1String("live"))
        m_live = m.data;
    else if (m.type == QLatin1String("resource"))
        m_resources.insert(m.data.value(QStringLiteral("name")).toString(), m.data);
}

void ReplayServer::stepTo(int index)
{
    index = qMin(index, int(m_messages.size()));
    while (m_pos < index) {
        const Message& m = m_messages.at(m_pos++);
        apply(m);
        broadcast(m);
    }
    Q_EMIT positionChanged(m_pos);
    if (m_pos >= m_messages.size())
        Q_EMIT finished();
}

void ReplayServer::play(double speed)
{
    m_speed = speed > 0 ? speed : 1.0;
    scheduleNext();
}

void ReplayServer::pause()
{
    m_timer->stop();
}

void ReplayServer::scheduleNext()
{
    if (m_pos >= m_messages.size())
        return;
    const qint64 prev = m_pos > 0 ? m_messages.at(m_pos - 1).rx : m_messages.at(m_pos).rx;
    const qint64 gap = qMax<qint64>(0, m_messages.at(m_pos).rx - prev);
    m_timer->start(int(qMin<qint64>(60000, qint64(gap / m_speed))));
}

void ReplayServer::send(Client& c, const QString& type, const QJsonObject& data)
{
    QJsonObject o;
    o.insert(QStringLiteral("type"), type);
    o.insert(QStringLiteral("seq"), ++c.seq);
    o.insert(QStringLiteral("ts"), QDateTime::currentMSecsSinceEpoch());
    o.insert(QStringLiteral("data"), data);
    c.socket->sendTextMessage(QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
}

void ReplayServer::sendResource(Client& c, const QString& name)
{
    if (m_resources.contains(name)) {
        send(c, QStringLiteral("resource"), m_resources.value(name));
        return;
    }
    QJsonObject d; // not in the recording
    d.insert(QStringLiteral("name"), name);
    d.insert(QStringLiteral("rev"), 0);
    d.insert(QStringLiteral("status"), 404);
    d.insert(QStringLiteral("content"), QJsonObject());
    send(c, QStringLiteral("resource"), d);
}

void ReplayServer::broadcast(const Message& m)
{
    const auto clients = m_clients.values();
    for (Client* c : clients) {
        if (m.type == QLatin1String("resource") && !c->subscriptions.contains(m.data.value(QStringLiteral("name")).toString()))
            continue;
        send(*c, m.type, m.data);
    }
}

void ReplayServer::onNewWebSocket()
{
    while (m_http && m_http->hasPendingWebSocketConnections()) {
        QWebSocket* s = m_http->nextPendingWebSocketConnection().release();
        if (!s)
            break;
        s->setParent(this);
        auto* c = new Client;
        c->socket = s;
        m_clients.insert(s, c);
        connect(s, &QWebSocket::textMessageReceived, this, [this, s](const QString& text) {
            Client* cl = m_clients.value(s);
            if (!cl)
                return;
            const QJsonObject msg = QJsonDocument::fromJson(text.toUtf8()).object();
            const bool sub = msg.value(QStringLiteral("type")).toString() == QLatin1String("subscribe");
            for (const auto& v : msg.value(QStringLiteral("data")).toObject().value(QStringLiteral("resources")).toArray()) {
                if (sub) {
                    cl->subscriptions.insert(v.toString());
                    sendResource(*cl, v.toString());
                } else {
                    cl->subscriptions.remove(v.toString());
                }
            }
        });
        connect(s, &QWebSocket::disconnected, this, [this, s] {
            delete m_clients.take(s);
            s->deleteLater();
        });
        send(*c, QStringLiteral("hello"), m_hello);
        send(*c, QStringLiteral("status"), m_status);
        send(*c, QStringLiteral("live"), m_live);
    }
}

void ReplayServer::onRequest(const QHttpServerRequest& req, QHttpServerResponder& resp)
{
    QHttpHeaders headers;
    headers.append(QHttpHeaders::WellKnownHeader::AccessControlAllowOrigin, "*");
    headers.append(QHttpHeaders::WellKnownHeader::ContentType, "application/json; charset=utf-8");
    QString name = req.url().path().mid(QStringLiteral("/api/v1/").size());
    if (name.startsWith(QLatin1String("runs/")))
        name = QStringLiteral("run/") + name.mid(5);
    if (!req.url().query().isEmpty())
        name += QLatin1Char('?') + req.url().query();
    QJsonObject body;
    int status = 200;
    if (name == QLatin1String("status")) {
        body = m_status;
    } else if (name == QLatin1String("live")) {
        body = m_live;
    } else if (m_resources.contains(name)) {
        body = m_resources.value(name).value(QStringLiteral("content")).toObject();
        status = m_resources.value(name).value(QStringLiteral("status")).toInt(200);
    } else {
        status = 404;
        body.insert(QStringLiteral("error"), QStringLiteral("notFound"));
        body.insert(QStringLiteral("message"), QStringLiteral("%1 is not in the recording").arg(name));
    }
    resp.write(QJsonDocument(body).toJson(QJsonDocument::Compact), headers, QHttpServerResponder::StatusCode(status));
}
