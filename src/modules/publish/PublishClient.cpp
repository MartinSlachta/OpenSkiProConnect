#include "PublishClient.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUuid>

PublishClient::PublishClient(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_debounce(new QTimer(this))
    , m_keepalive(new QTimer(this))
    , m_retry(new QTimer(this))
    , m_instance(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    // changes of 0.5 s go in one request (docs/publish-protocol.md 2.1)
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(500);
    connect(m_debounce, &QTimer::timeout, this, [this] { send(false); });
    // an empty request every 15 s keeps the race "online"
    m_keepalive->setInterval(15000);
    connect(m_keepalive, &QTimer::timeout, this, [this] {
        if (m_checked && !m_reply && m_state != State::Refused && m_state != State::Retrying)
            send(true);
    });
    m_retry->setSingleShot(true);
    connect(m_retry, &QTimer::timeout, this, [this] {
        if (!m_running)
            return;
        if (m_checked)
            send(true);
        else
            check();
    });
}

void PublishClient::setTimings(int debounceMs, int keepaliveMs, int maxBackoffMs)
{
    m_debounce->setInterval(debounceMs);
    m_keepalive->setInterval(keepaliveMs);
    m_maxBackoffMs = maxBackoffMs;
}

QString PublishClient::hashOf(const QJsonObject& data)
{
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(data).toJson(QJsonDocument::Compact), QCryptographicHash::Sha1).toHex());
}

void PublishClient::setTarget(const QUrl& server, const QString& token, const QString& eventDate)
{
    if (server == m_server && token == m_token && eventDate == m_eventDate)
        return;
    m_server = server;
    m_token = token;
    m_eventDate = eventDate;
    // another race (or server): nothing of the previous one may leak into it
    reset();
    if (m_running)
        check();
}

void PublishClient::reset()
{
    if (m_reply) {
        QNetworkReply* r = m_reply;
        m_reply = nullptr;
        r->abort();
        r->deleteLater();
    }
    m_debounce->stop();
    m_retry->stop();
    m_have.clear();
    m_inFlight.clear();
    m_full = true;
    m_checked = false;
    m_tokenState.clear();
    m_race = Race();
    m_backoffMs = 0;
    setState(State::Idle);
}

void PublishClient::setSections(const QMap<QString, QJsonObject>& sections)
{
    QHash<QString, QString> hashes;
    for (auto it = sections.begin(); it != sections.end(); ++it)
        hashes.insert(it.key(), hashOf(it.value()));
    if (hashes == m_hashes)
        return;
    m_sections = sections;
    m_hashes = hashes;
    schedule();
}

void PublishClient::start()
{
    if (m_running)
        return;
    m_running = true;
    m_keepalive->start();
    check();
}

void PublishClient::stop()
{
    m_running = false;
    m_keepalive->stop();
    reset();
}

void PublishClient::sendAgain()
{
    reset();
    if (m_running)
        check();
}

void PublishClient::setState(State s, const QString& message)
{
    if (s == m_state && message == m_message)
        return;
    m_state = s;
    m_message = message;
    Q_EMIT stateChanged();
}

void PublishClient::schedule()
{
    if (!m_running || !m_checked || m_reply || m_state == State::Refused || m_state == State::Retrying)
        return; // after the answer / the retry
    if (!m_debounce->isActive())
        m_debounce->start();
}

QNetworkReply* PublishClient::post(const QString& path, const QJsonObject& body)
{
    QUrl url = m_server;
    QString base = url.path();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    url.setPath(base + path);
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OpenSkiProConnect/%1").arg(QStringLiteral(OSC_VERSION)));
    req.setTransferTimeout(20000);
    const QByteArray json = QJsonDocument(body).toJson(QJsonDocument::Compact);
    // zlib stream: qCompress without its 4 bytes of length
    const QByteArray data = qCompress(json, 6).mid(4);
    req.setRawHeader("Content-Encoding", "deflate");
    m_bytes += data.size();
    return m_nam->post(req, data);
}

QJsonObject PublishClient::envelope() const
{
    QJsonObject o{
        { QStringLiteral("protocol"), 2 },
        { QStringLiteral("client"), QJsonObject{ { QStringLiteral("app"), QStringLiteral("OpenSkiPro Connect") },
                                                 { QStringLiteral("version"), QStringLiteral(OSC_VERSION) },
                                                 { QStringLiteral("instance"), m_instance } } },
        { QStringLiteral("eventDate"), m_eventDate },
        { QStringLiteral("sentAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) },
    };
    const QJsonObject clock = m_clock ? m_clock() : QJsonObject();
    if (!clock.isEmpty())
        o.insert(QStringLiteral("clock"), clock);
    return o;
}

void PublishClient::check()
{
    m_retry->stop();
    if (!m_running)
        return;
    if (!m_server.isValid() || m_server.isEmpty() || m_token.isEmpty() || m_eventDate.isEmpty()) {
        setState(State::Idle, m_token.isEmpty() ? QString() : tr("The event has no date."));
        return;
    }
    QJsonObject body{ { QStringLiteral("protocol"), 2 }, { QStringLiteral("eventDate"), m_eventDate } };
    m_reply = post(QStringLiteral("/ingest/v1/check"), body);
    QNetworkReply* r = m_reply;
    connect(r, &QNetworkReply::finished, this, [this, r] { onCheck(r); });
    setState(State::Checking, m_message);
}

void PublishClient::onCheck(QNetworkReply* r)
{
    r->deleteLater();
    if (r != m_reply)
        return; // aborted by a new target
    m_reply = nullptr;
    const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QJsonObject body = QJsonDocument::fromJson(r->readAll()).object();
    if (r->error() != QNetworkReply::NoError || status != 200) {
        handleError(r, body, status);
        return;
    }
    m_backoffMs = 0;
    const QJsonObject token = body.value(QStringLiteral("token")).toObject();
    m_tokenState = token.value(QStringLiteral("state")).toString();
    const QJsonObject race = token.value(QStringLiteral("race")).toObject();
    m_race = Race{ race.value(QStringLiteral("url")).toString(), race.value(QStringLiteral("title")).toString(),
                   race.value(QStringLiteral("date")).toString() };
    const QString message = body.value(QStringLiteral("message")).toString();
    if (!body.value(QStringLiteral("accept")).toBool()) {
        Q_EMIT log(tr("refused by the server: %1").arg(message));
        setState(State::Refused, message);
        return;
    }
    Q_EMIT log(message);
    m_checked = true;
    m_full = true;
    setState(State::Ready, message);
    send(true);
}

void PublishClient::send(bool force)
{
    m_debounce->stop();
    if (!m_running || !m_checked || m_reply)
        return;
    QJsonObject sections;
    QJsonArray remove;
    m_inFlight.clear();
    for (auto it = m_hashes.begin(); it != m_hashes.end(); ++it) {
        if (!m_full && m_have.value(it.key()) == it.value())
            continue;
        sections.insert(it.key(), QJsonObject{ { QStringLiteral("hash"), it.value() }, { QStringLiteral("data"), m_sections.value(it.key()) } });
        m_inFlight.insert(it.key(), it.value());
    }
    if (!m_full)
        for (auto it = m_have.begin(); it != m_have.end(); ++it)
            if (!m_hashes.contains(it.key()))
                remove.append(it.key());
    if (sections.isEmpty() && remove.isEmpty() && !force && !m_full)
        return;
    QJsonObject body = envelope();
    body.insert(QStringLiteral("full"), m_full);
    body.insert(QStringLiteral("sections"), sections);
    if (!remove.isEmpty())
        body.insert(QStringLiteral("remove"), remove);
    m_lastBody = QJsonDocument(body).toJson(QJsonDocument::Indented);
    Q_EMIT bodySent(m_lastBody);
    m_reply = post(QStringLiteral("/ingest/v1"), body);
    QNetworkReply* r = m_reply;
    connect(r, &QNetworkReply::finished, this, [this, r] { onIngest(r); });
    m_keepalive->start(); // the next keepalive 15 s after this request
    setState(State::Sending, m_message);
}

void PublishClient::onIngest(QNetworkReply* r)
{
    r->deleteLater();
    if (r != m_reply)
        return;
    m_reply = nullptr;
    const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QJsonObject body = QJsonDocument::fromJson(r->readAll()).object();
    if (r->error() != QNetworkReply::NoError || status != 200) {
        handleError(r, body, status);
        return;
    }
    if (m_backoffMs > 0)
        Q_EMIT log(tr("the server is reachable again"));
    m_backoffMs = 0;
    m_have.clear();
    const QJsonObject have = body.value(QStringLiteral("have")).toObject();
    for (auto it = have.begin(); it != have.end(); ++it)
        m_have.insert(it.key(), it.value().toString());
    m_full = false;
    const QJsonObject race = body.value(QStringLiteral("race")).toObject();
    m_race = Race{ race.value(QStringLiteral("url")).toString(), race.value(QStringLiteral("title")).toString(),
                   race.value(QStringLiteral("date")).toString() };
    if (race.value(QStringLiteral("firstUpload")).toBool()) {
        m_tokenState = QStringLiteral("bound");
        Q_EMIT log(tr("token bound to the race %1 (%2)").arg(m_race.title, m_race.url));
    }
    m_lastAccepted = QDateTime::currentDateTime();
    ++m_accepted;
    m_inFlight.clear();
    // what the server does not have (lost data, changes meanwhile): sent again
    int differ = 0;
    for (auto it = m_hashes.begin(); it != m_hashes.end(); ++it)
        differ += m_have.value(it.key()) == it.value() ? 0 : 1;
    for (auto it = m_have.begin(); it != m_have.end(); ++it)
        differ += m_hashes.contains(it.key()) ? 0 : 1;
    setState(State::Ready, QString());
    if (differ > 0) {
        m_full = differ * 2 > m_hashes.size();
        schedule();
    }
}

bool PublishClient::handleError(QNetworkReply* r, const QJsonObject& body, int status)
{
    const QString message = body.value(QStringLiteral("message")).toString();
    if (status == 429) {
        bool ok = false;
        const int secs = r->rawHeader("Retry-After").toInt(&ok);
        m_retry->start(ok ? qMax(1, secs) * 1000 : 1000);
        setState(State::Retrying, message.isEmpty() ? tr("Too many requests, waiting.") : message);
        return false;
    }
    if (status >= 400 && status < 500) {
        // refused: the timekeeper must change something (token, date) or send again
        const QString text = message.isEmpty() ? tr("The server refused the request (HTTP %1).").arg(status) : message;
        Q_EMIT log(tr("refused by the server: %1").arg(text));
        m_checked = false;
        setState(State::Refused, text);
        return false;
    }
    const QString text = status >= 500 ? tr("Server error (HTTP %1), trying again.").arg(status)
                                       : tr("%1 - trying again.").arg(r->errorString());
    if (m_backoffMs == 0)
        Q_EMIT log(text);
    retryLater();
    setState(State::Retrying, text);
    return true;
}

void PublishClient::retryLater()
{
    m_backoffMs = qMin(m_backoffMs == 0 ? 1000 : m_backoffMs * 2, m_maxBackoffMs); // 1 s, 2 s ... 30 s
    m_full = m_full || m_have.isEmpty();
    m_retry->start(m_backoffMs);
}
