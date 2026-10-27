#pragma once

#include "FisLiveSender.h"
#include "core/Module.h"

#include <QHash>
#include <QJsonObject>
#include <QPointer>
#include <QSet>

class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;

// Module FIS Live (docs/design.md 4): sends the race to the FIS Live server
// according to the FIS specification (Live XML documentation v53).
//  - initialisation: raceinfo, meteo, then for every run with data its start list,
//    command activerun and the results already known; activerun of the timed run;
//  - then every competitor event: start, inter, finish, dns / dnf / dq / nps, for
//    every run (an event of another run than the active one is wrapped in two
//    activerun commands); a correction sends the current state of the bib again
//    with correction="y" (the last message of a bib wins); a status taken back
//    (DNF by mistake) resends the start list and the results of the run;
//  - nextstart, a changed start list (resent with its results), a changed header.
class FisLiveModule : public Module {
    Q_OBJECT
public:
    explicit FisLiveModule(RaceModel* model, QObject* parent = nullptr);

    QString id() const override { return QStringLiteral("fislive"); }
    QString title() const override { return tr("FIS Live"); }
    QWidget* createPage(QWidget* parent) override;
    void setRunning(bool running) override;
    bool isRunning() const override { return m_running; }
    QString stateText() const override;

    FisLiveSender* sender() const { return m_sender; }
    void setSenderSettings(const FisLiveSender::Settings& s);

    // Operator actions (also used by the tests).
    void sendAllAgain(bool clearFirst);
    void resendBib(int run, int bib);
    void sendMeteo();
    void sendMessage(const QString& text);
    void sendCommand(const QString& name, int run, const QString& info);
    // The FIS codex of the race from the codex of the event ("SVK6534.185" -> "6534").
    static QString codexNumber(const QString& text);

Q_SIGNALS:
    void codexChanged();

private:
    QString eventCodex() const;
    void checkRunsCompleted();
    void editSettings(QWidget* parent); // server, port, password: configuration of the application
    void updateCodex();
    void load();
    void save();
    bool ready() const; // running, settings complete, event known
    void initialize();
    void sendRun(int run);
    // A run sent again once per event loop pass (many reverts at once: one resend).
    void scheduleRun(int run);
    void flushRuns();
    void onCompetitorEvent(const QJsonObject& d);
    void onLive();
    void onResource(const QString& name);
    void onTiming(bool active, int run);
    void requireResources();
    // Children of a raceevent for the current state of a bib (competitor event data).
    QList<QByteArray> stateOfBib(const QJsonObject& d, bool correction) const;
    // ... from the run ranking (no event received for the bib)
    QList<QByteArray> resultOfBib(int run, int bib, bool correction) const;
    void sendEvents(int run, const QList<QByteArray>& children, const QString& timestamp);
    QString diffText(const QJsonObject& rankedTime) const;
    // The ranked time of a finish at FIS: the run in the 1st run, the total later (empty: none).
    QJsonObject finishOf(int run, int bib, const QJsonObject& thisRun, const QJsonObject& total) const;
    // An intermediate at FIS: the time of the run in the 1st run, the total later (empty: none).
    QByteArray interXml(int run, int bib, const QJsonObject& inter, const QString& timestamp, bool correction) const;
    qint64 timeBefore(int run, int bib) const; // total before the run in ms, -1: none
    QByteArray finishXml(int run, int bib, const QJsonObject& thisRun, const QJsonObject& total, const QString& timestamp,
                         bool correction) const;
    QString timestampOf(qint64 dayMs) const;
    int precision() const;

    FisLiveSender* m_sender;
    bool m_running = false;
    bool m_initialized = false;
    int m_activeRun = 0;
    QHash<int, QByteArray> m_startlistSent; // run -> start list sent
    QByteArray m_raceinfoSent;
    QByteArray m_meteoSent;
    int m_nextStartSent = 0;
    QSet<int> m_runsToSend;
    QSet<int> m_endRunSent;
    QSet<int> m_inProgressSent;

    QPointer<QLabel> m_codex;
    QPointer<QLabel> m_run;
    QPointer<QLabel> m_serverInfo;
    QPointer<QLabel> m_link;
    QPointer<QLabel> m_stats;
};
