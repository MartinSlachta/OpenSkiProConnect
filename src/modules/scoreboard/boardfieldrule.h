#pragma once

#include <QString>
#include <QStringList>

class QJsonObject;

// Common competitor metadata, shared between on-course and finished
// subjects (see BoardContentController) - one shape for both since the
// data itself doesn't change based on which state a competitor is in.
struct CompetitorRef
{
    int bib = 0;
    QString firstName;
    QString lastName;
    QString category;
    QString nation;
    QString club;
    QString sex;
    int year = 0;
};

// One run's own result, from /api/onfinish's "runs" array. rank is already
// resolved to whichever of "rank"/"rank_cat" the app-wide rank field
// setting picks (BoardContentController::setRankField()) - one choice for
// every display, not a per-rule one.
struct RunInfo
{
    int runId = 0;
    QString status;
    QString time;
    QString rank;
};

// One line/position on a physical board, paired with what should be shown
// there - the same shape SkiPro's own TAG Heuer HL980/985 driver uses
// ("Line" + start/end character + alignment + content type). See
// docs/board-display-system.md for the full requirements this models.
//
// Scope and Trigger are deliberately independent: Scope says *whose* data a
// rule reads, Trigger says *when* the rule may show. Continuous rules show
// whenever their subject is available; AfterFinish/AfterDns/AfterDnf are
// event overlays, eligible only while [startOffsetSec,
// startOffsetSec+durationSec) has elapsed since the current finish subject
// became current - regardless of which scope the rule itself reads from, so
// "show the finish list's top 3 for 10s whenever somebody finishes" is just
// Scope::OnFinish + Trigger::AfterFinish. See BoardProgramEngine.
struct BoardFieldRule
{
    // Which live list (or held subject) a rule's content comes from - see
    // docs/board-display-system.md. The first three are indexed lists
    // addressed by scopeIndex; LastFinish is the single queued/held finish
    // subject (BoardContentController's hold + FIFO), which has no index.
    //
    // OnStart carries competitor metadata only - /api/onstart has no time,
    // rank or status - so only ContentType::Field resolves against it;
    // everything else renders blank there.
    enum class Scope { OnStart, OnCourse, OnFinish, LastFinish };
    enum class ContentType { Field, RunningTime, Rank, StatusText, Date, DayTime, Text };
    enum class FieldName { Bib, Category, FirstName, LastName, Nation, Sex, Year, Club };
    // Which run's value to use for RunningTime/Rank/StatusText - Total (the
    // overall/cumulative value) or ThisRun (the specific run that's
    // current/just finished).
    enum class RunSelector { Total, ThisRun };
    enum class Trigger { Continuous, AfterFinish, AfterDns, AfterDnf };
    enum class Alignment { Left, Right, Scroll };

    int line = 1;
    int startChar = 1;
    int endChar = 8;
    Alignment alignment = Alignment::Right;

    Scope scope = Scope::OnCourse;
    ContentType contentType = ContentType::Field;
    FieldName fieldName = FieldName::Bib; // ContentType::Field only
    // Position within the scope's list, 0-based: for OnCourse that's course
    // order (so a board can follow 2-3 simultaneously on-course competitors
    // side by side), for OnFinish it's /api/onfinish's own newest-first
    // order (0 = most recent finisher). An index with nobody there renders
    // blank. Ignored by Scope::LastFinish, which has no list.
    int scopeIndex = 0;
    RunSelector runSelector = RunSelector::Total; // RunningTime/Rank/StatusText only

    QString prefix;
    QString suffix;
    // Offset into the *resolved content string* (distinct from
    // startChar/endChar, which is where on the physical board this rule
    // draws) - lets one long piece of content be split across multiple
    // lines/rules, each showing a different slice of the same text.
    int contentStartChar = 0;
    QString dateTimeFormat = QStringLiteral("dd.MM.yyyy"); // Date/DayTime only - Qt date/time format string
    QString staticText;                                    // ContentType::Text only

    Trigger trigger = Trigger::Continuous;
    // Only meaningful when trigger != Continuous - whole-second precision.
    int startOffsetSec = 0;
    int durationSec = 5;

    QJsonObject toJsonObject() const;
    static BoardFieldRule fromJsonObject(const QJsonObject &obj);
};

QString scopeLabel(BoardFieldRule::Scope scope);
BoardFieldRule::Scope scopeFromLabel(const QString &label);
// Only the scopes that actually have a data source - OnStart is omitted on
// purpose (see the Scope enum's comment).
QStringList scopeLabels();

QString contentTypeLabel(BoardFieldRule::ContentType type);
BoardFieldRule::ContentType contentTypeFromLabel(const QString &label);
QStringList contentTypeLabels();

QString fieldNameLabel(BoardFieldRule::FieldName field);
BoardFieldRule::FieldName fieldNameFromLabel(const QString &label);
QStringList fieldNameLabels();

QString runSelectorLabel(BoardFieldRule::RunSelector selector);
BoardFieldRule::RunSelector runSelectorFromLabel(const QString &label);
QStringList runSelectorLabels();

QString triggerLabel(BoardFieldRule::Trigger trigger);
BoardFieldRule::Trigger triggerFromLabel(const QString &label);
QStringList triggerLabels();

QString alignmentLabel(BoardFieldRule::Alignment alignment);
BoardFieldRule::Alignment alignmentFromLabel(const QString &label);
QStringList alignmentLabels();
