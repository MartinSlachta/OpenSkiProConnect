#include "FisLiveModule.h"
#include "FisLiveXml.h"

#include "core/Log.h"
#include "core/Profile.h"
#include "core/RaceModel.h"
#include "ui/UiHelpers.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QString statusTag(const QString& state)
{
    if (state == QLatin1String("dns"))
        return QStringLiteral("dns");
    if (state == QLatin1String("dnf"))
        return QStringLiteral("dnf");
    if (state == QLatin1String("dsq"))
        return QStringLiteral("dq");
    if (state == QLatin1String("nps"))
        return QStringLiteral("nps");
    return QString();
}

QString kindTag(const QString& kind)
{
    return statusTag(kind.toLower());
}

QJsonObject rowOf(const QJsonObject& ranking, int bib)
{
    for (const auto& v : ranking.value(QStringLiteral("classified")).toArray())
        if (v.toObject().value(QStringLiteral("bib")).toInt() == bib)
            return v.toObject();
    return QJsonObject();
}

} // namespace

FisLiveModule::FisLiveModule(RaceModel* model, QObject* parent)
    : Module(model, parent)
    , m_sender(new FisLiveSender(this))
{
    load();
    m_sender->setFolder(QDir(Profile::folder()).filePath(QStringLiteral("logs")));
    connect(m_sender, &FisLiveSender::log, this, [](const QString& t) { Log::write(QStringLiteral("fislive"), t); });
    connect(m_sender, &FisLiveSender::stateChanged, this, &Module::stateChanged);

    connect(model, &RaceModel::competitorEvent, this, &FisLiveModule::onCompetitorEvent);
    connect(model, &RaceModel::liveChanged, this, &FisLiveModule::onLive);
    connect(model, &RaceModel::resourceChanged, this, &FisLiveModule::onResource);
    connect(model, &RaceModel::timingChanged, this, &FisLiveModule::onTiming);
    connect(model, &RaceModel::restarted, this, [this] { m_initialized = false; });
    requireResources();
}

QString FisLiveModule::eventCodex() const
{
    const QJsonObject general = model()->event().value(QStringLiteral("general")).toObject();
    // OpenSkiPro2 0.5+: the FIS code in its parts; earlier: "SVK6534.185" in general.codex
    const QString fis = codexNumber(general.value(QStringLiteral("fis")).toObject().value(QStringLiteral("codex")).toString());
    return fis.isEmpty() ? codexNumber(general.value(QStringLiteral("codex")).toString()) : fis;
}

QString FisLiveModule::codexNumber(const QString& text)
{
    static const QRegularExpression re(QStringLiteral("^\\s*([A-Za-z]{3})?\\s*(\\d+)(?:\\.(\\d+))?\\s*$"));
    const auto m = re.match(text);
    return m.hasMatch() ? m.captured(2) : QString();
}

void FisLiveModule::updateCodex()
{
    FisLiveSender::Settings st = m_sender->settings();
    const QString codex = eventCodex();
    if (codex == st.codex)
        return;
    st.codex = codex;
    m_sender->setSettings(st);
    m_initialized = false;
    if (!codex.isEmpty())
        Log::write(QStringLiteral("fislive"), tr("codex of the event: %1").arg(codex));
    Q_EMIT codexChanged();
    if (m_running)
        initialize();
}

void FisLiveModule::load()
{
    QSettings& s = Profile::settings();
    s.beginGroup(id());
    FisLiveSender::Settings st;
    // configuration of the application: one password of the timekeeper for every race
    st.password = s.value(QStringLiteral("password")).toString();
    st.host = s.value(QStringLiteral("host"), st.host).toString();
    st.port = quint16(s.value(QStringLiteral("port"), st.port).toUInt());
    s.endGroup();
    m_sender->setSettings(st);
}

void FisLiveModule::save()
{
    QSettings& s = Profile::settings();
    s.beginGroup(id());
    const FisLiveSender::Settings st = m_sender->settings();
    s.setValue(QStringLiteral("password"), st.password); // only in the profile on this PC
    s.setValue(QStringLiteral("host"), st.host);
    s.setValue(QStringLiteral("port"), int(st.port));
    s.remove(QStringLiteral("diffOfTotal")); // 0.2 - 0.3: run 2+ is always the total now
    s.endGroup();
    s.sync();
}

void FisLiveModule::setSenderSettings(const FisLiveSender::Settings& in)
{
    FisLiveSender::Settings s = in;
    s.codex = eventCodex(); // never typed: the codex of the event in OpenSkiPro2
    const bool newRace = s.codex != m_sender->settings().codex;
    m_sender->setSettings(s);
    if (newRace)
        m_initialized = false;
    save();
    if (m_running && !m_initialized)
        initialize();
}

QString FisLiveModule::stateText() const
{
    if (!m_running)
        return tr("stopped");
    const FisLiveSender::Settings s = m_sender->settings();
    if (s.codex.isEmpty())
        return tr("no codex in the event");
    if (s.password.isEmpty())
        return tr("password missing");
    return tr("%1 - codex %2 - %3 sent, %4 waiting for FIS")
        .arg(m_sender->isConnected() ? tr("connected") : tr("not connected"), s.codex)
        .arg(m_sender->sentCount())
        .arg(m_sender->pending());
}

void FisLiveModule::setRunning(bool running)
{
    if (running == m_running)
        return;
    m_running = running;
    if (running) {
        const FisLiveSender::Settings s = m_sender->settings();
        if (s.codex.isEmpty())
            Log::write(QStringLiteral("fislive"), tr("the event has no codex (OpenSkiPro2: Event > General Parameters > Codex)"));
        else if (s.password.isEmpty())
            Log::write(QStringLiteral("fislive"), tr("enter the FIS Live password in Settings of the FIS Live page"));
        m_sender->start();
        initialize();
    } else {
        m_sender->stop();
        m_initialized = false;
    }
    Log::write(QStringLiteral("fislive"), running ? tr("started") : tr("stopped"));
    Q_EMIT stateChanged();
}

int FisLiveModule::precision() const
{
    return model()->event().value(QStringLiteral("type")).toObject().value(QStringLiteral("precision")).toInt(2);
}

QString FisLiveModule::timestampOf(qint64 dayMs) const
{
    const QDate date = QDate::fromString(
        model()->event().value(QStringLiteral("general")).toObject().value(QStringLiteral("date")).toString(), Qt::ISODate);
    return FisLiveXml::isoUtc(date, dayMs, model()->utcOffsetMin());
}

void FisLiveModule::requireResources()
{
    QStringList names;
    const int runs = model()->event().value(QStringLiteral("type")).toObject().value(QStringLiteral("runCount")).toInt();
    for (int r = 1; r <= runs; ++r) {
        names << QStringLiteral("run/%1/startlist").arg(r) << QStringLiteral("run/%1/results").arg(r);
        if (runs > 1)
            names << QStringLiteral("results?upto=%1").arg(r); // totals of run r, the time before run r + 1
    }
    model()->require(names);
}

bool FisLiveModule::ready() const
{
    const FisLiveSender::Settings s = m_sender->settings();
    return m_running && !s.codex.isEmpty() && !s.password.isEmpty() && model()->hasResource(QStringLiteral("event"))
        && model()->hasResource(QStringLiteral("competitors"));
}

void FisLiveModule::initialize()
{
    if (!ready())
        return;
    const QJsonObject ev = model()->event();
    const int runs = ev.value(QStringLiteral("type")).toObject().value(QStringLiteral("runCount")).toInt();
    // every start list must be known first (they come after the event)
    for (int r = 1; r <= runs; ++r)
        if (!model()->hasResource(QStringLiteral("run/%1/startlist").arg(r)) || !model()->hasResource(QStringLiteral("run/%1/results").arg(r)))
            return;
    m_initialized = true;
    m_startlistSent.clear();
    m_raceinfoSent = FisLiveXml::raceinfo(ev, FisLiveXml::raceGender(model()->resource(QStringLiteral("competitors"))));
    m_sender->send(m_raceinfoSent);
    m_meteoSent = FisLiveXml::meteo(ev, FisLiveXml::isoUtcNow());
    if (!m_meteoSent.isEmpty())
        m_sender->send(m_meteoSent);
    // runs with data, then the timed run (or the last run with data) active
    int last = 0;
    for (const auto& rv : model()->resource(QStringLiteral("runs")).value(QStringLiteral("runs")).toArray()) {
        const QJsonObject r = rv.toObject();
        if (r.value(QStringLiteral("state")).toString() != QLatin1String("notStarted")) {
            sendRun(r.value(QStringLiteral("run")).toInt());
            last = r.value(QStringLiteral("run")).toInt();
        }
    }
    const int timed = model()->timedRun();
    if (timed > 0 && !m_startlistSent.contains(timed))
        sendRun(timed);
    const int active = timed > 0 ? timed : (last > 0 ? last : 1);
    if (!m_startlistSent.contains(active))
        sendRun(active);
    if (m_activeRun != active) {
        m_sender->send(FisLiveXml::command(QStringLiteral("activerun"), active, QString(), FisLiveXml::isoUtcNow()));
        m_activeRun = active;
    }
    m_endRunSent.clear();
    m_inProgressSent.clear();
    checkRunsCompleted();
    Log::write(QStringLiteral("fislive"), tr("race sent to FIS Live (codex %1), active run %2").arg(m_sender->settings().codex).arg(active));
}

void FisLiveModule::sendRun(int run)
{
    // a start list erases the results of the run at FIS: they follow
    const QString now = FisLiveXml::isoUtcNow();
    const QByteArray sl = FisLiveXml::startlist(run, model()->resource(QStringLiteral("run/%1/startlist").arg(run)), now);
    m_sender->send(sl);
    m_startlistSent.insert(run, sl);
    m_sender->send(FisLiveXml::command(QStringLiteral("activerun"), run, QString(), now));
    m_activeRun = run;

    QList<QByteArray> children;
    const QJsonObject ranking = model()->resource(QStringLiteral("run/%1/results").arg(run));
    for (const auto& v : ranking.value(QStringLiteral("classified")).toArray())
        children << resultOfBib(run, v.toObject().value(QStringLiteral("bib")).toInt(), false);
    for (const auto& g : ranking.value(QStringLiteral("unclassified")).toArray()) {
        const QString tag = kindTag(g.toObject().value(QStringLiteral("kind")).toString());
        if (tag.isEmpty() || g.toObject().value(QStringLiteral("run")).toInt() != run)
            continue;
        for (const auto& e : g.toObject().value(QStringLiteral("entries")).toArray())
            children << FisLiveXml::status(tag, e.toObject().value(QStringLiteral("bib")).toInt(), false);
    }
    // on course in the timed run: start and intermediates
    const QJsonObject live = model()->live();
    if (live.value(QStringLiteral("run")).toInt() == run)
        for (const auto& v : live.value(QStringLiteral("onCourse")).toArray()) {
            QJsonObject d = v.toObject();
            d.insert(QStringLiteral("run"), run);
            d.insert(QStringLiteral("state"), QStringLiteral("onCourse"));
            children << stateOfBib(d, false);
        }
    if (!children.isEmpty())
        m_sender->send(FisLiveXml::raceevent(children, now));
}

void FisLiveModule::scheduleRun(int run)
{
    if (m_runsToSend.isEmpty())
        QTimer::singleShot(0, this, &FisLiveModule::flushRuns);
    m_runsToSend.insert(run);
}

void FisLiveModule::flushRuns()
{
    const QSet<int> runs = m_runsToSend;
    m_runsToSend.clear();
    if (!m_initialized)
        return;
    const int back = model()->timedRun() > 0 ? model()->timedRun() : m_activeRun;
    for (int r : runs)
        sendRun(r);
    if (back > 0 && m_activeRun != back) {
        m_sender->send(FisLiveXml::command(QStringLiteral("activerun"), back, QString(), FisLiveXml::isoUtcNow()));
        m_activeRun = back;
    }
}

QString FisLiveModule::diffText(const QJsonObject& rankedTime) const
{
    const QJsonValue gap = rankedTime.value(QStringLiteral("gap"));
    if (gap.isString())
        return gap.toString();
    if (rankedTime.value(QStringLiteral("gapMs")).toInteger() == 0 && rankedTime.contains(QStringLiteral("gapMs")))
        return precision() > 0 ? QStringLiteral("0.") + QString(precision(), QLatin1Char('0')) : QStringLiteral("0"); // leader
    return QString();
}

QJsonObject FisLiveModule::finishOf(int run, int bib, const QJsonObject& thisRun, const QJsonObject& total) const
{
    // FIS: the results of a run no > 1 are added to the previous runs - its finish is the
    // total (time, diff, rank), FIS works out the time of the run itself
    if (run <= 1)
        return thisRun;
    if (total.value(QStringLiteral("time")).isString())
        return total;
    return rowOf(model()->resource(QStringLiteral("results?upto=%1").arg(run)), bib);
}

QByteArray FisLiveModule::finishXml(int run, int bib, const QJsonObject& thisRun, const QJsonObject& total,
                                    const QString& timestamp, bool correction) const
{
    const QJsonObject f = finishOf(run, bib, thisRun, total);
    if (!f.value(QStringLiteral("time")).isString())
        return QByteArray(); // no total (e.g. out of the 1st run): never the time of the run alone
    return FisLiveXml::finish(bib, f.value(QStringLiteral("time")).toString(), diffText(f), f.value(QStringLiteral("rank")).toInt(),
                              precision(), timestamp, correction);
}

qint64 FisLiveModule::timeBefore(int run, int bib) const
{
    const QJsonValue ms = rowOf(model()->resource(QStringLiteral("results?upto=%1").arg(run - 1)), bib).value(QStringLiteral("timeMs"));
    return ms.isDouble() ? ms.toInteger() : -1;
}

QByteArray FisLiveModule::interXml(int run, int bib, const QJsonObject& inter, const QString& timestamp, bool correction) const
{
    const int point = inter.value(QStringLiteral("point")).toInt();
    if (run <= 1)
        return inter.value(QStringLiteral("time")).isString()
                   ? FisLiveXml::inter(point, bib, inter.value(QStringLiteral("time")).toString(), diffText(inter),
                                       inter.value(QStringLiteral("rank")).toInt(), precision(), timestamp, correction)
                   : QByteArray();
    // like the finish: the total (the time before the run + the intermediate), ranked among
    // everybody of the run who passed the point, in units of the precision (truncated)
    const QJsonValue ms = inter.value(QStringLiteral("timeMs"));
    const qint64 before = timeBefore(run, bib);
    if (!ms.isDouble() || before < 0)
        return QByteArray(); // no total (e.g. out of the 1st run)
    qint64 div = 1;
    for (int i = precision(); i < 3; ++i)
        div *= 10;
    const qint64 mine = (before + ms.toInteger()) / div;
    qint64 best = mine;
    int rank = 1;
    auto other = [&](int b, const QJsonArray& inters) {
        if (b == bib)
            return;
        for (const auto& v : inters) {
            const QJsonObject i = v.toObject();
            const qint64 prev = timeBefore(run, b);
            if (i.value(QStringLiteral("point")).toInt() != point || !i.value(QStringLiteral("timeMs")).isDouble() || prev < 0)
                continue;
            const qint64 t = (prev + i.value(QStringLiteral("timeMs")).toInteger()) / div;
            best = qMin(best, t);
            if (t < mine)
                ++rank;
        }
    };
    for (const auto& v : model()->resource(QStringLiteral("run/%1/results").arg(run)).value(QStringLiteral("classified")).toArray())
        other(v.toObject().value(QStringLiteral("bib")).toInt(), v.toObject().value(QStringLiteral("inter")).toArray());
    const QJsonObject live = model()->live();
    if (live.value(QStringLiteral("run")).toInt() == run)
        for (const auto& v : live.value(QStringLiteral("onCourse")).toArray())
            other(v.toObject().value(QStringLiteral("bib")).toInt(), v.toObject().value(QStringLiteral("inter")).toArray());
    return FisLiveXml::inter(point, bib, FisLiveXml::timeText(mine * div, precision()), FisLiveXml::timeText((mine - best) * div, precision()),
                             rank, precision(), timestamp, correction);
}

QList<QByteArray> FisLiveModule::stateOfBib(const QJsonObject& d, bool correction) const
{
    QList<QByteArray> out;
    const int run = d.value(QStringLiteral("run")).toInt();
    const int bib = d.value(QStringLiteral("bib")).toInt();
    const QString state = d.value(QStringLiteral("state")).toString();
    const QString tag = statusTag(state);
    if (!tag.isEmpty()) {
        out << FisLiveXml::status(tag, bib, correction);
        return out;
    }
    const QJsonValue start = d.value(QStringLiteral("startDayMs"));
    if ((state == QLatin1String("onCourse") || state == QLatin1String("finished")) && !correction)
        out << FisLiveXml::start(bib, start.isDouble() ? timestampOf(start.toInteger()) : QString(), false);
    for (const auto& v : d.value(QStringLiteral("inter")).toArray()) {
        const QJsonObject i = v.toObject();
        const QJsonValue day = i.value(QStringLiteral("dayMs"));
        const QByteArray x = interXml(run, bib, i, day.isDouble() ? timestampOf(day.toInteger()) : QString(), correction);
        if (!x.isEmpty())
            out << x;
    }
    const QJsonObject thisRun = d.value(QStringLiteral("thisRun")).toObject();
    if (state == QLatin1String("finished") && !thisRun.isEmpty()) {
        const QJsonValue fin = d.value(QStringLiteral("finishDayMs"));
        const QByteArray f = finishXml(run, bib, thisRun, d.value(QStringLiteral("total")).toObject(),
                                       fin.isDouble() ? timestampOf(fin.toInteger()) : QString(), correction);
        if (!f.isEmpty())
            out << f;
    }
    return out;
}

QList<QByteArray> FisLiveModule::resultOfBib(int run, int bib, bool correction) const
{
    // a competitor event received in this session carries everything, with times of day
    const QJsonObject st = model()->competitorState(run, bib);
    if (!st.isEmpty())
        return stateOfBib(st, correction);
    QList<QByteArray> out;
    const QJsonObject ranking = model()->resource(QStringLiteral("run/%1/results").arg(run));
    const QJsonObject row = rowOf(ranking, bib);
    if (!row.isEmpty()) {
        for (const auto& v : row.value(QStringLiteral("inter")).toArray()) {
            const QByteArray x = interXml(run, bib, v.toObject(), QString(), correction);
            if (!x.isEmpty())
                out << x;
        }
        const QByteArray f = finishXml(run, bib, row, QJsonObject(), QString(), correction);
        if (!f.isEmpty())
            out << f;
        return out;
    }
    for (const auto& g : ranking.value(QStringLiteral("unclassified")).toArray())
        for (const auto& e : g.toObject().value(QStringLiteral("entries")).toArray())
            if (e.toObject().value(QStringLiteral("bib")).toInt() == bib) {
                const QString tag = kindTag(g.toObject().value(QStringLiteral("kind")).toString());
                if (!tag.isEmpty())
                    out << FisLiveXml::status(tag, bib, correction);
            }
    return out;
}

void FisLiveModule::sendEvents(int run, const QList<QByteArray>& children, const QString& timestamp)
{
    if (children.isEmpty())
        return;
    const QByteArray ev = FisLiveXml::raceevent(children, timestamp);
    if (run == m_activeRun || m_activeRun == 0) {
        m_sender->send(ev);
        return;
    }
    // another run (e.g. a DSQ of the 1st run given during the 2nd): switch, send, switch back
    const QString now = FisLiveXml::isoUtcNow();
    m_sender->send(FisLiveXml::command(QStringLiteral("activerun"), run, QString(), now) + ev
                   + FisLiveXml::command(QStringLiteral("activerun"), m_activeRun, QString(), now));
}

void FisLiveModule::onCompetitorEvent(const QJsonObject& d)
{
    if (!m_initialized)
        return;
    const int run = d.value(QStringLiteral("run")).toInt();
    const int bib = d.value(QStringLiteral("bib")).toInt();
    if (d.value(QStringLiteral("competitor")).toObject().value(QStringLiteral("forerunner")).toBool())
        return;
    if (!m_startlistSent.contains(run) || m_runsToSend.contains(run)) {
        scheduleRun(run); // the first event of a run nobody sent yet: start list + its results
        return;
    }
    const QString change = d.value(QStringLiteral("change")).toString();
    const QString state = d.value(QStringLiteral("state")).toString();
    const QString now = FisLiveXml::isoUtcNow();
    QList<QByteArray> children;
    QString ts = now;
    if (change == QLatin1String("started")) {
        const QJsonValue s = d.value(QStringLiteral("startDayMs"));
        if (s.isDouble())
            ts = timestampOf(s.toInteger());
        children << FisLiveXml::start(bib, ts, false);
    } else if (change == QLatin1String("intermediate")) {
        const int point = d.value(QStringLiteral("point")).toInt();
        for (const auto& v : d.value(QStringLiteral("inter")).toArray()) {
            const QJsonObject i = v.toObject();
            if (i.value(QStringLiteral("point")).toInt() != point)
                continue;
            const QJsonValue day = i.value(QStringLiteral("dayMs"));
            if (day.isDouble())
                ts = timestampOf(day.toInteger());
            const QByteArray x = interXml(run, bib, i, ts, false);
            if (!x.isEmpty())
                children << x;
        }
    } else if (change == QLatin1String("finished")) {
        const QJsonObject thisRun = d.value(QStringLiteral("thisRun")).toObject();
        if (!thisRun.isEmpty()) {
            const QJsonValue fin = d.value(QStringLiteral("finishDayMs"));
            if (fin.isDouble())
                ts = timestampOf(fin.toInteger());
            const QByteArray f = finishXml(run, bib, thisRun, d.value(QStringLiteral("total")).toObject(), ts, false);
            if (!f.isEmpty())
                children << f;
        }
    } else if (change == QLatin1String("status")) {
        children << FisLiveXml::status(statusTag(state), bib, false);
    } else if (change == QLatin1String("corrected")) {
        children << stateOfBib(d, true);
    } else if (change == QLatin1String("reverted")) {
        if (state == QLatin1String("onCourse") || state == QLatin1String("notStarted")) {
            // no message takes back a finish or a status: the run again
            scheduleRun(run);
            return;
        }
        children << stateOfBib(d, true); // e.g. a DSQ removed: the finish again
    }
    sendEvents(run, children, ts);
}

void FisLiveModule::onLive()
{
    if (!m_initialized)
        return;
    const QJsonObject live = model()->live();
    const int run = live.value(QStringLiteral("run")).toInt();
    const QJsonArray onStart = live.value(QStringLiteral("onStart")).toArray();
    const int bib = onStart.isEmpty() ? 0 : onStart.first().toObject().value(QStringLiteral("bib")).toInt();
    if (run <= 0 || run != m_activeRun || bib <= 0 || bib == m_nextStartSent)
        return;
    m_nextStartSent = bib;
    m_sender->send(FisLiveXml::raceevent({ FisLiveXml::nextstart(bib) }, FisLiveXml::isoUtcNow()));
}

void FisLiveModule::onTiming(bool active, int run)
{
    if (m_initialized && active && run > 0 && !m_inProgressSent.contains(run)) {
        m_inProgressSent.insert(run);
        m_sender->send(FisLiveXml::command(QStringLiteral("inprogress"), run, QString(), FisLiveXml::isoUtcNow()));
    }
    if (!m_initialized || !active || run <= 0 || run == m_activeRun)
        return;
    if (!m_startlistSent.contains(run)) {
        sendRun(run);
        return;
    }
    m_sender->send(FisLiveXml::command(QStringLiteral("activerun"), run, QString(), FisLiveXml::isoUtcNow()));
    m_activeRun = run;
}

void FisLiveModule::onResource(const QString& name)
{
    if (name == QLatin1String("event")) {
        requireResources();
        updateCodex();
    }
    if (!m_initialized) {
        if (m_running)
            initialize(); // waiting for the resources
        return;
    }
    if (name == QLatin1String("event") || name == QLatin1String("competitors")) {
        const QByteArray ri = FisLiveXml::raceinfo(model()->event(), FisLiveXml::raceGender(model()->resource(QStringLiteral("competitors"))));
        if (ri != m_raceinfoSent) {
            m_raceinfoSent = ri;
            m_sender->send(ri);
        }
        const QByteArray me = FisLiveXml::meteo(model()->event(), FisLiveXml::isoUtcNow());
        if (!me.isEmpty() && me.mid(me.indexOf('>')) != m_meteoSent.mid(m_meteoSent.indexOf('>'))) {
            m_meteoSent = me;
            m_sender->send(me);
        }
    }
    if (name == QLatin1String("runs"))
        checkRunsCompleted();
    if (name.startsWith(QLatin1String("run/")) && name.endsWith(QLatin1String("/startlist"))) {
        const int run = name.split(QLatin1Char('/')).value(1).toInt();
        if (!m_startlistSent.contains(run))
            return;
        const QByteArray sl = FisLiveXml::startlist(run, model()->resource(name), QString());
        const QByteArray sent = m_startlistSent.value(run);
        // the same racers in the same order: nothing to do (timestamps differ)
        if (sl.mid(sl.indexOf("<racer")) == sent.mid(sent.indexOf("<racer")))
            return;
        Log::write(QStringLiteral("fislive"), tr("start list of run %1 changed - sent again with its results").arg(run));
        scheduleRun(run);
    }
}

void FisLiveModule::checkRunsCompleted()
{
    if (!m_initialized)
        return;
    for (const auto& rv : model()->resource(QStringLiteral("runs")).value(QStringLiteral("runs")).toArray()) {
        const QJsonObject r = rv.toObject();
        const int run = r.value(QStringLiteral("run")).toInt();
        const bool finished = r.value(QStringLiteral("state")).toString() == QLatin1String("finished");
        if (!finished) {
            m_endRunSent.remove(run); // e.g. a result taken back: completed again later
            continue;
        }
        if (m_endRunSent.contains(run))
            continue;
        m_endRunSent.insert(run);
        m_sender->send(FisLiveXml::command(QStringLiteral("endrun"), run, QString(), FisLiveXml::isoUtcNow()));
        Log::write(QStringLiteral("fislive"), tr("run %1 completed - endrun sent").arg(run));
    }
}

void FisLiveModule::sendAllAgain(bool clearFirst)
{
    if (!m_running)
        return;
    if (clearFirst)
        m_sender->send(FisLiveXml::command(QStringLiteral("clear"), 0, QString(), FisLiveXml::isoUtcNow()));
    m_initialized = false;
    m_activeRun = 0;
    initialize();
}

void FisLiveModule::resendBib(int run, int bib)
{
    if (!m_initialized)
        return;
    const QList<QByteArray> children = resultOfBib(run, bib, true);
    if (children.isEmpty()) {
        Log::write(QStringLiteral("fislive"), tr("bib %1 has no result in run %2").arg(bib).arg(run));
        return;
    }
    sendEvents(run, children, FisLiveXml::isoUtcNow());
    Log::write(QStringLiteral("fislive"), tr("result of bib %1 (run %2) sent again").arg(bib).arg(run));
}

void FisLiveModule::sendMeteo()
{
    const QByteArray me = FisLiveXml::meteo(model()->event(), FisLiveXml::isoUtcNow());
    if (me.isEmpty()) {
        Log::write(QStringLiteral("fislive"), tr("no weather in the header of the event (OpenSkiPro2: Event > Header)"));
        return;
    }
    m_meteoSent = me;
    m_sender->send(me);
}

void FisLiveModule::sendMessage(const QString& text)
{
    // an empty text replaces the message shown by FIS Live: the message is cleared
    m_sender->send(FisLiveXml::message(text.trimmed(), FisLiveXml::isoUtcNow()));
    Log::write(QStringLiteral("fislive"), text.trimmed().isEmpty() ? tr("message cleared") : tr("message sent"));
}

void FisLiveModule::sendCommand(const QString& name, int run, const QString& info)
{
    m_sender->send(FisLiveXml::command(name, run, info, FisLiveXml::isoUtcNow()));
    Log::write(QStringLiteral("fislive"), tr("command %1 sent").arg(name));
}

void FisLiveModule::editSettings(QWidget* parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(tr("FIS Live - settings"));
    auto* lay = new QVBoxLayout(&dlg);
    auto* form = new QFormLayout;
    const FisLiveSender::Settings st = m_sender->settings();
    auto* host = new QLineEdit(st.host, &dlg);
    host->setMinimumWidth(260);
    auto* port = new QSpinBox(&dlg);
    port->setRange(1, 65535);
    port->setValue(st.port);
    auto* password = new QLineEdit(st.password, &dlg);
    password->setEchoMode(QLineEdit::Password);
    form->addRow(tr("Server :"), host);
    form->addRow(tr("TCP port :"), port);
    form->addRow(tr("Password :"), password);
    lay->addLayout(form);
    auto* note = new QLabel(tr("The password of the timekeeper for the FIS Live server, the same for every race; "
                               "it is changed in the personal area of the FIS website. The codex comes from the event."),
                            &dlg);
    note->setWordWrap(true);
    lay->addWidget(note);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(box);
    dlg.resize(520, 0);
    if (dlg.exec() != QDialog::Accepted)
        return;
    FisLiveSender::Settings s;
    s.host = host->text().trimmed();
    s.port = quint16(port->value());
    s.password = password->text();
    setSenderSettings(s);
}

QWidget* FisLiveModule::createPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* lay = new QVBoxLayout(page);

    auto* server = new QGroupBox(tr("Race at FIS Live"), page);
    auto* form = new QFormLayout(server);
    m_codex = new QLabel(server);
    m_run = new QLabel(server);
    m_link = new QLabel(server);
    m_link->setOpenExternalLinks(true);
    m_serverInfo = new QLabel(server);
    auto* race = new QHBoxLayout();
    race->addWidget(m_codex);
    race->addSpacing(40);
    race->addWidget(new QLabel(tr("Run :"), server));
    race->addWidget(m_run);
    race->addStretch();
    form->addRow(tr("Codex :"), race);
    form->addRow(tr("Live link :"), m_link);
    auto* srv = new QHBoxLayout();
    srv->addWidget(m_serverInfo);
    srv->addSpacing(16);
    auto* settingsBtn = UiHelpers::button(QStringLiteral("params"), tr("Settings..."), server);
    connect(settingsBtn, &QPushButton::clicked, this, [this, page] { editSettings(page); });
    srv->addWidget(settingsBtn);
    srv->addStretch();
    form->addRow(tr("Server :"), srv);

    // codex, run, server and link follow the event, the timing and the settings
    auto showRace = [this] {
        if (!m_codex)
            return;
        const FisLiveSender::Settings st = m_sender->settings();
        const QString codex = st.codex;
        const QString ball = QStringLiteral("<img src=\":/icons/%1.svg\" width=\"14\" height=\"14\"> ");
        m_codex->setText(ball.arg(codex.isEmpty() ? QStringLiteral("ball_red") : QStringLiteral("ball_green"))
                         + (codex.isEmpty() ? tr("<b>none</b> - set it in OpenSkiPro2 (Event > General Parameters > Codex)")
                                            : QStringLiteral("<b>%1</b>").arg(codex)));
        const int run = model()->timedRun();
        m_run->setText(ball.arg(run > 0 ? QStringLiteral("ball_green") : QStringLiteral("ball_red"))
                       + (run > 0 ? QStringLiteral("<b>%1</b>").arg(run) : tr("timing window closed")));
        m_link->setText(codex.isEmpty() ? QStringLiteral("-")
                                        : QStringLiteral("<a href=\"https://live.fis-ski.com/lv-al%1.htm\">https://live.fis-ski.com/lv-al%1.htm</a>").arg(codex));
        m_serverInfo->setText(QStringLiteral("%1 : %2  -  %3").arg(st.host).arg(st.port)
                                  .arg(st.password.isEmpty() ? tr("<span style=\"color:#c00000\"><b>password missing</b></span>")
                                                             : tr("password set")));
    };
    connect(this, &FisLiveModule::codexChanged, m_codex, showRace);
    connect(model(), &RaceModel::timingChanged, m_codex, showRace);
    connect(m_sender, &FisLiveSender::stateChanged, m_codex, showRace);
    showRace();
    lay->addWidget(server);

    auto* autoInfo = UiHelpers::infoBox(page);
    autoInfo->setWordWrap(true);
    autoInfo->setText(tr("Automatic: once the module runs, everything is sent by itself - the race and the start lists, "
                         "starts, intermediates, finishes, DNS / DNF / DSQ / NPS (also after the timing), corrections, "
                         "the next at the start, the weather of the header, \"in progress\" when the timing of a run is "
                         "opened and \"end of run\" when every competitor has a result. After a lost connection the "
                         "waiting messages are sent again. The actions below are only for special cases."));
    lay->addWidget(autoInfo);

    auto* manual = new QGroupBox(tr("Manual - only when needed"), page);
    auto* manualLay = new QVBoxLayout(manual);
    auto* actions = new QHBoxLayout();
    auto* all = UiHelpers::button(QStringLiteral("export"), tr("Send all again"), page);
    all->setToolTip(tr("Clears the race at FIS Live and sends everything again (raceinfo, start lists, results)."));
    connect(all, &QPushButton::clicked, this, [this, page] {
        if (QMessageBox::question(page, tr("FIS Live"), tr("Clear the race at FIS Live and send everything again?")) == QMessageBox::Yes)
            sendAllAgain(true);
    });
    actions->addWidget(all);
    auto* meteoBtn = UiHelpers::button(QStringLiteral("info"), tr("Meteo"), page);
    meteoBtn->setToolTip(tr("Weather, temperature at the start and snow from the header of the event."));
    connect(meteoBtn, &QPushButton::clicked, this, &FisLiveModule::sendMeteo);
    actions->addWidget(meteoBtn);
    actions->addSpacing(16);
    actions->addWidget(new QLabel(tr("Send result again - run :"), page));
    auto* run = new QSpinBox(page);
    run->setRange(1, 9);
    actions->addWidget(run);
    actions->addWidget(new QLabel(tr("bib :"), page));
    auto* bib = new QSpinBox(page);
    bib->setRange(1, 9999);
    actions->addWidget(bib);
    auto* resend = UiHelpers::button(QStringLiteral("check"), tr("Send"), page);
    connect(resend, &QPushButton::clicked, this, [this, run, bib] { resendBib(run->value(), bib->value()); });
    actions->addWidget(resend);
    actions->addStretch();
    manualLay->addLayout(actions);

    auto* msgRow = new QHBoxLayout();
    msgRow->addWidget(new QLabel(tr("Message :"), page));
    auto* msg = new QLineEdit(page);
    msgRow->addWidget(msg, 1);
    auto* msgBtn = UiHelpers::button(QStringLiteral("comment"), tr("Send message"), page);
    msgBtn->setToolTip(tr("The message shown by FIS Live; empty clears it."));
    connect(msgBtn, &QPushButton::clicked, this, [this, msg] {
        sendMessage(msg->text());
        msg->clear();
    });
    msgRow->addWidget(msgBtn);
    auto* clearBtn = UiHelpers::button(QStringLiteral("delete"), tr("Clear"), page);
    clearBtn->setToolTip(tr("Clears the message shown by FIS Live (the results stay)."));
    connect(clearBtn, &QPushButton::clicked, this, [this, msg] {
        sendMessage(QString());
        msg->clear();
    });
    msgRow->addWidget(clearBtn);
    manualLay->addLayout(msgRow);

    auto* cmdRow = new QHBoxLayout();
    auto* cmdLabel = new QLabel(tr("Race status :"), page);
    cmdLabel->setToolTip(tr("Optional. The state of the race shown by FIS Live (<command> of the FIS specification):\n"
                            "Scheduled / Ready to start / In progress / Run completed - the schedule of a run,\n"
                            "Delayed / Interrupted / Postponed / Cancelled - with the reason in Info,\n"
                            "Unofficial / Official result - the validity of the results.\n"
                            "The live results themselves are sent without it."));
    cmdRow->addWidget(cmdLabel);
    auto* cmd = new QComboBox(page);
    const QList<QPair<QString, QString>> cmds = {
        { QStringLiteral("scheduled"), tr("Scheduled") },          { QStringLiteral("readytostart"), tr("Ready to start") },
        { QStringLiteral("inprogress"), tr("In progress") },       { QStringLiteral("runcompleted"), tr("Run completed") },
        { QStringLiteral("endrun"), tr("End of run") },            { QStringLiteral("unofficial_result"), tr("Unofficial result") },
        { QStringLiteral("official_result"), tr("Official result") }, { QStringLiteral("delayed"), tr("Delayed (info)") },
        { QStringLiteral("interrupted"), tr("Interrupted (info)") }, { QStringLiteral("postponed"), tr("Postponed (info)") },
        { QStringLiteral("cancelled"), tr("Cancelled (info)") },
    };
    for (const auto& c : cmds)
        cmd->addItem(c.second, c.first);
    cmdRow->addWidget(cmd);
    cmdRow->addWidget(new QLabel(tr("run :"), page));
    auto* cmdRun = new QSpinBox(page);
    cmdRun->setRange(1, 9);
    cmdRow->addWidget(cmdRun);
    cmdRow->addWidget(new QLabel(tr("info :"), page));
    auto* info = new QLineEdit(page);
    cmdRow->addWidget(info, 1);
    cmd->setToolTip(cmdLabel->toolTip());
    // the runs follow the run of the timing window (another one can still be chosen)
    auto followRun = [this, run, cmdRun] {
        const int timed = model()->timedRun();
        if (timed > 0) {
            run->setValue(timed);
            cmdRun->setValue(timed);
        }
    };
    connect(model(), &RaceModel::timingChanged, run, followRun);
    followRun();
    auto* cmdBtn = UiHelpers::button(QStringLiteral("ok"), tr("Send status"), page);
    connect(cmdBtn, &QPushButton::clicked, this, [this, cmd, cmdRun, info] {
        sendCommand(cmd->currentData().toString(), cmdRun->value(), info->text().trimmed());
    });
    cmdRow->addWidget(cmdBtn);
    manualLay->addLayout(cmdRow);
    lay->addWidget(manual);

    m_stats = UiHelpers::infoBox(page);
    m_stats->setWordWrap(true);
    auto refresh = [this] {
        if (!m_stats)
            return;
        m_stats->setText(tr("Server %1:%2 - %3 - last sequence %4 - sent %5, acknowledged by FIS %6, waiting %7. "
                            "Everything sent is in logs/fislive-<codex>-<date>.xml (password hidden).")
                             .arg(m_sender->settings().host)
                             .arg(m_sender->settings().port)
                             .arg(m_sender->isConnected() ? tr("connected") : tr("not connected"))
                             .arg(m_sender->lastSequence())
                             .arg(m_sender->sentCount())
                             .arg(m_sender->ackCount())
                             .arg(m_sender->pending()));
    };
    connect(m_sender, &FisLiveSender::stateChanged, m_stats, refresh);
    refresh();
    lay->addWidget(m_stats);
    lay->addStretch();
    return page;
}
