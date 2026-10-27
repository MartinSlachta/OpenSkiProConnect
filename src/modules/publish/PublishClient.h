#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

// Client of the ingest protocol 1 of OpenSkiPro Publish (docs/publish-protocol.md):
// keeps the wanted state of one race (the sections) and brings the server to it -
// only changed sections, one request in flight, debounce, keepalive, backoff after
// network errors, a stop with the message of the server after a refusal. A new token,
// event date or server starts again with /ingest/v1/check and a full state.
class PublishClient : public QObject {
    Q_OBJECT
public:
    enum class State {
        Idle,     // no server / token, or stopped
        Checking, // /ingest/v1/check
        Ready,    // the server has the state
        Sending,
        Retrying, // network / server error, waiting for the next try
        Refused,  // the server refused (message), waits for a change or sendAgain()
    };
    Q_ENUM(State)

    struct Race {
        QString url, title, date;
    };

    explicit PublishClient(QObject* parent = nullptr);

    // The race to publish; a change starts again (check + full state).
    void setTarget(const QUrl& server, const QString& token, const QString& eventDate);
    QUrl server() const { return m_server; }
    QString token() const { return m_token; }
    // The wanted state (name -> data).
    void setSections(const QMap<QString, QJsonObject>& sections);
    // Clock of the timing device at the moment of a request ({dayMs, synced}, empty: none).
    void setClockSource(std::function<QJsonObject()> source) { m_clock = std::move(source); }

    void start();
    void stop();
    bool isRunning() const { return m_running; }
    // After a refusal or anyway: check the token again and send everything.
    void sendAgain();

    State state() const { return m_state; }
    QString message() const { return m_message; } // last message of the server / error, word for word
    QString tokenState() const { return m_tokenState; } // "free" / "bound" / "" (from check)
    Race race() const { return m_race; }
    QDateTime lastAccepted() const { return m_lastAccepted; }
    int acceptedCount() const { return m_accepted; }
    qint64 bytesSent() const { return m_bytes; }
    // Body of the last request (plain JSON, before compression) - fixtures for the server.
    QByteArray lastBody() const { return m_lastBody; }

    // Intervals (tests make them short).
    void setTimings(int debounceMs, int keepaliveMs, int maxBackoffMs);

    static QString hashOf(const QJsonObject& data); // SHA-1 hex of the compact JSON

Q_SIGNALS:
    void stateChanged();
    void log(const QString& text);
    void bodySent(const QByteArray& body); // plain JSON of every ingest request

private:
    void setState(State s, const QString& message = QString());
    void reset();
    void schedule();
    void check();
    void send(bool keepalive);
    QNetworkReply* post(const QString& path, const QJsonObject& body);
    void onCheck(QNetworkReply* r);
    void onIngest(QNetworkReply* r);
    // false: 4xx handled (refused / rate limited), true: network or server error
    bool handleError(QNetworkReply* r, const QJsonObject& body, int status);
    void retryLater();
    QJsonObject envelope() const;

    QNetworkAccessManager* m_nam;
    QTimer* m_debounce;
    QTimer* m_keepalive;
    QTimer* m_retry;
    QUrl m_server;
    QString m_token;
    QString m_eventDate;
    QString m_instance;
    bool m_running = false;

    QMap<QString, QJsonObject> m_sections;  // wanted
    QHash<QString, QString> m_hashes;       // wanted: name -> hash
    QHash<QString, QString> m_have;         // on the server (last "have")
    bool m_full = true;                     // next request: the complete state
    bool m_checked = false;
    QPointer<QNetworkReply> m_reply;        // the request in flight
    QHash<QString, QString> m_inFlight;     // sections of the request in flight
    std::function<QJsonObject()> m_clock;

    State m_state = State::Idle;
    QString m_message;
    QString m_tokenState;
    Race m_race;
    QDateTime m_lastAccepted;
    int m_accepted = 0;
    qint64 m_bytes = 0;
    QByteArray m_lastBody;
    int m_backoffMs = 0;
    int m_maxBackoffMs = 30000;
};
