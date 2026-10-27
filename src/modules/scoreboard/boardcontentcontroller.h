#pragma once

#include "boardfieldrule.h"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

class QTimer;
class RaceModel;

// Central live race state of the boards, decoupled from any specific physical
// display so any number of BoardProgramEngine instances can subscribe. Ported
// from SkiAlpBridgeDisplay; the data now comes from the RaceModel (OpenSkiPro2 API):
//
// - onStartChanged() / onCourseChanged() / finishListChanged() reflect the
//   live lists of the run being timed (live.onStart, live.onCourse - first
//   started first -, live.finished - newest first). An on-course entry carries
//   the start time of day (live clock of the timing device for the running
//   time) and its ranking before this run ("previous"): priorRunsTotalMs is
//   that time in a total-of-runs race, for a live total running time.
// - finishSubjectChanged() reflects a single held/queued finish result: the
//   moment a competitor of the timed run finishes or gets DNS/DNF/DSQ
//   ("competitor" event, which carries the complete result), it is queued and
//   shown for finishHoldMs() (status "QLF") or otherHoldMs() (any other
//   status), FIFO. valid=false means nothing is held. A "reverted" event ends
//   a hold immediately (or drops a still-queued entry); a "corrected" one
//   updates the held subject.
//
// Ranks are either the overall ones (as in the events) or the ranks inside
// the first output group of the competitor (setRankField("group"), the
// "rank_cat" of SkiAlpBridgeDisplay), read from the grouped rankings.
class BoardContentController : public QObject
{
    Q_OBJECT

public:
    struct OnCourseSubject
    {
        CompetitorRef competitor;
        qint64 startDtMs = 0;        // current run's start, time of day of the timing device
        qint64 priorRunsTotalMs = 0; // total of the earlier runs (total-of-runs race only)
        // The ranking before this run (one entry, empty in the first run): the
        // only rank an on-course racer has.
        QVector<RunInfo> priorRuns;
    };

    struct FinishSubject
    {
        bool valid = false;
        CompetitorRef competitor;
        QString overallStatus;
        QString totalTime;
        QString totalRank;
        QVector<RunInfo> runs; // the run of the result (last = this run)
        qint64 subjectSinceEpochMs = 0;
    };

    explicit BoardContentController(RaceModel *model, QObject *parent = nullptr);

    // Hold duration for a regular finish (status "QLF").
    void setFinishHoldMs(int ms) { m_finishHoldMs = ms; }
    int finishHoldMs() const { return m_finishHoldMs; }
    // Hold duration for any other outcome (DNS/DNF/DSQ/...).
    void setOtherHoldMs(int ms) { m_otherHoldMs = ms; }
    int otherHoldMs() const { return m_otherHoldMs; }

    // "overall" or "group" (rank inside the first output group of the competitor).
    void setRankField(const QString &field);
    QString rankField() const { return m_rankField; }

    QVector<CompetitorRef> onStartCompetitors() const { return m_onStartCompetitors; }
    QVector<OnCourseSubject> onCourseSubjects() const { return m_onCourseSubjects; }
    QVector<FinishSubject> finishListSubjects() const { return m_finishListSubjects; }
    FinishSubject currentFinishSubject() const { return m_currentFinishSubject; }

public slots:
    void rebuild();
    void onCompetitorEvent(const QJsonObject &data);
    // Leaving Timing mode (timing == false) drops any still-queued/held finish
    // subject - Outside timing shows something else entirely.
    void onTimingModeChanged(bool timing);
    void reset();

signals:
    void onStartChanged(const QVector<CompetitorRef> &competitors);
    void onCourseChanged(const QVector<BoardContentController::OnCourseSubject> &subjects);
    // The finish list (newest first) - a plain listing with no hold or queue.
    void finishListChanged(const QVector<BoardContentController::FinishSubject> &subjects);
    void finishSubjectChanged(const BoardContentController::FinishSubject &subject);

private slots:
    void advanceQueue();

private:
    struct PendingFinish
    {
        int run;
        int bib;
        QString status;
    };

    void requireRankings();
    // A competitor event / live.finished entry -> FinishSubject, shared by the
    // list and the held subject so the two never disagree.
    FinishSubject finishSubjectFromJson(const QJsonObject &obj) const;
    void setFinishSubject(const FinishSubject &subject);
    void refreshHeldSubject();

    RaceModel *m_model;
    QVector<PendingFinish> m_finishQueue;
    QVector<CompetitorRef> m_onStartCompetitors;
    QVector<OnCourseSubject> m_onCourseSubjects;
    QVector<FinishSubject> m_finishListSubjects;
    QString m_rankField = QStringLiteral("group");
    FinishSubject m_currentFinishSubject;
    int m_currentRun = 0;
    QTimer *m_holdTimer;
    int m_finishHoldMs = 10000;
    int m_otherHoldMs = 5000;
};
