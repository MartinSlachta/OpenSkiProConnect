#pragma once

#include "boardprogramengine.h"

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QVector>

// Common interface for anything that can render bridge data onto a physical
// display. Content always arrives through updateFields() -
// BoardProgramEngine's generic, freely configurable per-display line
// program (see boardprogramengine.h); there is no driver-specific content
// pathway any more. setActive()/setBrightness() are opt-in - a driver whose
// protocol doesn't support one just ignores it.
class DisplayDriver : public QObject
{
    Q_OBJECT

public:
    explicit DisplayDriver(QObject *parent = nullptr) : QObject(parent) {}
    ~DisplayDriver() override = default;

    virtual bool isConnected() const = 0;

    // Diagnostics for the GUI's status column - default to "no data" so a
    // driver that doesn't track these doesn't need boilerplate overrides.
    virtual qint64 bytesSent() const { return 0; }
    virtual qint64 bytesReceived() const { return 0; }
    virtual QDateTime lastSentAt() const { return QDateTime(); }

public slots:
    virtual void updateFields(const QVector<BoardProgramEngine::RenderedField> &fields) { Q_UNUSED(fields); }
    // Inactive: blank whatever's currently shown, then disconnect: Active:
    // reconnect. Toggled at runtime from the Displays table.
    virtual void setActive(bool active) { Q_UNUSED(active); }
    // 1-3 - only meaningful if the driver's protocol has a brightness byte.
    virtual void setBrightness(int level) { Q_UNUSED(level); }

signals:
    void statusMessage(const QString &message);
    void connectionStateChanged(bool connected);
};
