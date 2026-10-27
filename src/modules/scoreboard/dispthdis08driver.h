#pragma once

#include "displaydriver.h"

#include <QByteArray>
#include <QDateTime>
#include <QMap>
#include <QString>

class QTcpSocket;
class QTimer;

// Driver for the "THDIS08" protocol: an STX/line/brightness/8-char-payload/
// LF TCP line protocol, identical to dispVolaBoardPro's (see that class's
// comment, including the shared '1'-'9' then 'A'+(line-10) line encoding)
// except the payload is exactly 8 characters rather than padded to 64.
// How many lines the board actually has is a board property, not a
// protocol one, so nothing is clipped by line number here.
//
// Like dispVolaBoardPro, this driver consumes BoardProgramEngine's
// already-resolved RenderedField list - multiple fields sharing one
// physical line (different character ranges) are composed into that
// line's single frame before sending. Only lines
// whose composed content actually changed since the last send are
// transmitted (a line that had content but no longer does gets one blank
// frame, then drops out of the change-tracking set).
class DispThdis08Driver : public DisplayDriver
{
    Q_OBJECT

public:
    DispThdis08Driver(QString name, QString host, quint16 port, QObject *parent = nullptr);

    QString name() const { return m_name; }
    QString host() const { return m_host; }
    quint16 port() const { return m_port; }
    bool isConnected() const override;

    qint64 bytesSent() const override { return m_bytesSent; }
    qint64 bytesReceived() const override { return m_bytesReceived; }
    QDateTime lastSentAt() const override { return m_lastSentAt; }

public slots:
    void updateFields(const QVector<BoardProgramEngine::RenderedField> &fields) override;
    // Inactive: blanks every previously-shown line, then disconnects and
    // stops auto-reconnecting until reactivated.
    void setActive(bool active) override;
    // 1-3 - the protocol's own brightness byte.
    void setBrightness(int level) override;

private slots:
    void onConnected();
    void onDisconnected();
    void onErrorOccurred();
    void onReadyRead();
    void attemptReconnect();

private:
    void scheduleReconnect();
    void sendSnapshot();
    QByteArray buildLine(int line, const QString &payload) const;
    char encodeLineNumber(int line) const;

    QString m_name;
    QString m_host;
    quint16 m_port;

    QTcpSocket *m_socket;
    QTimer *m_reconnectTimer;
    QVector<BoardProgramEngine::RenderedField> m_lastFields;
    // Change-detection cache: composed content last actually sent, per
    // line - sendSnapshot() only transmits a line whose new composed
    // content differs from this.
    QMap<int, QString> m_lastSentLineContent;
    bool m_active = true;
    int m_brightness = 3;

    qint64 m_bytesSent = 0;
    qint64 m_bytesReceived = 0;
    QDateTime m_lastSentAt;

    static constexpr int kPayloadLen = 8;
    static constexpr int kReconnectIntervalMs = 2000;
};
