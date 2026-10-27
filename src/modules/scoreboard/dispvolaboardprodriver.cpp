#include "dispvolaboardprodriver.h"

#include <QTcpSocket>
#include <QTimer>

namespace {

// Left-justifies/truncates into exactly `width` chars - a defensive
// pad/truncate only; BoardProgramEngine has already aligned/fit the text
// to the rule's own width before it reaches here.
QString fitField(const QString &value, int width)
{
    QString v = value;
    if (v.size() > width)
        v = v.left(width);
    return v.leftJustified(width, QLatin1Char(' '));
}

} // namespace

DispVolaBoardProDriver::DispVolaBoardProDriver(QString name, QString host, quint16 port, QObject *parent)
    : DisplayDriver(parent)
    , m_name(std::move(name))
    , m_host(std::move(host))
    , m_port(port)
    , m_socket(new QTcpSocket(this))
    , m_reconnectTimer(new QTimer(this))
{
    connect(m_socket, &QTcpSocket::connected, this, &DispVolaBoardProDriver::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &DispVolaBoardProDriver::onDisconnected);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &DispVolaBoardProDriver::onErrorOccurred);
    connect(m_socket, &QTcpSocket::readyRead, this, &DispVolaBoardProDriver::onReadyRead);

    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &DispVolaBoardProDriver::attemptReconnect);

    m_socket->connectToHost(m_host, m_port);
}

bool DispVolaBoardProDriver::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

void DispVolaBoardProDriver::onConnected()
{
    emit statusMessage(QStringLiteral("connected (%1:%2)").arg(m_host).arg(m_port));
    emit connectionStateChanged(true);
    // A fresh connection has no idea what the board's actually showing -
    // forget the change-detection cache so the next snapshot resends
    // everything currently active rather than assuming it already matches.
    m_lastSentLineContent.clear();
    sendSnapshot();
}

void DispVolaBoardProDriver::onDisconnected()
{
    emit statusMessage(QStringLiteral("disconnected"));
    emit connectionStateChanged(false);
    scheduleReconnect();
}

void DispVolaBoardProDriver::onErrorOccurred()
{
    emit statusMessage(QStringLiteral("error: %1").arg(m_socket->errorString()));
    scheduleReconnect();
}

void DispVolaBoardProDriver::onReadyRead()
{
    // The board never sends anything meaningful back over this protocol -
    // just count and discard, so an unexpected reply is visible in the
    // stats without needing any parsing.
    m_bytesReceived += m_socket->readAll().size();
}

void DispVolaBoardProDriver::scheduleReconnect()
{
    if (!m_active)
        return;
    if (!m_reconnectTimer->isActive())
        m_reconnectTimer->start(kReconnectIntervalMs);
}

void DispVolaBoardProDriver::attemptReconnect()
{
    if (m_active && m_socket->state() == QAbstractSocket::UnconnectedState)
        m_socket->connectToHost(m_host, m_port);
}

void DispVolaBoardProDriver::updateFields(const QVector<BoardProgramEngine::RenderedField> &fields)
{
    m_lastFields = fields;
    sendSnapshot();
}

void DispVolaBoardProDriver::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;

    if (!m_active) {
        m_lastFields.clear();
        sendSnapshot(); // blanks every previously-shown row while still connected
        m_reconnectTimer->stop();
        if (m_socket->state() != QAbstractSocket::UnconnectedState)
            m_socket->close();
        emit statusMessage(QStringLiteral("deactivated"));
        emit connectionStateChanged(false);
    } else {
        emit statusMessage(QStringLiteral("reactivating"));
        m_socket->connectToHost(m_host, m_port);
    }
}

void DispVolaBoardProDriver::setBrightness(int level)
{
    m_brightness = qBound(1, level, 3);
    m_lastSentLineContent.clear(); // an intensity change needs a resend even if the text itself is unchanged
    sendSnapshot();
}

QByteArray DispVolaBoardProDriver::buildLine(int line, const QString &payload) const
{
    QByteArray frame;
    frame.append(char(0x02));                            // STX
    frame.append(static_cast<char>('0' + line));         // row number, '1'-'8'
    frame.append(static_cast<char>('0' + m_brightness)); // intensity, '1'-'3'
    frame.append(fitField(payload, kPayloadLen).toLatin1());
    frame.append(char(0x0A)); // LF
    return frame;
}

void DispVolaBoardProDriver::sendSnapshot()
{
    if (m_socket->state() != QAbstractSocket::ConnectedState)
        return;

    // Multiple fields can share one physical row at different character
    // ranges (e.g. bib at 1-4, rank at 5-8) - compose each row's buffer
    // before comparing/sending. Fields already arrive pre-aligned/fitted to
    // their own width by BoardProgramEngine.
    QMap<int, QString> lineBuffers;
    for (const BoardProgramEngine::RenderedField &field : m_lastFields) {
        if (field.line < 1 || field.line > kRowCount)
            continue; // no such row on this board
        const int startChar = qMax(1, field.startChar);
        const int endChar = qMin(kPayloadLen, field.endChar);
        const int width = endChar - startChar + 1;
        if (width <= 0)
            continue;

        QString &buffer = lineBuffers[field.line];
        if (buffer.isEmpty())
            buffer = QString(kPayloadLen, QLatin1Char(' '));
        const QString fitted = fitField(field.text, width);
        for (int i = 0; i < width; ++i)
            buffer[startChar - 1 + i] = fitted.at(i);
    }

    // Blank any row that had content last round but isn't targeted by any
    // field this round, so content disappearing (e.g. a finish overlay
    // expiring) doesn't leave stale text on the physical display.
    for (auto it = m_lastSentLineContent.constBegin(); it != m_lastSentLineContent.constEnd(); ++it) {
        if (!lineBuffers.contains(it.key()))
            lineBuffers[it.key()] = QString(kPayloadLen, QLatin1Char(' '));
    }

    qint64 sentThisRound = 0;
    QMap<int, QString> newSentContent;
    for (auto it = lineBuffers.constBegin(); it != lineBuffers.constEnd(); ++it) {
        const int line = it.key();
        const QString &content = it.value();
        if (m_lastSentLineContent.value(line) != content)
            sentThisRound += m_socket->write(buildLine(line, content));
        newSentContent.insert(line, content);
    }
    m_lastSentLineContent = newSentContent;

    if (sentThisRound > 0) {
        m_socket->flush();
        m_lastSentAt = QDateTime::currentDateTime();
    }
    m_bytesSent += sentThisRound;
}
