#pragma once

#include "boardfieldrule.h"

#include <QString>
#include <QVector>

// One configured physical display target, including its own pair of line
// programs (see BoardProgramEngine) - Timing (used while the bridge's
// timing device is actively engaged in a run) and Outside-timing (used the
// rest of the time, e.g. a static clock/date/text). Which program is
// active switches automatically with the timing window of OpenSkiPro2
// (RaceModel::timingChanged).
struct DisplayConfig
{
    QString name;
    QString driverType = QStringLiteral("dispVolaBoardPro");
    QString host;
    quint16 port = 0;
    int updateRateFps = 10;  // 1 or 10 - see BoardProgramEngine::setUpdateRateFps()
    int brightness = 3;      // 1-3 - only used if the driver's protocol supports it
    bool active = true;      // togglable at runtime - inactive blanks the board then disconnects
    QVector<BoardFieldRule> timingFields;
    QVector<BoardFieldRule> outsideTimingFields;
};

class QSettings;

namespace DisplayConfigStore {

// The "displays" array in the current group of `settings` - the [scoreboard]
// group of the profile, or the root of a SkiAlpBridgeDisplay.ini (import, same format).
QVector<DisplayConfig> load(QSettings &settings);
void save(QSettings &settings, const QVector<DisplayConfig> &configs);

} // namespace DisplayConfigStore
