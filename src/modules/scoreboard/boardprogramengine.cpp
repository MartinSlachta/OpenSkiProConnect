#include "boardprogramengine.h"

#include <QDate>
#include <QDateTime>
#include <QTime>
#include <QTimer>

namespace {

// M:SS.t (or SS.t under a minute) - a spectator-facing running clock, not
// an official time. Whole seconds at 1fps, tenths at 10fps - matches the
// display's own update rate rather than implying more precision than it
// can show. Minutes and their separator are dropped entirely below one
// minute, and the leading zero on seconds is dropped along with them
// (e.g. "5.5", not "0:05.5"). The tenths digit - or one blank space in its
// place at 1fps - plus one further trailing space are always appended, so
// mm/ss stay in the same columns whether or not a tenths digit is present.
QString formatElapsed(qint64 elapsedMs, bool tenths)
{
    if (elapsedMs < 0)
        elapsedMs = 0;
    const qint64 deci = elapsedMs / 100;
    const qint64 mins = deci / 600;
    const qint64 secs = (deci / 10) % 60;
    const qint64 tenth = deci % 10;

    QString result = mins > 0
        ? QStringLiteral("%1:%2").arg(mins).arg(secs, 2, 10, QLatin1Char('0'))
        : QString::number(secs);
    result += QLatin1Char('.');
    result += tenths ? QString::number(tenth) : QStringLiteral(" ");
    result += QLatin1Char(' ');
    return result;
}

// Left/right-aligns into exactly `width` chars, truncating from the side
// that would otherwise overflow (left truncation keeps the end - e.g. a
// time's seconds/decimals - since that matters more than the start).
QString fitField(const QString &value, int width, bool leftAlign)
{
    QString v = value;
    if (v.size() > width)
        v = leftAlign ? v.left(width) : v.right(width);
    return leftAlign ? v.leftJustified(width, QLatin1Char(' ')) : v.rightJustified(width, QLatin1Char(' '));
}

} // namespace

BoardProgramEngine::BoardProgramEngine(QObject *parent)
    : QObject(parent)
    , m_tickTimer(new QTimer(this))
{
    connect(m_tickTimer, &QTimer::timeout, this, &BoardProgramEngine::tick);
    m_tickTimer->start(1000 / m_updateRateFps);
}

// Everything below only updates state - publishing is the tick's job alone,
// see tick(). None of these call recompute() themselves.

void BoardProgramEngine::setTimingRules(const QVector<BoardFieldRule> &rules)
{
    m_timingRules = rules;
}

void BoardProgramEngine::setOutsideTimingRules(const QVector<BoardFieldRule> &rules)
{
    m_outsideTimingRules = rules;
}

void BoardProgramEngine::setUpdateRateFps(int fps)
{
    m_updateRateFps = fps > 0 ? fps : 10;
    m_tickTimer->setInterval(1000 / m_updateRateFps);
}

void BoardProgramEngine::onOnStartChanged(const QVector<CompetitorRef> &competitors)
{
    m_onStartCompetitors = competitors;
}

void BoardProgramEngine::onOnCourseChanged(const QVector<BoardContentController::OnCourseSubject> &subjects)
{
    m_onCourseSubjects = subjects;
}

void BoardProgramEngine::onFinishListChanged(const QVector<BoardContentController::FinishSubject> &subjects)
{
    m_finishListSubjects = subjects;
}

void BoardProgramEngine::onFinishSubjectChanged(const BoardContentController::FinishSubject &subject)
{
    m_finishSubject = subject;
}

void BoardProgramEngine::onClockOffsetChanged(qint64 offsetMs, bool /*synced*/)
{
    m_clockOffsetMs = offsetMs;
}

void BoardProgramEngine::onTimingModeChanged(bool timing)
{
    m_timingActive = timing;
}

// The single point where anything reaches a display. State updates used to
// publish immediately as well, which meant a half-built state could be sent
// and then corrected microseconds later: BoardContentController::advanceQueue()
// sets a provisional finish subject (no totalTime/rank yet - they can't be
// derived from the WS event alone) and enriches it from /api/onfinish on the
// very next line, so a board would get one blank frame followed instantly by
// the real one. Publishing only here collapses any such intermediate state
// before it can leave the process, and makes the code do what the display's
// own Update rate already promised: "how often data is actually sent to this
// display" (docs/board-display-system.md), rather than "whenever a signal
// happens to land".
void BoardProgramEngine::tick()
{
    recompute();
}

bool BoardProgramEngine::isRuleActive(const BoardFieldRule &rule, qint64 elapsedSinceFinishMs) const
{
    if (rule.trigger == BoardFieldRule::Trigger::Continuous)
        return true;

    if (!m_finishSubject.valid)
        return false;

    // The run that just triggered this finish event is the last entry in
    // runs[] (assumed ascending by runId, matching /api/onfinish's shape).
    // A subject built straight from the WS event's "result" takes its runs
    // from the DB rather than from the event, so - unlike the provisional
    // path, which always synthesised exactly one run - it can arrive with
    // none. Falling back to the overall status keeps an event overlay from
    // silently never firing in that case.
    const QString lastRunStatus =
        m_finishSubject.runs.isEmpty() ? m_finishSubject.overallStatus : m_finishSubject.runs.last().status;
    QString wantedStatus;
    switch (rule.trigger) {
    case BoardFieldRule::Trigger::AfterFinish:
        wantedStatus = QStringLiteral("QLF");
        break;
    case BoardFieldRule::Trigger::AfterDns:
        wantedStatus = QStringLiteral("DNS");
        break;
    case BoardFieldRule::Trigger::AfterDnf:
        wantedStatus = QStringLiteral("DNF");
        break;
    case BoardFieldRule::Trigger::Continuous:
        break; // unreachable - handled above
    }
    if (lastRunStatus != wantedStatus)
        return false;

    const qint64 startMs = static_cast<qint64>(rule.startOffsetSec) * 1000;
    const qint64 endMs = static_cast<qint64>(rule.startOffsetSec + rule.durationSec) * 1000;
    return elapsedSinceFinishMs >= startMs && elapsedSinceFinishMs < endMs;
}

const BoardContentController::OnCourseSubject *
BoardProgramEngine::onCourseSubjectFor(const BoardFieldRule &rule) const
{
    if (rule.scope != BoardFieldRule::Scope::OnCourse)
        return nullptr;
    if (rule.scopeIndex < 0 || rule.scopeIndex >= m_onCourseSubjects.size())
        return nullptr;
    return &m_onCourseSubjects.at(rule.scopeIndex);
}

const BoardContentController::FinishSubject *
BoardProgramEngine::finishSubjectFor(const BoardFieldRule &rule) const
{
    if (rule.scope == BoardFieldRule::Scope::LastFinish)
        return m_finishSubject.valid ? &m_finishSubject : nullptr;
    if (rule.scope != BoardFieldRule::Scope::OnFinish)
        return nullptr;
    if (rule.scopeIndex < 0 || rule.scopeIndex >= m_finishListSubjects.size())
        return nullptr;
    return &m_finishListSubjects.at(rule.scopeIndex);
}

QString BoardProgramEngine::resolveFieldText(const BoardFieldRule &rule) const
{
    // Competitor metadata is the one thing every scope carries, so this only
    // needs to know which subject the scope points at. It's also all that
    // Scope::OnStart has, which is why that scope appears here and nowhere
    // else - /api/onstart has no time, rank or status to resolve.
    const CompetitorRef *competitor = nullptr;
    if (rule.scope == BoardFieldRule::Scope::OnStart) {
        if (rule.scopeIndex < 0 || rule.scopeIndex >= m_onStartCompetitors.size())
            return QString();
        competitor = &m_onStartCompetitors.at(rule.scopeIndex);
    } else if (const auto *onCourse = onCourseSubjectFor(rule)) {
        competitor = &onCourse->competitor;
    } else if (const auto *finish = finishSubjectFor(rule)) {
        competitor = &finish->competitor;
    } else {
        return QString();
    }

    switch (rule.fieldName) {
    case BoardFieldRule::FieldName::Bib:
        return competitor->bib > 0 ? QString::number(competitor->bib) : QString();
    case BoardFieldRule::FieldName::Category:
        return competitor->category;
    case BoardFieldRule::FieldName::FirstName:
        return competitor->firstName;
    case BoardFieldRule::FieldName::LastName:
        return competitor->lastName;
    case BoardFieldRule::FieldName::Nation:
        return competitor->nation;
    case BoardFieldRule::FieldName::Sex:
        return competitor->sex;
    case BoardFieldRule::FieldName::Year:
        return competitor->year > 0 ? QString::number(competitor->year) : QString();
    case BoardFieldRule::FieldName::Club:
        return competitor->club;
    }
    return QString();
}

QString BoardProgramEngine::resolveRunningTimeText(const BoardFieldRule &rule) const
{
    // On course the time is still running, so it's computed live against the
    // clock; in either finish scope it's simply the recorded one. Same
    // content type either way - the scope is what decides which it means.
    if (const auto *subject = onCourseSubjectFor(rule)) {
        const qint64 nowServerMs = QTime::currentTime().msecsSinceStartOfDay() + m_clockOffsetMs;
        const qint64 elapsedThisRun = nowServerMs - subject->startDtMs;
        const qint64 elapsedMs = rule.runSelector == BoardFieldRule::RunSelector::Total
            ? subject->priorRunsTotalMs + elapsedThisRun
            : elapsedThisRun;
        return formatElapsed(elapsedMs, m_updateRateFps >= 10);
    }

    const auto *finish = finishSubjectFor(rule);
    if (!finish)
        return QString();
    if (rule.runSelector == BoardFieldRule::RunSelector::Total)
        return finish->totalTime;
    return finish->runs.isEmpty() ? QString() : finish->runs.last().time;
}

QString BoardProgramEngine::resolveRankText(const BoardFieldRule &rule) const
{
    // On course, the only rank that exists is one from a completed earlier
    // run - /api/oncourse carries no overall rank and the in-progress run
    // obviously has none, so both run selectors resolve to the last
    // completed run and a first-heat racer renders blank.
    if (const auto *subject = onCourseSubjectFor(rule))
        return subject->priorRuns.isEmpty() ? QString() : subject->priorRuns.last().rank;

    const auto *finish = finishSubjectFor(rule);
    if (!finish)
        return QString();
    if (rule.runSelector == BoardFieldRule::RunSelector::Total)
        return finish->totalRank;
    return finish->runs.isEmpty() ? QString() : finish->runs.last().rank;
}

QString BoardProgramEngine::resolveStatusText(const BoardFieldRule &rule) const
{
    // Same reasoning as rank above: on course, the last completed run's
    // status is the only one there is.
    if (const auto *subject = onCourseSubjectFor(rule))
        return subject->priorRuns.isEmpty() ? QString() : subject->priorRuns.last().status;

    const auto *finish = finishSubjectFor(rule);
    if (!finish)
        return QString();
    if (rule.runSelector == BoardFieldRule::RunSelector::Total)
        return finish->overallStatus;
    return finish->runs.isEmpty() ? QString() : finish->runs.last().status;
}

QString BoardProgramEngine::resolveText(const BoardFieldRule &rule) const
{
    QString text;
    switch (rule.contentType) {
    case BoardFieldRule::ContentType::Field:
        text = resolveFieldText(rule);
        break;
    case BoardFieldRule::ContentType::RunningTime:
        text = resolveRunningTimeText(rule);
        break;
    case BoardFieldRule::ContentType::Rank:
        text = resolveRankText(rule);
        break;
    case BoardFieldRule::ContentType::StatusText:
        text = resolveStatusText(rule);
        break;
    case BoardFieldRule::ContentType::Date:
        text = QDate::currentDate().toString(rule.dateTimeFormat);
        break;
    case BoardFieldRule::ContentType::DayTime: {
        const qint64 nowMs = QTime::currentTime().msecsSinceStartOfDay() + m_clockOffsetMs;
        const qint64 msSinceMidnight = ((nowMs % 86400000) + 86400000) % 86400000;
        text = QTime(0, 0).addMSecs(static_cast<int>(msSinceMidnight)).toString(rule.dateTimeFormat);
        break;
    }
    case BoardFieldRule::ContentType::Text:
        text = rule.staticText;
        break;
    }

    if (text.isEmpty())
        return text;

    if (rule.contentStartChar > 0)
        text = rule.contentStartChar < text.size() ? text.mid(rule.contentStartChar) : QString();

    if (!text.isEmpty())
        text = rule.prefix + text + rule.suffix;

    return text;
}

QString BoardProgramEngine::scrollText(const BoardFieldRule &rule, const QString &content, int width)
{
    if (content.size() <= width)
        return fitField(content, width, true);

    // Gentle default: ~1s pause showing the start, scroll left one
    // character every ~500ms (2 chars/sec), ~1s pause at the end, then
    // loop - boards this targets are often only 8 characters wide.
    constexpr int kStepMs = 500;
    constexpr int kStartPauseSteps = 2;
    constexpr int kEndPauseSteps = 2;

    const QString key = QStringLiteral("%1:%2:%3").arg(rule.line).arg(rule.startChar).arg(rule.endChar);
    const int scrollableSteps = content.size() - width + 1;
    const int cycleSteps = kStartPauseSteps + scrollableSteps + kEndPauseSteps;

    qint64 &cycleStartMs = m_scrollCycleStartMs[key];
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (cycleStartMs <= 0)
        cycleStartMs = now;

    const int stepIndex = static_cast<int>(((now - cycleStartMs) / kStepMs) % cycleSteps);
    int offset = 0;
    if (stepIndex < kStartPauseSteps)
        offset = 0;
    else if (stepIndex < kStartPauseSteps + scrollableSteps)
        offset = stepIndex - kStartPauseSteps;
    else
        offset = scrollableSteps - 1;

    return content.mid(offset, width);
}

QString BoardProgramEngine::previewText(const BoardFieldRule &rule, bool *active)
{
    const qint64 elapsedSinceFinishMs =
        m_finishSubject.valid ? QDateTime::currentMSecsSinceEpoch() - m_finishSubject.subjectSinceEpochMs : 0;

    const bool ruleActive = isRuleActive(rule, elapsedSinceFinishMs);
    if (active)
        *active = ruleActive;
    if (!ruleActive)
        return QString();

    const int width = rule.endChar - rule.startChar + 1;
    if (width <= 0)
        return QString();

    const QString content = resolveText(rule);
    return rule.alignment == BoardFieldRule::Alignment::Scroll
        ? scrollText(rule, content, width)
        : fitField(content, width, rule.alignment == BoardFieldRule::Alignment::Left);
}

void BoardProgramEngine::recompute()
{
    const QVector<BoardFieldRule> &rules = m_timingActive ? m_timingRules : m_outsideTimingRules;

    const qint64 elapsedSinceFinishMs =
        m_finishSubject.valid ? QDateTime::currentMSecsSinceEpoch() - m_finishSubject.subjectSinceEpochMs : 0;

    // Keyed by (line, startChar, endChar) via linear scan - rule counts per
    // display are small, so this stays simple rather than hashing a tuple
    // key. Later rules in the list win for the same slot (list order =
    // priority, same as SkiPro's Back/Front reordering).
    QVector<RenderedField> fields;
    for (const BoardFieldRule &rule : rules) {
        if (!isRuleActive(rule, elapsedSinceFinishMs))
            continue;

        const int width = rule.endChar - rule.startChar + 1;
        if (width <= 0)
            continue;

        const QString content = resolveText(rule);
        const QString fitted = rule.alignment == BoardFieldRule::Alignment::Scroll
            ? scrollText(rule, content, width)
            : fitField(content, width, rule.alignment == BoardFieldRule::Alignment::Left);

        RenderedField rendered;
        rendered.line = rule.line;
        rendered.startChar = rule.startChar;
        rendered.endChar = rule.endChar;
        rendered.alignment = rule.alignment;
        rendered.text = fitted;

        bool replaced = false;
        for (RenderedField &existing : fields) {
            if (existing.line == rendered.line && existing.startChar == rendered.startChar
                && existing.endChar == rendered.endChar) {
                existing = rendered;
                replaced = true;
                break;
            }
        }
        if (!replaced)
            fields.append(rendered);
    }

    emit fieldsChanged(fields);
}
