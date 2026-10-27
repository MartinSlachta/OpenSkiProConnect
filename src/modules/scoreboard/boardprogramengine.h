#pragma once

#include "boardcontentcontroller.h"
#include "boardfieldrule.h"

#include <QHash>
#include <QObject>
#include <QVector>

class QTimer;

// Resolves one display's two line-programs (Timing / Outside-timing - see
// docs/board-display-system.md) against the shared BoardContentController
// state into concrete per-line text - one instance per configured display,
// all fed by the same BoardContentController. Which program is active
// switches automatically with BridgeApiClient's "timing" signal.
//
// A rule's own BoardFieldRule::Scope picks which of the live states it
// reads (on-course list, finish list, or the held finish subject);
// isRuleActive() independently decides whether it may show at all. Those
// two were once the same thing - the trigger used to imply the data source
// - which is exactly why a rule could never read the finish list
// continuously.
class BoardProgramEngine : public QObject
{
    Q_OBJECT

public:
    struct RenderedField
    {
        int line = 1;
        int startChar = 1;
        int endChar = 8;
        BoardFieldRule::Alignment alignment = BoardFieldRule::Alignment::Right;
        QString text;
    };

    explicit BoardProgramEngine(QObject *parent = nullptr);

    // Rule and state setters (these, plus the slots below) only store - they
    // never publish. fieldsChanged is emitted from the tick alone, so an
    // intermediate state can't reach a board; see tick() for why that matters.
    void setTimingRules(const QVector<BoardFieldRule> &rules);
    void setOutsideTimingRules(const QVector<BoardFieldRule> &rules);
    QVector<BoardFieldRule> timingRules() const { return m_timingRules; }
    QVector<BoardFieldRule> outsideTimingRules() const { return m_outsideTimingRules; }

    // 1 or 10 - sets the tick, which is both how often anything is sent to
    // this display and RunningTime's precision (1fps -> whole seconds,
    // 10fps -> tenths). An event-driven change therefore reaches the board
    // within one tick rather than instantly, which is the point.
    void setUpdateRateFps(int fps);

    // UI-only: evaluates a single rule against the engine's current live
    // state, independent of which program (Timing/Outside-timing) is
    // actually driving the display and independent of any other rule
    // sharing the same line/chars slot. Used by the "Edit lines" dialog's
    // per-row preview column - unlike fieldsChanged, it never mixes one
    // rule's output into another's. *active reports whether the rule is
    // currently eligible to show (trigger window open); return value is
    // empty when inactive.
    QString previewText(const BoardFieldRule &rule, bool *active = nullptr);

public slots:
    void onOnStartChanged(const QVector<CompetitorRef> &competitors);
    void onOnCourseChanged(const QVector<BoardContentController::OnCourseSubject> &subjects);
    void onFinishListChanged(const QVector<BoardContentController::FinishSubject> &subjects);
    void onFinishSubjectChanged(const BoardContentController::FinishSubject &subject);
    void onClockOffsetChanged(qint64 offsetMs, bool synced);
    void onTimingModeChanged(bool timing);

signals:
    void fieldsChanged(const QVector<BoardProgramEngine::RenderedField> &fields);

private slots:
    void tick();

private:
    bool isRuleActive(const BoardFieldRule &rule, qint64 elapsedSinceFinishMs) const;
    // The subject a rule's Scope + scopeIndex point at, or nullptr when
    // there's nobody there (index past the end of the list, no finish
    // currently held, or a scope with no data source yet) - in which case
    // the rule renders blank.
    const BoardContentController::OnCourseSubject *onCourseSubjectFor(const BoardFieldRule &rule) const;
    const BoardContentController::FinishSubject *finishSubjectFor(const BoardFieldRule &rule) const;
    QString resolveText(const BoardFieldRule &rule) const;
    QString resolveFieldText(const BoardFieldRule &rule) const;
    QString resolveRunningTimeText(const BoardFieldRule &rule) const;
    QString resolveRankText(const BoardFieldRule &rule) const;
    QString resolveStatusText(const BoardFieldRule &rule) const;
    QString scrollText(const BoardFieldRule &rule, const QString &content, int width);
    void recompute();

    QVector<BoardFieldRule> m_timingRules;
    QVector<BoardFieldRule> m_outsideTimingRules;
    bool m_timingActive = true;

    QVector<CompetitorRef> m_onStartCompetitors;
    QVector<BoardContentController::OnCourseSubject> m_onCourseSubjects;
    QVector<BoardContentController::FinishSubject> m_finishListSubjects;
    BoardContentController::FinishSubject m_finishSubject;
    qint64 m_clockOffsetMs = 0;

    // Per-rule (keyed by "line:startChar:endChar") scroll cycle start time,
    // for Alignment::Scroll rules.
    QHash<QString, qint64> m_scrollCycleStartMs;

    QTimer *m_tickTimer;
    int m_updateRateFps = 10;
};
