#pragma once

#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <functional>

class QNetworkAccessManager;
class QTimer;
class QWebSocket;

// Client of the OpenSkiPro2 API (OpenSkiPro2/docs/api.md): one WebSocket to
// ws://host:port/ws for everything that happens, REST for the rest.
//  - reconnects every 2 s while started;
//  - checks seq: a gap means a lost message, the connection is reopened and the
//    snapshot sent on connection restores the state;
//  - keeps the subscriptions and renews them after every connection.
class ApiClient : public QObject {
    Q_OBJECT
public:
    explicit ApiClient(QObject* parent = nullptr);

    void setServer(const QString& host, quint16 port);
    QString host() const { return m_host; }
    quint16 port() const { return m_port; }
    void start();
    void stop();
    bool isStarted() const { return m_started; }
    bool isConnected() const { return m_connected; }

    // Resources pushed after every change ("event", "run/2/results?group=0", ...).
    void subscribe(const QStringList& names);
    void unsubscribe(const QStringList& names);
    QSet<QString> subscriptions() const { return m_subscriptions; }

    // GET /api/v1/<name>; callback(httpStatus, body), status 0 = not reachable.
    void get(const QString& name, std::function<void(int, const QJsonObject&)> callback);

Q_SIGNALS:
    void connectedChanged(bool connected);
    // Every message from the server (hello, status, live, competitor, changed, resource, clock ...).
    void message(const QString& type, const QJsonObject& data);
    void log(const QString& text);

private:
    void open();
    void onText(const QString& text);
    void sendSubscribe(const QStringList& names, bool subscribe);

    QWebSocket* m_ws;
    QNetworkAccessManager* m_nam;
    QTimer* m_retry;
    QString m_host = QStringLiteral("localhost");
    quint16 m_port = 8080;
    bool m_started = false;
    bool m_connected = false;
    qint64 m_seq = 0;
    QSet<QString> m_subscriptions;
};
