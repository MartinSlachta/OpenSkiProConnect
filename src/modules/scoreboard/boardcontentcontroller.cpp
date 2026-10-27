#include "boardcontentcontroller.h"

#include "core/RaceModel.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonValue>
#include <QTimer>

namespace {

CompetitorRef competitorRefFromJson(const QJsonObject &obj)
{
    CompetitorRef ref;
    ref.bib = obj.value(QStringLiteral("bib")).toInt();
    ref.firstName = obj.value(QStringLiteral("firstName")).toString();
    ref.lastName = obj.value(QStringLiteral("lastName")).toString();
    ref.category = obj.value(QStringLiteral("category")).toString();
    ref.nation = obj.value(QStringLiteral("nation")).toString();
    ref.club = obj.value(QStringLiteral("club")).toString();
    ref.sex = obj.value(QStringLiteral("gender")).toString();
    ref.year = obj.value(QStringLiteral("year")).toInt();
    return ref;
}

QString rankText(const QJsonObject &rankedTime)
{
    const QJsonValue r = rankedTime.value(QStringLiteral("rank"));
    return r.isDouble() ? QString::number(r.toInt()) : QString();
}

} // namespace

BoardContentController::BoardContentController(RaceModel *model, QObject *parent)
    : QObject(parent)
    , m_model(model)
    , m_holdTimer(new QTimer(this))
{
    m_holdTimer->setSingleShot(true);
    connect(m_holdTimer, &QTimer::timeout, this, &BoardContentController::advanceQueue);

    connect(m_model, &RaceModel::liveChanged, this, &BoardContentController::rebuild);
    connect(m_model, &RaceModel::competitorEvent, this, &BoardContentController::onCompetitorEvent);
    connect(m_model, &RaceModel::resourceChanged, this, [this](const QString &name) {
        // grouped rankings arrive after the competitor event of the same change
        if (name.contains(QLatin1String("results")) || name == QLatin1String("competitors")) {
            rebuild();
            refreshHeldSubject();
        }
        if (name == QLatin1String("event"))
            requireRankings();
    });
    connect(m_model, &RaceModel::timingChanged, this, [this](bool active, int) {
        requireRankings();
        onTimingModeChanged(active);
        rebuild();
    });
    connect(m_model, &RaceModel::connectedChanged, this, [this](bool connected) {
        if (!connected)
            onTimingModeChanged(false);
    });
    requireRankings();
}

void BoardContentController::setRankField(const QString &field)
{
    m_rankField = field == QLatin1String("overall") ? field : QStringLiteral("group");
    requireRankings();
    rebuild();
    refreshHeldSubject();
}

void BoardContentController::requireRankings()
{
    const int run = m_model->timedRun();
    if (run <= 0 || m_rankField != QLatin1String("group"))
        return;
    QStringList names;
    const int groups = int(m_model->event().value(QStringLiteral("groups")).toArray().size());
    for (int g = 0; g < groups; ++g) {
        names << RaceModel::withGroup(QStringLiteral("run/%1/results").arg(run), g)
              << RaceModel::withGroup(QStringLiteral("results?upto=%1").arg(run), g);
        if (run > 1)
            names << RaceModel::withGroup(QStringLiteral("results?upto=%1").arg(run - 1), g);
    }
    m_model->require(names);
}

void BoardContentController::rebuild()
{
    const QJsonObject live = m_model->live();
    const int run = live.value(QStringLiteral("run")).toInt();
    const bool group = m_rankField == QLatin1String("group");
    const bool sumOfRuns = m_model->event().value(QStringLiteral("type")).toObject().value(QStringLiteral("finalRanking")).toString()
        != QLatin1String("bestOfRuns");

    QVector<CompetitorRef> onStart;
    for (const QJsonValue &v : live.value(QStringLiteral("onStart")).toArray())
        onStart.append(competitorRefFromJson(v.toObject().value(QStringLiteral("competitor")).toObject()));
    m_onStartCompetitors = onStart;
    emit onStartChanged(onStart);

    QVector<OnCourseSubject> onCourse;
    for (const QJsonValue &v : live.value(QStringLiteral("onCourse")).toArray()) {
        const QJsonObject obj = v.toObject();
        OnCourseSubject subject;
        subject.competitor = competitorRefFromJson(obj.value(QStringLiteral("competitor")).toObject());
        subject.startDtMs = obj.value(QStringLiteral("startDayMs")).toInteger();
        const QJsonObject previous = obj.value(QStringLiteral("previous")).toObject();
        if (!previous.isEmpty()) {
            RunInfo prior;
            prior.runId = run - 1;
            prior.status = QStringLiteral("QLF");
            prior.time = previous.value(QStringLiteral("time")).toString();
            prior.rank = group ? m_model->rankIn(QStringLiteral("results?upto=%1").arg(run - 1), subject.competitor.bib, true)
                               : rankText(previous);
            subject.priorRuns.append(prior);
            // a best-of-runs race has no live "total" concept
            if (sumOfRuns)
                subject.priorRunsTotalMs = previous.value(QStringLiteral("timeMs")).toInteger();
        }
        onCourse.append(subject);
    }
    m_onCourseSubjects = onCourse;
    emit onCourseChanged(onCourse);

    QVector<FinishSubject> finished;
    for (const QJsonValue &v : live.value(QStringLiteral("finished")).toArray()) {
        QJsonObject obj = v.toObject();
        obj.insert(QStringLiteral("run"), run);
        finished.append(finishSubjectFromJson(obj));
    }
    m_finishListSubjects = finished;
    emit finishListChanged(finished);
}

void BoardContentController::onCompetitorEvent(const QJsonObject &data)
{
    const int run = data.value(QStringLiteral("run")).toInt();
    if (!m_model->timingActive() || run != m_model->timedRun())
        return; // only the run on the boards
    const int bib = data.value(QStringLiteral("bib")).toInt();
    const QString change = data.value(QStringLiteral("change")).toString();
    const QString state = data.value(QStringLiteral("state")).toString();

    if (change == QLatin1String("finished") || change == QLatin1String("status")) {
        m_finishQueue.append({run, bib, data.value(QStringLiteral("status")).toString()});
        if (!m_holdTimer->isActive())
            advanceQueue();
        return;
    }
    if (change == QLatin1String("reverted") && (state == QLatin1String("onCourse") || state == QLatin1String("notStarted"))) {
        // the timing walked back a just-reported finish / status of this bib
        for (int i = m_finishQueue.size() - 1; i >= 0; --i)
            if (m_finishQueue.at(i).bib == bib)
                m_finishQueue.removeAt(i);
        if (m_currentFinishSubject.valid && m_currentFinishSubject.competitor.bib == bib)
            advanceQueue();
        return;
    }
    if (m_currentFinishSubject.valid && m_currentFinishSubject.competitor.bib == bib)
        refreshHeldSubject(); // corrected
}

void BoardContentController::onTimingModeChanged(bool timing)
{
    if (timing)
        return;

    m_finishQueue.clear();
    m_holdTimer->stop();
    setFinishSubject(FinishSubject{});
}

void BoardContentController::reset()
{
    m_finishQueue.clear();
    m_holdTimer->stop();
    setFinishSubject(FinishSubject{});
    rebuild();
}

void BoardContentController::advanceQueue()
{
    if (!m_finishQueue.isEmpty()) {
        const PendingFinish item = m_finishQueue.takeFirst();
        // the complete result travels with the competitor event; the latest one
        // known is shown (a correction while queued included)
        QJsonObject data = m_model->competitorState(item.run, item.bib);
        m_currentRun = item.run;
        setFinishSubject(finishSubjectFromJson(data));

        const int holdMs = item.status == QStringLiteral("QLF") ? m_finishHoldMs : m_otherHoldMs;
        m_holdTimer->start(holdMs);
        return;
    }

    m_holdTimer->stop();
    setFinishSubject(FinishSubject{});
}

void BoardContentController::refreshHeldSubject()
{
    if (!m_currentFinishSubject.valid)
        return;
    const QJsonObject data = m_model->competitorState(m_currentRun, m_currentFinishSubject.competitor.bib);
    if (!data.isEmpty())
        setFinishSubject(finishSubjectFromJson(data));
}

BoardContentController::FinishSubject BoardContentController::finishSubjectFromJson(const QJsonObject &obj) const
{
    const int run = obj.value(QStringLiteral("run")).toInt();
    const bool group = m_rankField == QLatin1String("group");
    const QJsonObject thisRun = obj.value(QStringLiteral("thisRun")).toObject();
    const QJsonObject total = obj.value(QStringLiteral("total")).toObject();

    FinishSubject subject;
    subject.valid = true;
    subject.competitor = competitorRefFromJson(obj.value(QStringLiteral("competitor")).toObject());
    subject.overallStatus = obj.value(QStringLiteral("status")).toString();
    subject.totalTime = total.value(QStringLiteral("time")).toString();
    subject.totalRank = group ? m_model->rankIn(QStringLiteral("results?upto=%1").arg(run), subject.competitor.bib, true)
                              : rankText(total);

    RunInfo info;
    info.runId = run;
    info.status = subject.overallStatus;
    info.time = thisRun.value(QStringLiteral("time")).toString();
    info.rank = group ? m_model->rankIn(QStringLiteral("run/%1/results").arg(run), subject.competitor.bib, true)
                      : rankText(thisRun);
    subject.runs.append(info);
    // subjectSinceEpochMs is left at 0 - only the held subject has a
    // "became current" moment, and setFinishSubject() stamps that one.
    return subject;
}

void BoardContentController::setFinishSubject(const FinishSubject &subject)
{
    FinishSubject next = subject;
    const bool sameSubject =
        next.valid && m_currentFinishSubject.valid && next.competitor.bib == m_currentFinishSubject.competitor.bib;
    next.subjectSinceEpochMs =
        sameSubject ? m_currentFinishSubject.subjectSinceEpochMs : QDateTime::currentMSecsSinceEpoch();

    m_currentFinishSubject = next;
    emit finishSubjectChanged(next);
}
