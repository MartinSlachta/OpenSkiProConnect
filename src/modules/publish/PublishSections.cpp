#include "PublishSections.h"

#include "ScreenData.h"
#include "core/RaceModel.h"

#include <QLocale>
#include <QSet>

namespace PublishSections {

namespace {

const int kOnStart = 5;   // next at the start
const int kFinished = 10; // last at the finish

using Fields = QList<QPair<QString, QString>>;

QString str(const QJsonObject& o, const QString& key)
{
    const QJsonValue v = o.value(key);
    return v.isDouble() ? QString::number(v.toInteger()) : v.toString();
}

bool isStatus(const QString& s)
{
    static const QSet<QString> l = { QStringLiteral("DNS"), QStringLiteral("DNF"), QStringLiteral("DSQ"), QStringLiteral("NPS"),
                                     QStringLiteral("DNQ") };
    return l.contains(s);
}

// "+0.45"; the API sends the gap without the sign
QString gap(const QString& g)
{
    return !g.isEmpty() && g.at(0).isDigit() ? QLatin1Char('+') + g : g;
}

QString nameOf(const QJsonObject& r)
{
    const QString name = str(r, QStringLiteral("name"));
    return name.isEmpty() ? (str(r, QStringLiteral("lastName")) + QLatin1Char(' ') + str(r, QStringLiteral("firstName"))).trimmed() : name;
}

QJsonArray rows(const QJsonArray& in, const Fields& fields, List list, int run, bool best, int max = -1)
{
    QJsonArray out;
    for (const auto& v : in) {
        if (max >= 0 && out.size() >= max)
            break;
        QJsonObject r = row(v.toObject(), fields, list, run, best);
        if (list == List::OnStart && !r.contains(QStringLiteral("order"))) // next first
            r.insert(QStringLiteral("order"), QString::number(out.size() + 1));
        out.append(r);
    }
    return out;
}

QJsonObject ranking(const QJsonObject& r, int run, bool total, const Fields& fields, bool best)
{
    const List list = total ? List::Total : List::Run;
    QJsonArray unclassified;
    for (const auto& gv : r.value(QStringLiteral("unclassified")).toArray()) {
        const QJsonObject g = gv.toObject();
        // not raced yet (the start list of the run): not a status, they are on start (as Screen)
        if (g.value(QStringLiteral("kind")).toString() == QLatin1String("notProcessed"))
            continue;
        unclassified.append(QJsonObject{ { QStringLiteral("kind"), g.value(QStringLiteral("kind")) },
                                         { QStringLiteral("run"), g.value(QStringLiteral("run")) },
                                         { QStringLiteral("rows"), rows(g.value(QStringLiteral("rows")).toArray(), fields, list, run, best) } });
    }
    return QJsonObject{ { QStringLiteral("run"), run },
                        { QStringLiteral("total"), total },
                        { QStringLiteral("rows"), rows(r.value(QStringLiteral("rows")).toArray(), fields, list, run, best) },
                        { QStringLiteral("unclassified"), unclassified },
                        { QStringLiteral("counters"), r.value(QStringLiteral("counters")) } };
}


} // namespace

Fields fields(const RaceModel* model)
{
    const Fields f = model->publicFields();
    if (!f.isEmpty())
        return f;
    return { { QStringLiteral("name"), QStringLiteral("Name") }, { QStringLiteral("nation"), QStringLiteral("Nation") },
             { QStringLiteral("club"), QStringLiteral("Club") } };
}

QJsonObject row(const QJsonObject& r, const Fields& fields, List list, int run, bool bestOfRuns)
{
    QJsonObject out{ { QStringLiteral("bib"), str(r, QStringLiteral("bib")) } };
    const QString status = str(r, QStringLiteral("status"));
    const bool ranked = list == List::Total || list == List::Run || list == List::Finished;

    if (list == List::StartList || list == List::OnStart) {
        const QString order = str(r, QStringLiteral("order"));
        if (!order.isEmpty())
            out.insert(QStringLiteral("order"), order);
    }
    // the start list puts the start order under the rank (Screen): not here
    const QString rank = list == List::StartList || list == List::OnCourse || !status.isEmpty() ? QString() : str(r, QStringLiteral("rank"));
    if (!rank.isEmpty())
        out.insert(QStringLiteral("rank"), rank);

    QJsonObject f;
    for (const auto& field : fields) {
        const QString v = field.first == QLatin1String("name") ? nameOf(r) : str(r, field.first);
        if (!v.isEmpty())
            f.insert(field.first, v);
    }
    out.insert(QStringLiteral("fields"), f);

    // the single runs: up to the run in a total ranking and at the finish, before it at
    // the start and on course; a ranking of one run and the start list have none
    int lastRun = 0;
    if (list == List::Total || list == List::Finished)
        lastRun = run;
    else if (list == List::OnStart || list == List::OnCourse)
        lastRun = run - 1;
    const QString time = str(r, QStringLiteral("time"));
    QJsonObject runs;
    bool bestDone = false;
    for (int k = 1; k <= lastRun; ++k) {
        const QString t = str(r, QStringLiteral("run%1").arg(k));
        if (t.isEmpty())
            continue;
        QJsonObject one{ { QStringLiteral("time"), t } };
        if (isStatus(t)) {
            one.insert(QStringLiteral("status"), t);
        } else {
            const QString rk = str(r, QStringLiteral("run%1Rank").arg(k));
            if (!rk.isEmpty())
                one.insert(QStringLiteral("rank"), rk);
            if (bestOfRuns && ranked && status.isEmpty() && !bestDone && t == time) {
                one.insert(QStringLiteral("best"), true);
                bestDone = true;
            }
        }
        runs.insert(QString::number(k), one);
    }
    if (!runs.isEmpty())
        out.insert(QStringLiteral("runs"), runs);

    if (list != List::StartList && list != List::OnCourse) {
        if (!time.isEmpty())
            out.insert(QStringLiteral("time"), time);
        const QString g = status.isEmpty() ? gap(str(r, QStringLiteral("gap"))) : QString(); // Screen puts "(gate)" there
        if (!g.isEmpty())
            out.insert(QStringLiteral("gap"), g);
    }

    if (ranked && status.isEmpty()) {
        const QJsonObject ranks = r.value(QStringLiteral("groupRanks")).toObject();
        const QJsonObject gaps = r.value(QStringLiteral("groupGaps")).toObject();
        QJsonObject groups;
        for (auto it = ranks.begin(); it != ranks.end(); ++it) {
            QJsonObject g{ { QStringLiteral("rank"), it.value().toString() } };
            const QString gg = gap(gaps.value(it.key()).toString());
            if (!gg.isEmpty())
                g.insert(QStringLiteral("gap"), gg);
            QJsonObject rr; // the rank in the group of each run of the row
            for (auto k = runs.begin(); k != runs.end(); ++k) {
                const QString rk = r.value(QStringLiteral("run%1GroupRanks").arg(k.key())).toObject().value(it.key()).toString();
                if (!rk.isEmpty() && !k.value().toObject().contains(QStringLiteral("status")))
                    rr.insert(k.key(), rk);
            }
            if (!rr.isEmpty())
                g.insert(QStringLiteral("runs"), rr);
            groups.insert(it.key(), g);
        }
        if (!groups.isEmpty())
            out.insert(QStringLiteral("groups"), groups);
    }

    if (!status.isEmpty())
        out.insert(QStringLiteral("status"), status);
    const QString reason = str(r, QStringLiteral("reason"));
    if (!reason.isEmpty())
        out.insert(QStringLiteral("reason"), reason);

    if (list == List::OnCourse) {
        if (r.contains(QStringLiteral("startDayMs")))
            out.insert(QStringLiteral("startDayMs"), r.value(QStringLiteral("startDayMs")).toInteger());
        for (int n = 3; n >= 1; --n) { // the last intermediate passed
            const QString t = str(r, QStringLiteral("inter%1").arg(n));
            if (t.isEmpty())
                continue;
            QJsonObject in{ { QStringLiteral("n"), n }, { QStringLiteral("time"), t } };
            const QString rk = str(r, QStringLiteral("inter%1Rank").arg(n));
            if (!rk.isEmpty())
                in.insert(QStringLiteral("rank"), rk);
            const QString g = gap(str(r, QStringLiteral("inter%1Gap").arg(n)));
            if (!g.isEmpty())
                in.insert(QStringLiteral("gap"), g);
            out.insert(QStringLiteral("inter"), in);
            break;
        }
    }
    if (list == List::Finished && r.value(QStringLiteral("finishDayMs")).toInteger() > 0)
        out.insert(QStringLiteral("finishDayMs"), r.value(QStringLiteral("finishDayMs")).toInteger());
    if ((list == List::StartList || list == List::OnStart) && !str(r, QStringLiteral("plannedStart")).isEmpty())
        out.insert(QStringLiteral("plannedStart"), str(r, QStringLiteral("plannedStart")));
    return out;
}

QMap<QString, QJsonObject> build(const RaceModel* model, const ScreenData* data)
{
    QMap<QString, QJsonObject> s;
    const QJsonObject ev = model->event();
    if (ev.isEmpty())
        return s;
    const QJsonObject state = data->state();
    const QJsonObject general = ev.value(QStringLiteral("general")).toObject();
    const QJsonObject type = ev.value(QStringLiteral("type")).toObject();
    const QJsonObject stEvent = state.value(QStringLiteral("event")).toObject();
    const int runCount = qMax(1, type.value(QStringLiteral("runCount")).toInt(1));
    const QString finalRanking = type.value(QStringLiteral("finalRanking")).toString(QStringLiteral("totalOfRuns"));
    const bool best = finalRanking == QLatin1String("bestOfRuns");
    const Fields flds = fields(model);
    QJsonArray fieldList;
    for (const auto& f : flds)
        fieldList.append(QJsonObject{ { QStringLiteral("key"), f.first }, { QStringLiteral("label"), f.second } });

    s.insert(QStringLiteral("event"),
             QJsonObject{ { QStringLiteral("title1"), general.value(QStringLiteral("title1")) },
                          { QStringLiteral("title2"), general.value(QStringLiteral("title2")) },
                          { QStringLiteral("location"), general.value(QStringLiteral("location")) },
                          { QStringLiteral("nation"), general.value(QStringLiteral("nation")) },
                          { QStringLiteral("date"), general.value(QStringLiteral("date")) },
                          { QStringLiteral("organisation"), general.value(QStringLiteral("organisation")) },
                          { QStringLiteral("timingBy"), general.value(QStringLiteral("timingBy")) },
                          { QStringLiteral("discipline"), type.value(QStringLiteral("disciplineName")) },
                          { QStringLiteral("fis"), type.value(QStringLiteral("regulation")).toString() == QLatin1String("FIS") },
                          { QStringLiteral("codex"), general.value(QStringLiteral("fis")).toObject().value(QStringLiteral("codex")) },
                          { QStringLiteral("runCount"), runCount },
                          { QStringLiteral("interCount"), type.value(QStringLiteral("interCount")).toInt() },
                          { QStringLiteral("precision"), type.value(QStringLiteral("precision")).toInt(2) },
                          { QStringLiteral("finalRanking"), finalRanking },
                          { QStringLiteral("runs"), stEvent.value(QStringLiteral("runs")) },
                          { QStringLiteral("groups"), stEvent.value(QStringLiteral("groups")) },
                          { QStringLiteral("fields"), fieldList },
                          { QStringLiteral("lang"), QLocale().name().left(2) } });
    s.insert(QStringLiteral("info"), QJsonObject{ { QStringLiteral("courses"), ev.value(QStringLiteral("courses")) },
                                                  { QStringLiteral("jury"), ev.value(QStringLiteral("jury")) },
                                                  { QStringLiteral("conditions"), ev.value(QStringLiteral("conditions")) } });
    s.insert(QStringLiteral("status"),
             QJsonObject{ { QStringLiteral("phase"), state.value(QStringLiteral("phase")) },
                          { QStringLiteral("timing"), state.value(QStringLiteral("timing")) },
                          { QStringLiteral("run"), state.value(QStringLiteral("run")) },
                          { QStringLiteral("startListRun"), state.value(QStringLiteral("startList")).toObject().value(QStringLiteral("run")) } });

    // live lists only while a run is timed (empty lists close the Live tab of the pages)
    const bool timing = model->timingActive();
    const int run = model->timedRun();
    s.insert(QStringLiteral("live"),
             QJsonObject{ { QStringLiteral("run"), run },
                          { QStringLiteral("onStart"), timing ? rows(state.value(QStringLiteral("onStart")).toArray(), flds, List::OnStart, run, best, kOnStart) : QJsonArray() },
                          { QStringLiteral("onCourse"), timing ? rows(state.value(QStringLiteral("onCourse")).toArray(), flds, List::OnCourse, run, best) : QJsonArray() },
                          { QStringLiteral("finished"), timing ? rows(state.value(QStringLiteral("finished")).toArray(), flds, List::Finished, run, best, kFinished) : QJsonArray() } });

    for (int r = 1; r <= runCount; ++r) {
        const QJsonArray sl = data->startListOfRun(r);
        if (!sl.isEmpty())
            s.insert(QStringLiteral("startlist.%1").arg(r),
                     QJsonObject{ { QStringLiteral("run"), r }, { QStringLiteral("rows"), rows(sl, flds, List::StartList, r, best) } });
        const QJsonObject total = ranking(data->rankingOfRun(r, true), r, true, flds, best);
        if (raced(total, r))
            s.insert(QStringLiteral("results.total.%1").arg(r), total);
        if (runCount > 1) { // a 1-run race: the total is the run
            const QJsonObject alone = ranking(data->rankingOfRun(r, false), r, false, flds, best);
            if (raced(alone, r))
                s.insert(QStringLiteral("results.run.%1").arg(r), alone);
        }
    }
    return s;
}

bool raced(const QJsonObject& ranking, int run)
{
    if (!ranking.value(QStringLiteral("rows")).toArray().isEmpty())
        return true;
    for (const auto& gv : ranking.value(QStringLiteral("unclassified")).toArray()) {
        const QJsonObject g = gv.toObject();
        if (g.value(QStringLiteral("run")).toVariant().toInt() == run && g.value(QStringLiteral("kind")).toString() != QLatin1String("DNQ")
            && !g.value(QStringLiteral("rows")).toArray().isEmpty())
            return true;
    }
    return false;
}

} // namespace PublishSections
