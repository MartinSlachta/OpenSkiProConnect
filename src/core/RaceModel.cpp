#include "RaceModel.h"
#include "ApiClient.h"

#include <QJsonArray>
#include <QTime>

RaceModel::RaceModel(ApiClient* client, QObject* parent)
    : QObject(parent)
    , m_client(client)
{
    if (m_client) {
        connect(m_client, &ApiClient::message, this, &RaceModel::handle);
        connect(m_client, &ApiClient::connectedChanged, this, &RaceModel::setConnected);
    }
    // what every module needs
    require({ QStringLiteral("event"), QStringLiteral("competitors"), QStringLiteral("runs") });
}

void RaceModel::require(const QStringList& names)
{
    if (m_client)
        m_client->subscribe(names);
}

void RaceModel::setConnected(bool connected)
{
    if (connected == m_connected)
        return;
    m_connected = connected;
    Q_EMIT connectedChanged(connected);
    updateTiming();
}

bool RaceModel::timingActive() const
{
    return m_timingActive;
}

int RaceModel::timedRun() const
{
    return m_timedRun;
}

void RaceModel::updateTiming()
{
    const QJsonObject t = m_status.value(QStringLiteral("timing")).toObject();
    const bool active = m_connected && t.value(QStringLiteral("active")).toBool();
    const int run = active ? t.value(QStringLiteral("run")).toInt() : 0;
    if (active == m_timingActive && run == m_timedRun)
        return;
    m_timingActive = active;
    m_timedRun = run;
    Q_EMIT timingChanged(active, run);
}

void RaceModel::setClock(const QJsonObject& clock)
{
    if (!clock.contains(QStringLiteral("dayMs")))
        return;
    const qint64 day = clock.value(QStringLiteral("dayMs")).toInteger();
    m_clockOffsetMs = day - QTime::currentTime().msecsSinceStartOfDay();
    m_clockSynced = clock.value(QStringLiteral("synced")).toBool();
    m_utcOffsetMin = clock.value(QStringLiteral("utcOffsetMin")).toInt();
    Q_EMIT clockChanged();
}

qint64 RaceModel::deviceNowMs() const
{
    return QTime::currentTime().msecsSinceStartOfDay() + m_clockOffsetMs;
}

void RaceModel::handle(const QString& type, const QJsonObject& data)
{
    if (type == QLatin1String("hello")) {
        const QString inst = data.value(QStringLiteral("instance")).toString();
        if (!m_instance.isEmpty() && inst != m_instance) {
            // OpenSkiPro2 restarted: nothing known is valid anymore
            m_resources.clear();
            m_competitors.clear();
            m_instance = inst;
            Q_EMIT restarted();
        }
        m_instance = inst;
    } else if (type == QLatin1String("status")) {
        m_status = data;
        setClock(data.value(QStringLiteral("timing")).toObject().value(QStringLiteral("clock")).toObject());
        Q_EMIT statusChanged();
        updateTiming();
    } else if (type == QLatin1String("live")) {
        m_live = data;
        Q_EMIT liveChanged();
    } else if (type == QLatin1String("competitor")) {
        m_competitors.insert(qint64(data.value(QStringLiteral("run")).toInt()) * 10000 + data.value(QStringLiteral("bib")).toInt(), data);
        Q_EMIT competitorEvent(data);
    } else if (type == QLatin1String("resource")) {
        const QString name = data.value(QStringLiteral("name")).toString();
        if (data.value(QStringLiteral("status")).toInt() == 200)
            m_resources.insert(name, data.value(QStringLiteral("content")).toObject());
        else
            m_resources.remove(name); // e.g. 503: no event open
        if (name == QLatin1String("event"))
            Q_EMIT eventChanged();
        Q_EMIT resourceChanged(name);
    } else if (type == QLatin1String("clock")) {
        setClock(data);
    }
}

QJsonObject RaceModel::competitor(int bib) const
{
    for (const auto& v : m_resources.value(QStringLiteral("competitors")).value(QStringLiteral("competitors")).toArray())
        if (v.toObject().value(QStringLiteral("bib")).toInt() == bib)
            return v.toObject();
    return QJsonObject();
}

QList<QPair<QString, QString>> RaceModel::publicFields() const
{
    QList<QPair<QString, QString>> l;
    for (const auto& v : event().value(QStringLiteral("fields")).toArray()) {
        const QJsonObject f = v.toObject();
        const QString key = f.value(QStringLiteral("key")).toString();
        if (!key.isEmpty())
            l << qMakePair(key, f.value(QStringLiteral("label")).toString());
    }
    return l;
}

QList<int> RaceModel::groupsOf(int bib) const
{
    QList<int> l;
    for (const auto& g : competitor(bib).value(QStringLiteral("groups")).toArray())
        l << g.toInt();
    return l;
}

QString RaceModel::withGroup(const QString& ranking, int group)
{
    return ranking + (ranking.contains(QLatin1Char('?')) ? QLatin1Char('&') : QLatin1Char('?')) + QStringLiteral("group=%1").arg(group);
}

QString RaceModel::rankIn(const QString& ranking, int bib, bool group) const
{
    QString name = ranking;
    if (group) {
        const QList<int> groups = groupsOf(bib);
        if (groups.isEmpty())
            return QString();
        // OpenSkiPro2 0.7.1: the rank in every group is in the ranking itself
        for (const auto& v : m_resources.value(ranking).value(QStringLiteral("classified")).toArray()) {
            const QJsonObject e = v.toObject();
            if (e.value(QStringLiteral("bib")).toInt() != bib || !e.contains(QStringLiteral("groupRanks")))
                continue;
            for (const auto& g : e.value(QStringLiteral("groupRanks")).toArray())
                if (g.toObject().value(QStringLiteral("group")).toInt() == groups.first())
                    return QString::number(g.toObject().value(QStringLiteral("rank")).toInt());
            return QString();
        }
        name = withGroup(ranking, groups.first());
    }
    for (const auto& v : m_resources.value(name).value(QStringLiteral("classified")).toArray())
        if (v.toObject().value(QStringLiteral("bib")).toInt() == bib)
            return QString::number(v.toObject().value(QStringLiteral("rank")).toInt());
    return QString();
}
