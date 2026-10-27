#pragma once

#include <QByteArray>
#include <QFile>
#include <QList>
#include <QObject>
#include <QString>

class QTcpSocket;
class QTimer;

// TCP connection to the FIS Live server (specification v53, 2, 15, 16):
//  - every message gets the next sequence number; the server replies
//    <livetiming sequence="..."> for each message received;
//  - messages not acknowledged stay in a queue (their bodies also in a file, so a
//    restart of Connect loses nothing) and are sent again, in order, after a
//    reconnection; the queue is kept across connection drops;
//  - <keepalive/> after 5 minutes without any message;
//  - every message sent / received is written to the XML log, password masked.
class FisLiveSender : public QObject {
    Q_OBJECT
public:
    explicit FisLiveSender(QObject* parent = nullptr);
    ~FisLiveSender() override;

    struct Settings {
        QString host = QStringLiteral("livedata.fis-ski.com");
        quint16 port = 1550;
        QString codex;
        QString password;
    };
    void setSettings(const Settings& s);
    Settings settings() const { return m_settings; }
    // Folder of the logs and of the queue file ("" = none, tests).
    void setFolder(const QString& folder);
    void setKeepaliveMs(int ms);

    void start();
    void stop();
    bool isRunning() const { return m_running; }
    bool isConnected() const;

    // Adds one message (its body, without <livetiming>) to the queue.
    void send(const QByteArray& body);
    int pending() const { return int(m_queue.size()); } // not acknowledged
    int lastSequence() const { return m_sequence; }
    int sentCount() const { return m_sentCount; }
    int ackCount() const { return m_ackCount; }

Q_SIGNALS:
    void stateChanged();
    void log(const QString& text);

private:
    struct Item {
        int sequence;
        QByteArray body; // without <livetiming>: the password never reaches the queue file
        bool written = false; // written on the current connection
    };
    void connectNow();
    void flush();
    void onReadyRead();
    void saveQueue();
    void loadQueue();
    void writeLog(const char* direction, const QByteArray& data);
    QString queuePath() const;

    Settings m_settings;
    QString m_folder;
    QTcpSocket* m_socket;
    QTimer* m_retry;
    QTimer* m_keepalive;
    bool m_running = false;
    int m_sequence = 0;
    int m_sentCount = 0;
    int m_ackCount = 0;
    QList<Item> m_queue;
    QByteArray m_incoming;
    QFile m_log;
};
