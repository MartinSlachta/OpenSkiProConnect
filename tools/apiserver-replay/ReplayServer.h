#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSet>
#include <memory>

class QHttpServer;
class QHttpServerRequest;
class QHttpServerResponder;
class QTcpServer;
class QTimer;
class QWebSocket;

// A fake OpenSkiPro2: serves a recording of osp-apicheck (one JSON line per
// WebSocket message received) as the REST + WebSocket API, so Connect and its
// modules can be developed and tested without OpenSkiPro2 and a real race.
//  - WebSocket /ws: hello + status + live of the current position, then the
//    recorded messages in their original rhythm (play) or step by step (tests);
//    "resource" messages only for the resources the client subscribed to.
//  - REST /api/v1/<name>: the last content of that resource up to the position.
class ReplayServer : public QObject {
    Q_OBJECT
public:
    explicit ReplayServer(QObject* parent = nullptr);
    ~ReplayServer() override;

    bool load(const QString& jsonlPath, QString* error);
    bool start(quint16 port, QString* error); // 0: any free port
    quint16 port() const;

    int count() const { return int(m_messages.size()); }
    int position() const { return m_pos; } // messages already sent
    // Sends the messages up to (excluding) index.
    void stepTo(int index);
    void play(double speed = 1.0);
    void pause();
    int clientCount() const { return int(m_clients.size()); }

Q_SIGNALS:
    void positionChanged(int position);
    void finished();

private:
    struct Message {
        qint64 rx = 0;
        QString type;
        QJsonObject data;
    };
    struct Client {
        QWebSocket* socket = nullptr;
        qint64 seq = 0;
        QSet<QString> subscriptions;
    };

    void apply(const Message& m); // state of the fake server
    void broadcast(const Message& m);
    void send(Client& c, const QString& type, const QJsonObject& data);
    void sendResource(Client& c, const QString& name);
    void onRequest(const QHttpServerRequest& req, QHttpServerResponder& resp);
    void onNewWebSocket();
    void scheduleNext();

    QList<Message> m_messages;
    int m_pos = 0;
    QJsonObject m_hello;
    QJsonObject m_status;
    QJsonObject m_live;
    QHash<QString, QJsonObject> m_resources; // name -> data of the resource message (name, rev, status, content)

    std::unique_ptr<QHttpServer> m_http;
    QTcpServer* m_tcp = nullptr;
    QHash<QWebSocket*, Client*> m_clients;
    QTimer* m_timer;
    double m_speed = 1.0;
};
