#include "displayconfig.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

namespace {

QVector<BoardFieldRule> rulesFromJsonArray(const QJsonArray &array)
{
    QVector<BoardFieldRule> rules;
    rules.reserve(array.size());
    for (const QJsonValue &v : array)
        rules.append(BoardFieldRule::fromJsonObject(v.toObject()));
    return rules;
}

QJsonArray rulesToJsonArray(const QVector<BoardFieldRule> &rules)
{
    QJsonArray array;
    for (const BoardFieldRule &rule : rules)
        array.append(rule.toJsonObject());
    return array;
}

} // namespace

namespace DisplayConfigStore {

QVector<DisplayConfig> load(QSettings &settings)
{
    QVector<DisplayConfig> configs;

    const int count = settings.beginReadArray(QStringLiteral("displays"));
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        DisplayConfig config;
        config.name = settings.value(QStringLiteral("name")).toString();
        config.driverType =
            settings.value(QStringLiteral("driverType"), QStringLiteral("dispVolaBoardPro")).toString();
        config.host = settings.value(QStringLiteral("host")).toString();
        config.port = static_cast<quint16>(settings.value(QStringLiteral("port")).toUInt());
        config.updateRateFps = settings.value(QStringLiteral("updateRateFps"), 10).toInt();
        config.brightness = settings.value(QStringLiteral("brightness"), 3).toInt();
        config.active = settings.value(QStringLiteral("active"), true).toBool();

        // Stored as a compact JSON blob (a variable-length nested list)
        // rather than QSettings' own nested-array API - simpler, and
        // consistent with how the rest of the app already leans on
        // QJsonArray/QJsonObject. Absent/malformed just means no lines yet
        // (a pre-existing saved config, or one that was never given any).
        const QJsonObject linePrograms =
            QJsonDocument::fromJson(settings.value(QStringLiteral("linePrograms")).toString().toUtf8()).object();
        config.timingFields = rulesFromJsonArray(linePrograms.value(QStringLiteral("timing")).toArray());
        config.outsideTimingFields = rulesFromJsonArray(linePrograms.value(QStringLiteral("outsideTiming")).toArray());

        configs.append(config);
    }
    settings.endArray();

    return configs;
}

void save(QSettings &settings, const QVector<DisplayConfig> &configs)
{
    settings.remove(QStringLiteral("displays")); // fewer displays than before: no leftovers

    settings.beginWriteArray(QStringLiteral("displays"));
    for (int i = 0; i < configs.size(); ++i) {
        settings.setArrayIndex(i);
        // Clear this entry's group before rewriting it: QSettings only
        // overwrites the keys we set, so a key we've stopped writing would
        // otherwise survive every save forever. That's exactly what happened
        // to v1's per-display "contentType"/"fields" - they sat in saved INIs
        // long after the code had stopped reading them.
        settings.remove(QString());

        const DisplayConfig &config = configs.at(i);
        settings.setValue(QStringLiteral("name"), config.name);
        settings.setValue(QStringLiteral("driverType"), config.driverType);
        settings.setValue(QStringLiteral("host"), config.host);
        settings.setValue(QStringLiteral("port"), config.port);
        settings.setValue(QStringLiteral("updateRateFps"), config.updateRateFps);
        settings.setValue(QStringLiteral("brightness"), config.brightness);
        settings.setValue(QStringLiteral("active"), config.active);

        QJsonObject linePrograms;
        linePrograms[QStringLiteral("timing")] = rulesToJsonArray(config.timingFields);
        linePrograms[QStringLiteral("outsideTiming")] = rulesToJsonArray(config.outsideTimingFields);
        settings.setValue(QStringLiteral("linePrograms"),
                          QString::fromUtf8(QJsonDocument(linePrograms).toJson(QJsonDocument::Compact)));
    }
    settings.endArray();
}

} // namespace DisplayConfigStore
