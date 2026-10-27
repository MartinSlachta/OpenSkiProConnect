#include "ScreenServer.h"
#include "ScreenData.h"
#include "core/RaceModel.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHttpHeaders>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QHttpServerWebSocketUpgradeResponse>
#include <QJsonDocument>
#include <QNetworkInterface>
#include <QTcpServer>
#include <QTimer>
#include <QUrlQuery>
#include <QWebSocket>

namespace {

// Fingerprint of the code of the pages: a page loaded from an older Connect reloads
// itself when it connects to a newer one (no F5 on the TVs after an update).
QString webVersion()
{
    static const QString v = [] {
        QCryptographicHash h(QCryptographicHash::Md5);
        h.addData(QByteArrayLiteral(OSC_VERSION));
        QDirIterator it(QStringLiteral(":/screen/web"), QDir::Files, QDirIterator::Subdirectories);
        QStringList files;
        while (it.hasNext())
            files << it.next();
        files.sort();
        for (const QString& name : files) {
            QFile f(name);
            if (f.open(QIODevice::ReadOnly))
                h.addData(f.readAll());
        }
        return QString::fromLatin1(h.result().toHex().left(12));
    }();
    return v;
}

QByteArray mimeOf(const QString& path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == QLatin1String("html"))
        return "text/html; charset=utf-8";
    if (ext == QLatin1String("js"))
        return "text/javascript; charset=utf-8";
    if (ext == QLatin1String("css"))
        return "text/css; charset=utf-8";
    if (ext == QLatin1String("svg"))
        return "image/svg+xml";
    if (ext == QLatin1String("png"))
        return "image/png";
    if (ext == QLatin1String("jpg") || ext == QLatin1String("jpeg"))
        return "image/jpeg";
    if (ext == QLatin1String("gif"))
        return "image/gif";
    if (ext == QLatin1String("bmp"))
        return "image/bmp";
    if (ext == QLatin1String("webp"))
        return "image/webp";
    return "application/octet-stream";
}

// A plain file name (no folders, no "..") - nothing outside the folder is served.
bool safeName(const QString& name)
{
    return !name.isEmpty() && !name.contains(QLatin1Char('/')) && !name.contains(QLatin1Char('\\')) && !name.contains(QStringLiteral(".."));
}

QString htmlEscape(const QString& s)
{
    return s.toHtmlEscaped();
}

} // namespace

ScreenServer::ScreenServer(ScreenData* data, QObject* parent)
    : QObject(parent)
    , m_data(data)
    , m_clock(new QTimer(this))
{
    connect(m_data, &ScreenData::stateChanged, this, &ScreenServer::broadcastState);
    connect(m_data->model(), &RaceModel::eventChanged, this, &ScreenServer::reloadConfigs); // the public fields
    // the browsers keep the clock running; one tick a second keeps it exact
    m_clock->setInterval(1000);
    connect(m_clock, &QTimer::timeout, this, &ScreenServer::broadcastClock);
}

ScreenServer::~ScreenServer()
{
    stop();
}

bool ScreenServer::start(quint16 port, QString* error)
{
    stop();
    m_http = std::make_unique<QHttpServer>();
    m_http->setMissingHandler(this, [this](const QHttpServerRequest& req, QHttpServerResponder& resp) { onRequest(req, resp); });
    m_http->addWebSocketUpgradeVerifier(this, [](const QHttpServerRequest& req) {
        return req.url().path() == QLatin1String("/ws") ? QHttpServerWebSocketUpgradeResponse::accept()
                                                        : QHttpServerWebSocketUpgradeResponse::passToNext();
    });
    connect(m_http.get(), &QAbstractHttpServer::newWebSocketConnection, this, &ScreenServer::onNewWebSocket);
    m_tcp = new QTcpServer(m_http.get());
    if (!m_tcp->listen(QHostAddress::Any, port) || !m_http->bind(m_tcp)) {
        if (error)
            *error = m_tcp->errorString();
        m_http.reset();
        m_tcp = nullptr;
        return false;
    }
    m_clock->start();
    return true;
}

void ScreenServer::stop()
{
    m_clock->stop();
    const auto clients = m_clients.keys();
    for (QWebSocket* s : clients) {
        s->disconnect(this);
        s->close();
        s->deleteLater();
    }
    qDeleteAll(m_clients);
    m_clients.clear();
    m_http.reset();
    m_tcp = nullptr;
    Q_EMIT clientsChanged();
}

bool ScreenServer::isListening() const
{
    return m_tcp && m_tcp->isListening();
}

quint16 ScreenServer::port() const
{
    return m_tcp ? m_tcp->serverPort() : 0;
}

QStringList ScreenServer::urls() const
{
    QStringList l;
    if (!isListening())
        return l;
    for (const QHostAddress& a : QNetworkInterface::allAddresses())
        if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback() && !a.isLinkLocal())
            l << QStringLiteral("http://%1:%2/").arg(a.toString()).arg(port());
    l << QStringLiteral("http://localhost:%1/").arg(port());
    return l;
}

void ScreenServer::setFolders(const QString& configs, const QString& flags)
{
    m_configFolder = configs;
    m_flagsFolder = flags;
    m_cache.clear();
    reloadConfigs();
}

void ScreenServer::setConfigFile(const QString& file)
{
    m_configFile = file;
    reloadConfigs();
}

void ScreenServer::setMessage(const QString& message)
{
    m_message = message;
    for (Client* c : std::as_const(m_clients))
        send(*c, QStringLiteral("control"), QJsonObject{ { QStringLiteral("message"), message } });
}

void ScreenServer::setPreview(const QString& id, const ScreenConfig& config)
{
    m_previews.insert(QStringLiteral("preview-") + id, config);
    reloadConfigs();
}

void ScreenServer::reloadConfigs()
{
    for (Client* c : std::as_const(m_clients))
        sendConfig(*c, false);
}

int ScreenServer::clientCount(const QString& page) const
{
    if (page.isEmpty())
        return int(m_clients.size());
    int n = 0;
    for (const Client* c : m_clients)
        n += c->page == page && c->preview.isEmpty() ? 1 : 0;
    return n;
}

ScreenConfig ScreenServer::configOf(const QString& preview) const
{
    if (m_previews.contains(preview))
        return m_previews.value(preview);
    if (m_configFile.isEmpty())
        return ScreenConfig();
    const QString path = QDir(m_configFolder).filePath(m_configFile);
    const QFileInfo fi(path);
    Cached& c = m_cache[path];
    if (!fi.exists())
        return ScreenConfig();
    if (c.modified != fi.lastModified()) {
        ScreenConfig cfg;
        if (cfg.load(path, nullptr))
            c.config = cfg;
        c.modified = fi.lastModified();
    }
    return c.config;
}

void ScreenServer::send(Client& c, const QString& type, const QJsonObject& data)
{
    const QJsonObject o{ { QStringLiteral("type"), type }, { QStringLiteral("data"), data } };
    c.socket->sendTextMessage(QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
}

void ScreenServer::sendConfig(Client& c, bool force)
{
    // only the public fields of the event (OpenSkiPro2: Competitor Fields > Public)
    const QJsonObject cfg = configOf(c.preview).forPublicFields(m_data->model()->publicFields()).toJson();
    if (!force && cfg == c.configSent)
        return;
    c.configSent = cfg;
    send(c, QStringLiteral("config"), cfg);
}

void ScreenServer::broadcastState()
{
    const QJsonObject state = m_data->state();
    if (m_clients.isEmpty())
        return;
    const QString text = QString::fromUtf8(QJsonDocument(QJsonObject{ { QStringLiteral("type"), QStringLiteral("state") },
                                                                      { QStringLiteral("data"), state } })
                                               .toJson(QJsonDocument::Compact));
    for (Client* c : std::as_const(m_clients))
        c->socket->sendTextMessage(text);
}

void ScreenServer::broadcastClock()
{
    if (m_clients.isEmpty())
        return;
    const QJsonObject clock = m_data->clock();
    for (Client* c : std::as_const(m_clients))
        send(*c, QStringLiteral("clock"), clock);
}

void ScreenServer::onNewWebSocket()
{
    while (m_http && m_http->hasPendingWebSocketConnections()) {
        QWebSocket* s = m_http->nextPendingWebSocketConnection().release();
        if (!s)
            break;
        s->setParent(this);
        auto* c = new Client;
        c->socket = s;
        const QUrlQuery q(s->requestUrl());
        c->page = q.queryItemValue(QStringLiteral("page")) == QLatin1String("speaker") ? QStringLiteral("speaker") : QStringLiteral("board");
        const QString board = q.queryItemValue(QStringLiteral("board"), QUrl::FullyDecoded);
        if (m_previews.contains(board))
            c->preview = board;
        m_clients.insert(s, c);
        connect(s, &QWebSocket::disconnected, this, [this, s] {
            delete m_clients.take(s);
            s->deleteLater();
            Q_EMIT clientsChanged();
        });
        send(*c, QStringLiteral("hello"), QJsonObject{ { QStringLiteral("version"), QStringLiteral(OSC_VERSION) },
                                                      { QStringLiteral("web"), webVersion() } });
        sendConfig(*c, true);
        send(*c, QStringLiteral("clock"), m_data->clock());
        send(*c, QStringLiteral("state"), m_data->state());
        send(*c, QStringLiteral("control"), QJsonObject{ { QStringLiteral("message"), m_message } });
        Q_EMIT clientsChanged();
    }
}

QByteArray ScreenServer::indexPage() const
{
    const ScreenConfig cfg = configOf();
    return QStringLiteral("<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width\">"
                          "<title>OpenSkiPro Connect - Screen</title><style>body{font-family:Arial,sans-serif;margin:24px;background:#f4f7fb;color:#0b1f3a}"
                          "h1{font-size:22px}li{margin:10px 0;font-size:20px}span{color:#667;font-size:15px;margin-left:10px}a{color:#1f4e8c}</style>"
                          "</head><body><h1>OpenSkiPro Connect - Screen</h1><ul>"
                          "<li><a href=\"/board\">/board</a> <span>TV board (every TV)</span></li>"
                          "<li><a href=\"/speaker\">/speaker</a> <span>page of the speaker</span></li>"
                          "</ul><p><span>Configuration: %1</span></p></body></html>")
        .arg(htmlEscape(cfg.name.isEmpty() ? m_configFile : cfg.name))
        .toUtf8();
}

void ScreenServer::onRequest(const QHttpServerRequest& req, QHttpServerResponder& resp)
{
    const QString path = req.url().path();
    auto reply = [&resp](const QByteArray& body, const QByteArray& mime, QHttpServerResponder::StatusCode status = QHttpServerResponder::StatusCode::Ok) {
        QHttpHeaders h;
        h.append(QHttpHeaders::WellKnownHeader::ContentType, mime);
        h.append(QHttpHeaders::WellKnownHeader::CacheControl, "no-cache");
        resp.write(body, h, status);
    };
    auto notFound = [&reply] { reply("not found", "text/plain", QHttpServerResponder::StatusCode::NotFound); };
    auto resource = [&](const QString& file) {
        QFile f(QStringLiteral(":/screen/web/") + file);
        if (!safeName(file) || !f.open(QIODevice::ReadOnly))
            return notFound();
        reply(f.readAll(), mimeOf(file));
    };

    if (path == QLatin1String("/")) {
        reply(indexPage(), "text/html; charset=utf-8");
    } else if (path == QLatin1String("/board") || path.startsWith(QLatin1String("/board/"))) {
        // every TV the same board; old addresses of 0.3 (/board/finish, /board/speaker) still work
        const bool speaker = path.mid(7).compare(QLatin1String("speaker"), Qt::CaseInsensitive) == 0;
        resource(speaker ? QStringLiteral("speaker.html") : QStringLiteral("board.html"));
    } else if (path == QLatin1String("/speaker")) {
        resource(QStringLiteral("speaker.html"));
    } else if (path.startsWith(QLatin1String("/web/"))) {
        resource(path.mid(5));
    } else if (path.startsWith(QLatin1String("/images/"))) {
        const QString name = QUrl::fromPercentEncoding(path.mid(8).toUtf8());
        QFile f(QDir(m_configFolder).filePath(QStringLiteral("images/") + name));
        if (!safeName(name) || !f.open(QIODevice::ReadOnly))
            return notFound();
        reply(f.readAll(), mimeOf(name));
    } else if (path.startsWith(QLatin1String("/flags/"))) {
        const QString code = path.mid(7).toUpper();
        if (!safeName(code) || m_flagsFolder.isEmpty())
            return notFound();
        for (const char* ext : { "svg", "png", "gif", "jpg", "bmp", "webp" }) {
            for (const QString& name : { code + QLatin1Char('.') + QLatin1String(ext), code.toLower() + QLatin1Char('.') + QLatin1String(ext) }) {
                QFile f(QDir(m_flagsFolder).filePath(name));
                if (f.open(QIODevice::ReadOnly)) {
                    QHttpHeaders h;
                    h.append(QHttpHeaders::WellKnownHeader::ContentType, mimeOf(name));
                    h.append(QHttpHeaders::WellKnownHeader::CacheControl, "max-age=3600");
                    resp.write(f.readAll(), h);
                    return;
                }
            }
        }
        notFound();
    } else {
        notFound();
    }
}
