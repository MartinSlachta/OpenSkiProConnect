#pragma once

#include "ScreenConfig.h"

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <memory>

class QHttpServer;
class QHttpServerRequest;
class QHttpServerResponder;
class QTcpServer;
class QTimer;
class QWebSocket;
class ScreenData;

// The web server of the module Screen (docs/screen-design.md 3, 9): serves the pages
// (compiled into the exe), the images of the configurations and the flags, and pushes
// over its WebSocket the configuration chosen in the module, the view model of
// ScreenData, the clock of the timing device and the message of the operator.
//   /                 the two pages
//   /board            the TV board (every TV opens the same address; /board/<any name>
//                     too, /board/preview-<id> shows an unsaved configuration of the editor)
//   /speaker          the page of the speaker (/board/speaker too, ?board=preview-<id>)
//   /web/<file>       pages, scripts, styles
//   /images/<file>    images of the configurations (folder screens/images)
//   /flags/<code>     flag of a nation (folder of flags: SVK.png, SVK.svg ...)
//   /ws?page=board|speaker[&board=preview-<id>]  WebSocket
class ScreenServer : public QObject {
    Q_OBJECT
public:
    explicit ScreenServer(ScreenData* data, QObject* parent = nullptr);
    ~ScreenServer() override;

    bool start(quint16 port, QString* error);
    void stop();
    bool isListening() const;
    quint16 port() const;
    QStringList urls() const; // http://<every IPv4 address>:<port>/

    void setFolders(const QString& configs, const QString& flags);
    QString configFolder() const { return m_configFolder; }
    // The configuration of every screen: a file of the folder of the configurations.
    void setConfigFile(const QString& file);
    QString configFile() const { return m_configFile; }
    void setMessage(const QString& message); // full screen message on every screen, empty: none
    QString message() const { return m_message; }
    // Unsaved configuration of the editor, shown on /board/preview-<id>.
    void setPreview(const QString& id, const ScreenConfig& config);
    // A configuration file was saved: its boards get it again.
    void reloadConfigs();

    int clientCount(const QString& page = QString()) const; // page: board / speaker, empty: all
    // The configuration (of the file, or a preview of the editor), loaded from its file.
    ScreenConfig configOf(const QString& preview = QString()) const;

Q_SIGNALS:
    void clientsChanged();

private:
    struct Client {
        QWebSocket* socket = nullptr;
        QString page;    // board / speaker
        QString preview; // preview-<id> of the editor, else empty
        QJsonObject configSent;
    };

    void onRequest(const QHttpServerRequest& req, QHttpServerResponder& resp);
    void onNewWebSocket();
    void send(Client& c, const QString& type, const QJsonObject& data);
    void sendConfig(Client& c, bool force);
    void broadcastState();
    void broadcastClock();
    QByteArray indexPage() const;

    ScreenData* m_data;
    std::unique_ptr<QHttpServer> m_http;
    QTcpServer* m_tcp = nullptr;
    QHash<QWebSocket*, Client*> m_clients;
    QTimer* m_clock;
    QString m_configFolder;
    QString m_flagsFolder;
    QString m_configFile;
    QString m_message;
    QHash<QString, ScreenConfig> m_previews;
    // configurations read from their files, reloaded when the file changes
    struct Cached {
        QDateTime modified;
        ScreenConfig config;
    };
    mutable QHash<QString, Cached> m_cache;
};
