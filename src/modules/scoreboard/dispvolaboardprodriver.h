#pragma once

#include "displaydriver.h"

#include <QByteArray>
#include <QDateTime>
#include <QMap>
#include <QString>

class QTcpSocket;
class QTimer;

// Driver for the "dispVolaBoardPro" protocol: a raw TCP line protocol -
// STX(0x02) N(line) L('1'-'3', intensity) <64 ASCII chars> LF(0x0A) - one
// frame per line, where N encodes the line number as '1'-'9' then
// 'A'+(line-10) for line 10 and up (identical to THDIS08's encoding).
//
// How many lines and columns exist is a property of the board, not of the
// protocol: the frame always carries 64 characters regardless. The board
// this driver was written against is 8 rows x 18 columns, so rows outside
// 1-8 are dropped here and everything past column 18 of the payload is
// simply ignored by the hardware.
//
// Content comes from BoardProgramEngine's already-resolved RenderedField
// list (same as DispThdis08Driver - see that class's comment): multiple
// fields sharing one row at different character ranges are composed into
// that row's single frame, and only rows whose composed content actually
// changed since the last send are transmitted. "Edit lines" deliberately
// doesn't validate against any particular board's shape, so clipping to
// what the hardware can show is the driver's job.
//
// Outbound TCP client, auto-reconnects on a fixed interval. Re-sends
// everything currently active on (re)connect so a dropped connection
// doesn't leave the board blank or stale once it comes back.
class DispVolaBoardProDriver : public DisplayDriver
{
    Q_OBJECT

public:
    DispVolaBoardProDriver(QString name, QString host, quint16 port, QObject *parent = nullptr);

    QString name() const { return m_name; }
    QString host() const { return m_host; }
    quint16 port() const { return m_port; }
    bool isConnected() const override;

    // Diagnostics for the GUI's status column. Cumulative since the driver
    // was created (not reset on reconnect) - a running total is simpler and
    // more useful than "since last reconnect" for a display that's meant to
    // stay up all day. The board never sends anything meaningful back over
    // this protocol, so bytesReceived() will normally read 0 - it's here so
    // an unexpected reply (or line noise) is visible rather than silently
    // discarded.
    qint64 bytesSent() const override { return m_bytesSent; }
    qint64 bytesReceived() const override { return m_bytesReceived; }
    QDateTime lastSentAt() const override { return m_lastSentAt; }

public slots:
    void updateFields(const QVector<BoardProgramEngine::RenderedField> &fields) override;

    // Inactive: blanks every previously-shown row, then disconnects and
    // stops auto-reconnecting until reactivated.
    void setActive(bool active) override;
    // 1-3 - the protocol's own intensity byte.
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

    QString m_name;
    QString m_host;
    quint16 m_port;

    QTcpSocket *m_socket;
    QTimer *m_reconnectTimer;
    QVector<BoardProgramEngine::RenderedField> m_lastFields;
    // Change-detection cache: composed content last actually sent, per row -
    // sendSnapshot() only transmits a row whose new composed content differs
    // from this.
    QMap<int, QString> m_lastSentLineContent;
    bool m_active = true;
    int m_brightness = 3;

    qint64 m_bytesSent = 0;
    qint64 m_bytesReceived = 0;
    QDateTime m_lastSentAt;

    static constexpr int kRowCount = 8;
    static constexpr int kPayloadLen = 64;
    static constexpr int kReconnectIntervalMs = 2000;
};
