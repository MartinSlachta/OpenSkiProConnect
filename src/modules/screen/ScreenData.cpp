#include "ScreenData.h"

#include "core/RaceModel.h"

#include <QTime>
#include <QTimer>

namespace {

// groupRanks of the API ([{group, rank, gap}]) as {group: rank} and {group: gap}
void groupRanksOf(const QJsonArray& groupRanks, QJsonObject* ranks, QJsonObject* gaps)
{
    for (const auto& v : groupRanks) {
        const QJsonObject g = v.toObject();
        const QString key = QString::number(g.value(QStringLiteral("group")).toInt());
        ranks->insert(key, QString::number(g.value(QStringLiteral("rank")).toInt()));
        if (gaps)
            gaps->insert(key, g.value(QStringLiteral("gap")).toString());
    }
}

QString str(const QJsonObject& o, const char* key)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isDouble() ? QString::number(v.toInteger()) : v.toString();
}

QString statusText(const QString& state)
{
    if (state == QLatin1String("notProcessed"))
        return QString();
    return state.toUpper();
}

} // namespace

ScreenData::ScreenData(RaceModel* model, QObject* parent)
    : QObject(parent)
    , m_model(model)
    , m_timer(new QTimer(this))
{
    // several messages of one change (competitor, live, changed, resources) give one state
    m_timer->setSingleShot(true);
    m_timer->setInterval(80);
    connect(m_timer, &QTimer::timeout, this, &ScreenData::rebuild);
    connect(model, &RaceModel::liveChanged, this, &ScreenData::schedule);
    connect(model, &RaceModel::resourceChanged, this, &ScreenData::schedule);
    connect(model, &RaceModel::statusChanged, this, &ScreenData::schedule);
    connect(model, &RaceModel::connectedChanged, this, &ScreenData::schedule);
    connect(model, &RaceModel::restarted, this, &ScreenData::schedule);
    connect(model, &RaceModel::eventChanged, this, &ScreenData::requireResources);
    requireResources();
    rebuild();
}

void ScreenData::schedule()
{
    if (!m_timer->isActive())
        m_timer->start();
}

void ScreenData::requireResources()
{
    // every run: start list, ranking of the run, ranking after the run (a few resources)
    const int runs = qMax(1, m_model->event().value(QStringLiteral("type")).toObject().value(QStringLiteral("runCount")).toInt());
    QStringList names;
    for (int r = 1; r <= runs; ++r)
        names << QStringLiteral("run/%1/startlist").arg(r) << QStringLiteral("run/%1/results").arg(r)
              << QStringLiteral("results?upto=%1").arg(r);
    m_model->require(names);
    schedule();
}

QString ScreenData::formatMs(qint64 ms, int precision)
{
    if (ms < 0)
        return QString();
    precision = qBound(0, precision, 3);
    qint64 div = 1;
    for (int i = precision; i < 3; ++i)
        div *= 10;
    const qint64 units = ms / div; // truncated, never rounded
    qint64 scale = 1;
    for (int i = 0; i < precision; ++i)
        scale *= 10;
    const qint64 secs = units / scale;
    const qint64 frac = units % scale;
    QString s = secs >= 60 ? QStringLiteral("%1:%2").arg(secs / 60).arg(secs % 60, 2, 10, QLatin1Char('0')) : QString::number(secs);
    if (precision > 0)
        s += QLatin1Char('.') + QStringLiteral("%1").arg(frac, precision, 10, QLatin1Char('0'));
    return s;
}

QJsonObject ScreenData::clock() const
{
    return QJsonObject{ { QStringLiteral("dayMs"), m_model->deviceNowMs() }, { QStringLiteral("synced"), m_model->clockSynced() } };
}

int ScreenData::currentRun() const
{
    if (m_model->timedRun() > 0)
        return m_model->timedRun();
    int last = 0;
    for (const auto& v : m_model->resource(QStringLiteral("runs")).value(QStringLiteral("runs")).toArray()) {
        const QJsonObject r = v.toObject();
        if (r.value(QStringLiteral("used")).toBool(true) && r.value(QStringLiteral("state")).toString() != QLatin1String("notStarted"))
            last = r.value(QStringLiteral("run")).toInt();
    }
    return qMax(1, last);
}

int ScreenData::startListRun() const
{
    if (m_model->timedRun() > 0)
        return m_model->timedRun();
    for (const auto& v : m_model->resource(QStringLiteral("runs")).value(QStringLiteral("runs")).toArray()) {
        const QJsonObject r = v.toObject();
        if (r.value(QStringLiteral("used")).toBool(true) && r.value(QStringLiteral("state")).toString() == QLatin1String("notStarted"))
            return r.value(QStringLiteral("run")).toInt();
    }
    return currentRun();
}

QString ScreenData::phase() const
{
    if (m_model->timingActive())
        return QStringLiteral("live");
    for (const auto& v : m_model->resource(QStringLiteral("runs")).value(QStringLiteral("runs")).toArray())
        if (v.toObject().value(QStringLiteral("state")).toString() != QLatin1String("notStarted"))
            return QStringLiteral("results");
    return QStringLiteral("beforeRace");
}

QJsonObject ScreenData::base(const QJsonObject& c) const
{
    QJsonObject row{
        { QStringLiteral("bib"), str(c, "bib") },
        { QStringLiteral("name"), str(c, "name") },
        { QStringLiteral("lastName"), str(c, "lastName") },
        { QStringLiteral("firstName"), str(c, "firstName") },
        { QStringLiteral("nation"), str(c, "nation") },
        { QStringLiteral("club"), str(c, "club") },
        { QStringLiteral("team"), str(c, "team") },
        { QStringLiteral("region"), str(c, "region") },
        { QStringLiteral("year"), str(c, "year") },
        { QStringLiteral("gender"), str(c, "gender") },
        { QStringLiteral("category"), str(c, "category") },
        { QStringLiteral("code"), str(c, "code") },
        { QStringLiteral("custom"), str(c, "custom") },
    };
    QJsonArray groups = c.value(QStringLiteral("groups")).toArray();
    if (groups.isEmpty()) // embedded competitor objects may be shorter: the full one
        groups = m_model->competitor(c.value(QStringLiteral("bib")).toInt()).value(QStringLiteral("groups")).toArray();
    row.insert(QStringLiteral("groups"), groups);
    if (!groups.isEmpty()) {
        const int g = groups.first().toInt();
        for (const auto& v : m_model->event().value(QStringLiteral("groups")).toArray())
            if (v.toObject().value(QStringLiteral("index")).toInt() == g)
                row.insert(QStringLiteral("group"), v.toObject().value(QStringLiteral("label")).toString());
    }
    return row;
}

// Ranks and gaps inside every output group: from the API (groupRanks, OpenSkiPro2 0.7.1),
// else from the order of the ranking - the same time gives the same rank (as the API).
void ScreenData::addGroupRanks(QJsonArray& rows) const
{
    if (!rows.isEmpty() && rows.first().toObject().contains(QStringLiteral("groupRanks"))) {
        for (int i = 0; i < rows.size(); ++i) {
            QJsonObject row = rows.at(i).toObject();
            const QJsonObject ranks = row.value(QStringLiteral("groupRanks")).toObject();
            const QJsonObject gaps = row.value(QStringLiteral("groupGaps")).toObject();
            const QJsonArray groups = row.value(QStringLiteral("groups")).toArray();
            if (!groups.isEmpty()) {
                const QString key = QString::number(groups.first().toInt());
                row.insert(QStringLiteral("groupRank"), ranks.value(key));
                row.insert(QStringLiteral("groupGap"), gaps.value(key));
            }
            rows[i] = row;
        }
        return;
    }
    struct G {
        int count = 0;
        int rank = 0;
        qint64 lastMs = -1;
        qint64 firstMs = -1;
    };
    QHash<int, G> gs;
    for (int i = 0; i < rows.size(); ++i) {
        QJsonObject row = rows.at(i).toObject();
        const qint64 ms = row.value(QStringLiteral("timeMs")).toInteger(-1);
        QJsonObject ranks, gaps;
        for (const auto& gv : row.value(QStringLiteral("groups")).toArray()) {
            G& g = gs[gv.toInt()];
            ++g.count;
            if (ms != g.lastMs)
                g.rank = g.count;
            g.lastMs = ms;
            if (g.firstMs < 0)
                g.firstMs = ms;
            const QString key = QString::number(gv.toInt());
            ranks.insert(key, QString::number(g.rank));
            gaps.insert(key, ms > g.firstMs ? formatMs(ms - g.firstMs, m_precision) : QString());
        }
        row.insert(QStringLiteral("groupRanks"), ranks);
        row.insert(QStringLiteral("groupGaps"), gaps);
        const QJsonArray groups = row.value(QStringLiteral("groups")).toArray();
        if (!groups.isEmpty()) {
            const QString key = QString::number(groups.first().toInt());
            row.insert(QStringLiteral("groupRank"), ranks.value(key));
            row.insert(QStringLiteral("groupGap"), gaps.value(key));
        }
        rows[i] = row;
    }
}

QJsonObject ScreenData::ranking(const QString& name, bool total, int run) const
{
    const QJsonObject res = m_model->resource(name);
    QJsonArray rows;
    for (const auto& v : res.value(QStringLiteral("classified")).toArray()) {
        const QJsonObject e = v.toObject();
        QJsonObject row = base(e.value(QStringLiteral("competitor")).toObject());
        row.insert(QStringLiteral("rank"), str(e, "rank"));
        row.insert(QStringLiteral("time"), str(e, "time"));
        row.insert(QStringLiteral("timeMs"), e.value(QStringLiteral("timeMs")).toInteger());
        row.insert(QStringLiteral("gap"), str(e, "gap"));
        if (e.contains(QStringLiteral("groupRanks"))) { // OpenSkiPro2 0.7.1
            QJsonObject ranks, gaps;
            groupRanksOf(e.value(QStringLiteral("groupRanks")).toArray(), &ranks, &gaps);
            row.insert(QStringLiteral("groupRanks"), ranks);
            row.insert(QStringLiteral("groupGaps"), gaps);
        }
        for (const auto& rv : e.value(QStringLiteral("runs")).toArray()) {
            const QJsonObject r = rv.toObject();
            const QString key = QStringLiteral("run%1").arg(r.value(QStringLiteral("run")).toInt());
            row.insert(key, str(r, "time"));
            row.insert(key + QStringLiteral("Rank"), str(r, "rank"));
            if (r.contains(QStringLiteral("groupRanks"))) { // the rank in each group in that run
                QJsonObject ranks;
                groupRanksOf(r.value(QStringLiteral("groupRanks")).toArray(), &ranks, nullptr);
                row.insert(key + QStringLiteral("GroupRanks"), ranks);
            }
        }
        if (!total) { // the time of the run is also "run<n>"
            row.insert(QStringLiteral("run%1").arg(run), str(e, "time"));
            row.insert(QStringLiteral("run%1Rank").arg(run), str(e, "rank"));
            if (row.contains(QStringLiteral("groupRanks")))
                row.insert(QStringLiteral("run%1GroupRanks").arg(run), row.value(QStringLiteral("groupRanks")));
        }
        // the current run whatever its number; empty in the 1st run, where it is the time itself
        if (run > 1) {
            row.insert(QStringLiteral("runTime"), row.value(QStringLiteral("run%1").arg(run)));
            row.insert(QStringLiteral("runRank"), row.value(QStringLiteral("run%1Rank").arg(run)));
        }
        for (const auto& iv : e.value(QStringLiteral("inter")).toArray()) {
            const QJsonObject in = iv.toObject();
            const QString key = QStringLiteral("inter%1").arg(in.value(QStringLiteral("point")).toInt());
            row.insert(key, str(in, "time"));
            row.insert(key + QStringLiteral("Gap"), str(in, "gap"));
            row.insert(key + QStringLiteral("Rank"), str(in, "rank"));
        }
        rows.append(row);
    }
    addGroupRanks(rows);

    QJsonArray unclassified;
    QHash<QString, int> counts;
    for (const auto& gv : res.value(QStringLiteral("unclassified")).toArray()) {
        const QJsonObject g = gv.toObject();
        const QString kind = g.value(QStringLiteral("kind")).toString();
        QJsonArray entries;
        for (const auto& ev : g.value(QStringLiteral("entries")).toArray()) {
            const QJsonObject e = ev.toObject();
            QJsonObject row = base(e.value(QStringLiteral("competitor")).toObject());
            const QString status = kind == QLatin1String("notProcessed") ? QString() : kind;
            row.insert(QStringLiteral("status"), status);
            row.insert(QStringLiteral("reason"), str(e, "reason"));
            // in the same columns: the status under the time (and its run), the reason (gate) under the gap
            row.insert(QStringLiteral("time"), status);
            row.insert(QStringLiteral("run%1").arg(g.value(QStringLiteral("run")).toInt(run)), status);
            if (!str(e, "reason").isEmpty())
                row.insert(QStringLiteral("gap"), QStringLiteral("(%1)").arg(str(e, "reason")));
            entries.append(row);
        }
        counts[kind] += int(entries.size());
        unclassified.append(QJsonObject{ { QStringLiteral("kind"), kind },
                                         { QStringLiteral("run"), g.value(QStringLiteral("run")).toInt(run) },
                                         { QStringLiteral("rows"), entries } });
    }
    int list = 0;
    for (const auto& v : m_model->resource(QStringLiteral("runs")).value(QStringLiteral("runs")).toArray())
        if (v.toObject().value(QStringLiteral("run")).toInt() == (total ? 1 : run))
            list = v.toObject().value(QStringLiteral("counters")).toObject().value(QStringLiteral("startList")).toInt();
    const QJsonObject counters{
        { QStringLiteral("classified"), int(rows.size()) }, { QStringLiteral("list"), list },
        { QStringLiteral("dns"), counts.value(QStringLiteral("DNS")) + counts.value(QStringLiteral("NPS")) },
        { QStringLiteral("dnf"), counts.value(QStringLiteral("DNF")) }, { QStringLiteral("dsq"), counts.value(QStringLiteral("DSQ")) },
    };
    return QJsonObject{ { QStringLiteral("run"), run }, { QStringLiteral("total"), total },
                        { QStringLiteral("rows"), rows }, { QStringLiteral("unclassified"), unclassified },
                        { QStringLiteral("counters"), counters } };
}

QJsonObject ScreenData::rankingOfRun(int run, bool total) const
{
    return total ? ranking(QStringLiteral("results?upto=%1").arg(run), true, run)
                 : ranking(QStringLiteral("run/%1/results").arg(run), false, run);
}

QJsonArray ScreenData::startList(int run) const
{
    QJsonArray rows;
    for (const auto& v : m_model->resource(QStringLiteral("run/%1/startlist").arg(run)).value(QStringLiteral("entries")).toArray()) {
        const QJsonObject e = v.toObject();
        QJsonObject row = base(e.value(QStringLiteral("competitor")).toObject());
        if (row.value(QStringLiteral("bib")).toString().isEmpty())
            row.insert(QStringLiteral("bib"), str(e, "bib"));
        row.insert(QStringLiteral("order"), str(e, "order"));
        row.insert(QStringLiteral("rank"), str(e, "order")); // the start order under the rank
        const QJsonValue planned = e.value(QStringLiteral("plannedStartDayMs"));
        if (planned.isDouble())
            row.insert(QStringLiteral("plannedStart"), QTime::fromMSecsSinceStartOfDay(int(planned.toInteger())).toString(QStringLiteral("HH:mm:ss")));
        rows.append(row);
    }
    return rows;
}

// The live lists in the columns of the configuration (docs/screen-design.md 9.2):
//   next to start - rank, time, gap and the runs before this one;
//   on course     - the runs before, the running time under the current run (under the
//                   time in the 1st run), the gap of the last intermediate;
//   finished      - like the ranking (total), a status under the times.
QJsonArray ScreenData::liveRows(const QString& list) const
{
    QJsonArray rows;
    const int run = m_model->live().value(QStringLiteral("run")).toInt(currentRun());
    for (const auto& v : m_model->live().value(list).toArray()) {
        const QJsonObject e = v.toObject();
        QJsonObject row = base(e.value(QStringLiteral("competitor")).toObject());
        if (row.value(QStringLiteral("bib")).toString().isEmpty())
            row.insert(QStringLiteral("bib"), str(e, "bib"));
        const int bib = row.value(QStringLiteral("bib")).toString().toInt();
        // the runs before this one (time and rank of each)
        const QJsonObject before = m_prevByBib.value(bib);
        for (auto it = before.begin(); it != before.end(); ++it)
            if (it.key().startsWith(QLatin1String("run")) && it.key() != QLatin1String("runTime") && it.key() != QLatin1String("runRank"))
                row.insert(it.key(), it.value());
        if (e.contains(QStringLiteral("startDayMs")) && e.value(QStringLiteral("startDayMs")).isDouble())
            row.insert(QStringLiteral("startDayMs"), e.value(QStringLiteral("startDayMs")).toInteger());
        QString lastGap;
        for (const auto& iv : e.value(QStringLiteral("inter")).toArray()) {
            const QJsonObject in = iv.toObject();
            const QString key = QStringLiteral("inter%1").arg(in.value(QStringLiteral("point")).toInt());
            row.insert(key, str(in, "time"));
            row.insert(key + QStringLiteral("Gap"), str(in, "gap"));
            row.insert(key + QStringLiteral("Rank"), str(in, "rank"));
            lastGap = str(in, "gap");
        }
        const QJsonObject prev = e.value(QStringLiteral("previous")).toObject();
        if (list == QLatin1String("onStart")) {
            if (!prev.isEmpty()) {
                row.insert(QStringLiteral("rank"), str(prev, "rank"));
                row.insert(QStringLiteral("time"), str(prev, "time"));
                row.insert(QStringLiteral("gap"), str(prev, "gap"));
            }
        } else if (list == QLatin1String("onCourse")) {
            // the fields where the page runs the time: the first of them in the columns
            row.insert(QStringLiteral("live"), run > 1 ? QJsonArray{ QStringLiteral("runTime"), QStringLiteral("run%1").arg(run), QStringLiteral("time") }
                                                     : QJsonArray{ QStringLiteral("time"), QStringLiteral("run1"), QStringLiteral("runTime") });
            row.insert(QStringLiteral("gap"), lastGap);
        } else if (list == QLatin1String("finished")) {
            const QJsonObject thisRun = e.value(QStringLiteral("thisRun")).toObject();
            const QJsonObject total = e.value(QStringLiteral("total")).toObject();
            const QString state = e.value(QStringLiteral("state")).toString();
            const bool timed = state == QLatin1String("finished") && !thisRun.isEmpty();
            const QString status = timed ? QString() : statusText(state == QLatin1String("finished") ? QStringLiteral("dnf") : state);
            row.insert(QStringLiteral("finishDayMs"), e.value(QStringLiteral("finishDayMs")).toInteger(0));
            row.insert(QStringLiteral("status"), status);
            row.insert(QStringLiteral("reason"), str(e, "reason"));
            row.insert(QStringLiteral("run%1").arg(run), timed ? str(thisRun, "time") : status);
            if (timed) {
                row.insert(QStringLiteral("run%1Rank").arg(run), str(thisRun, "rank"));
                if (thisRun.contains(QStringLiteral("groupRanks"))) {
                    QJsonObject ranks;
                    groupRanksOf(thisRun.value(QStringLiteral("groupRanks")).toArray(), &ranks, nullptr);
                    row.insert(QStringLiteral("run%1GroupRanks").arg(run), ranks);
                }
            }
            if (run > 1 && timed) {
                row.insert(QStringLiteral("runTime"), str(thisRun, "time"));
                row.insert(QStringLiteral("runRank"), str(thisRun, "rank"));
            }
            if (timed && !total.isEmpty()) {
                row.insert(QStringLiteral("rank"), str(total, "rank"));
                row.insert(QStringLiteral("time"), str(total, "time"));
                row.insert(QStringLiteral("gap"), str(total, "gap"));
            } else if (!timed) {
                row.insert(QStringLiteral("time"), status);
                if (!str(e, "reason").isEmpty())
                    row.insert(QStringLiteral("gap"), QStringLiteral("(%1)").arg(str(e, "reason")));
            }
            const QJsonObject t = m_totalByBib.value(bib);
            for (auto it = t.begin(); it != t.end(); ++it) // group ranks of the total
                if (it.key().startsWith(QLatin1String("group")) && !row.contains(it.key()))
                    row.insert(it.key(), it.value());
        }
        rows.append(row);
    }
    return rows;
}

void ScreenData::rebuild()
{
    m_timer->stop();
    const QJsonObject ev = m_model->event();
    const QJsonObject general = ev.value(QStringLiteral("general")).toObject();
    const QJsonObject type = ev.value(QStringLiteral("type")).toObject();
    m_precision = type.value(QStringLiteral("precision")).toInt(2);
    const int run = currentRun();

    QJsonObject groups;
    for (const auto& v : ev.value(QStringLiteral("groups")).toArray())
        groups.insert(QString::number(v.toObject().value(QStringLiteral("index")).toInt()), v.toObject().value(QStringLiteral("label")));
    QJsonObject runNames;
    for (const auto& v : ev.value(QStringLiteral("runs")).toArray())
        runNames.insert(QString::number(v.toObject().value(QStringLiteral("run")).toInt()), v.toObject().value(QStringLiteral("name")));

    const QJsonObject total = ranking(QStringLiteral("results?upto=%1").arg(run), true, run);
    m_totalByBib.clear();
    for (const auto& v : total.value(QStringLiteral("rows")).toArray())
        m_totalByBib.insert(v.toObject().value(QStringLiteral("bib")).toString().toInt(), v.toObject());
    m_prevByBib.clear();
    if (run > 1)
        for (const auto& v : ranking(QStringLiteral("results?upto=%1").arg(run - 1), true, run - 1).value(QStringLiteral("rows")).toArray())
            m_prevByBib.insert(v.toObject().value(QStringLiteral("bib")).toString().toInt(), v.toObject());

    QJsonObject s{
        { QStringLiteral("connected"), m_model->isConnected() },
        { QStringLiteral("eventOpen"), !ev.isEmpty() },
        { QStringLiteral("event"), QJsonObject{ { QStringLiteral("title1"), general.value(QStringLiteral("title1")) },
                                               { QStringLiteral("title2"), general.value(QStringLiteral("title2")) },
                                               { QStringLiteral("location"), general.value(QStringLiteral("location")) },
                                               { QStringLiteral("date"), general.value(QStringLiteral("date")) },
                                               { QStringLiteral("discipline"), type.value(QStringLiteral("disciplineName")) },
                                               { QStringLiteral("fis"), !type.value(QStringLiteral("fisCategory")).toString().isEmpty() },
                                               { QStringLiteral("runCount"), type.value(QStringLiteral("runCount")).toInt(1) },
                                               { QStringLiteral("interCount"), type.value(QStringLiteral("interCount")).toInt() },
                                               { QStringLiteral("precision"), m_precision },
                                               { QStringLiteral("runs"), runNames },
                                               { QStringLiteral("groups"), groups } } },
        { QStringLiteral("timing"), QJsonObject{ { QStringLiteral("active"), m_model->timingActive() }, { QStringLiteral("run"), m_model->timedRun() } } },
        { QStringLiteral("phase"), phase() },
        { QStringLiteral("run"), run },
        { QStringLiteral("rankings"), QJsonObject{ { QStringLiteral("total"), total },
                                                  { QStringLiteral("run"), ranking(QStringLiteral("run/%1/results").arg(run), false, run) } } },
        { QStringLiteral("startList"), QJsonObject{ { QStringLiteral("run"), startListRun() }, { QStringLiteral("rows"), startList(startListRun()) } } },
        { QStringLiteral("onStart"), liveRows(QStringLiteral("onStart")) },
        { QStringLiteral("onCourse"), liveRows(QStringLiteral("onCourse")) },
        { QStringLiteral("finished"), liveRows(QStringLiteral("finished")) },
    };
    if (s == m_state)
        return;
    m_state = s;
    Q_EMIT stateChanged();
}
