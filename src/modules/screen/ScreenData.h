#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

class QTimer;
class RaceModel;

// The view model of the screens (docs/screen-design.md 3): everything the pages
// show, computed once from the RaceModel and pushed ready-made to every browser -
// event, phase of the race, live lists, the rankings (total and the run, with the
// ranks inside the output groups) and the start list. Rows are flat objects whose
// keys are the fields of the configurations (ScreenConfig::fields()).
class ScreenData : public QObject {
    Q_OBJECT
public:
    explicit ScreenData(RaceModel* model, QObject* parent = nullptr);

    RaceModel* model() const { return m_model; }
    QJsonObject state() const { return m_state; }
    // Clock of the timing device now ({dayMs, synced}); the pages keep it running.
    QJsonObject clock() const;
    // Rebuilds at once (tests); normally a change is rebuilt after a short delay.
    void rebuild();

    // "1:02.37" / "0.63" from milliseconds, truncated to the precision of the event.
    static QString formatMs(qint64 ms, int precision);

    // Rows of any run (module Publish sends every run, the screens only the current one):
    // the ranking of a run alone (total = false, resource "run/<n>/results") or after it
    // (total = true, "results?upto=<n>"), and the start list of a run. Valid after rebuild().
    QJsonObject rankingOfRun(int run, bool total) const;
    QJsonArray startListOfRun(int run) const { return startList(run); }

Q_SIGNALS:
    void stateChanged(); // the state differs from the last one

private:
    void schedule();
    void requireResources();
    int currentRun() const;   // timed run, else the last run with data, else 1
    int startListRun() const; // timed run, else the next run without data
    QString phase() const;    // live / beforeRace / results
    QJsonObject base(const QJsonObject& competitor) const;
    QJsonObject ranking(const QString& name, bool total, int run) const;
    QJsonArray startList(int run) const;
    QJsonArray liveRows(const QString& list) const;
    void addGroupRanks(QJsonArray& rows) const;

    RaceModel* m_model;
    QTimer* m_timer;
    QJsonObject m_state;
    int m_precision = 2;
    QHash<int, QJsonObject> m_totalByBib; // rows of the total ranking (group ranks for the live lists)
    QHash<int, QJsonObject> m_prevByBib;  // rows of the ranking before the current run (its runs for the live lists)
};
