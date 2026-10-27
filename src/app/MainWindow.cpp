#include "MainWindow.h"

#include "FisLiveModule.h"
#include "PublishModule.h"
#include "ScoreboardModule.h"
#include "ScreenModule.h"
#include "core/ApiClient.h"
#include "core/Log.h"
#include "core/Profile.h"
#include "core/RaceModel.h"
#include "ui/NavPanel.h"
#include "ui/UiHelpers.h"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

namespace {
void setStateColor(QLabel* label, bool ok)
{
    label->setStyleSheet(ok ? QStringLiteral("color: darkgreen; font-weight: bold;")
                            : QStringLiteral("color: #c00000; font-weight: bold;"));
}
} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_client(new ApiClient(this))
    , m_model(new RaceModel(m_client, this))
{
    m_modules << new ScoreboardModule(m_model, this) << new FisLiveModule(m_model, this) << new ScreenModule(m_model, this)
              << new PublishModule(m_model, this);

    buildUi();
    connect(&Log::instance(), &Log::line, m_log, &QPlainTextEdit::appendPlainText);
    Log::write(QStringLiteral("app"), tr("OpenSkiPro Connect %1, Qt %2, profile %3")
                                          .arg(QStringLiteral(OSC_VERSION), QString::fromLatin1(qVersion()), Profile::path()));

    connect(m_client, &ApiClient::log, this, [](const QString& t) { Log::write(QStringLiteral("api"), t); });
    connect(m_model, &RaceModel::connectedChanged, this, &MainWindow::updateConnection);
    connect(m_model, &RaceModel::statusChanged, this, &MainWindow::updateConnection);
    connect(m_model, &RaceModel::eventChanged, this, &MainWindow::updateConnection);
    connect(m_model, &RaceModel::timingChanged, this, [this](bool active, int run) {
        Log::write(QStringLiteral("api"), active ? tr("timing of run %1").arg(run) : tr("no timing"));
        updateConnection();
    });
    connect(m_model, &RaceModel::restarted, this, [] { Log::write(QStringLiteral("api"), tr("OpenSkiPro2 was restarted")); });
    for (Module* m : m_modules)
        connect(m, &Module::stateChanged, this, &MainWindow::updateModules);

    auto* tick = new QTimer(this);
    connect(tick, &QTimer::timeout, this, [this] {
        if (!m_model->isConnected()) {
            m_clock->setText(QStringLiteral("-"));
        } else {
            const qint64 ms = ((m_model->deviceNowMs() % 86400000) + 86400000) % 86400000;
            m_clock->setText(QTime(0, 0).addMSecs(int(ms)).toString(QStringLiteral("HH:mm:ss"))
                             + (m_model->clockSynced() ? QString() : tr("  (PC time, not synchronized)")));
        }
        updateModules();
    });
    tick->start(500);

    QSettings& s = Profile::settings();
    m_host->setText(s.value(QStringLiteral("connection/host"), QStringLiteral("localhost")).toString());
    m_port->setValue(s.value(QStringLiteral("connection/port"), 8080).toInt());
    for (Module* m : m_modules)
        if (s.value(QStringLiteral("modules/%1").arg(m->id()), false).toBool())
            m->setRunning(true);
    updateModules();
    updateConnection();
    if (s.value(QStringLiteral("connection/connected"), true).toBool())
        connectToServer();

    const QByteArray geo = s.value(QStringLiteral("window/geometry")).toByteArray();
    if (geo.isEmpty() || !restoreGeometry(geo))
        resize(1280, 820);
}

MainWindow::~MainWindow() = default;

void MainWindow::showPage(int index)
{
    if (index == 0)
        m_connectionAction->trigger();
    else if (index - 1 < m_moduleActions.size())
        m_moduleActions.at(index - 1)->trigger();
}

void MainWindow::buildUi()
{
    setWindowTitle(tr("OpenSkiPro Connect (version %1) - %2").arg(QStringLiteral(OSC_VERSION), QFileInfo(Profile::path()).fileName()));
    setWindowIcon(UiHelpers::icon(QStringLiteral("app")));
    auto* central = new QWidget(this);
    auto* lay = new QVBoxLayout(central);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // banner: the event on the left, the program on the right (as in OpenSkiPro2)
    auto* banner = new QWidget(central);
    banner->setObjectName(QStringLiteral("headerBanner"));
    banner->setAttribute(Qt::WA_StyledBackground, true);
    auto* bl = new QHBoxLayout(banner);
    bl->setContentsMargins(8, 4, 12, 4);
    auto* evIcon = new QLabel(banner);
    evIcon->setPixmap(UiHelpers::icon(QStringLiteral("connect")).pixmap(40, 40));
    bl->addWidget(evIcon);
    auto* titles = new QVBoxLayout;
    titles->setSpacing(0);
    m_title = new QLabel(banner);
    m_subtitle = new QLabel(banner);
    QFont f = m_title->font();
    f.setPointSize(14);
    f.setBold(true);
    m_title->setFont(f);
    QFont fs = m_subtitle->font();
    fs.setPointSize(10);
    fs.setBold(true);
    m_subtitle->setFont(fs);
    titles->addWidget(m_title);
    titles->addWidget(m_subtitle);
    bl->addLayout(titles);
    bl->addStretch();
    auto* logo = new QLabel(banner);
    logo->setPixmap(UiHelpers::icon(QStringLiteral("app")).pixmap(44, 44));
    bl->addWidget(logo);
    auto* brand = new QLabel(banner);
    brand->setText(QStringLiteral("<span style='font-size:20pt; font-weight:700; color:#1b5fa8;'>Open</span>"
                                  "<span style='font-size:20pt; font-weight:700; color:#222;'>SkiPro</span>"
                                  "<span style='font-size:20pt; font-weight:700; color:#d93b30;'> Connect</span><br/>"
                                  "<span style='font-size:8pt; letter-spacing:3px; color:#555;'>BOARDS - FIS LIVE - RESULTS</span>"));
    bl->addWidget(brand);
    banner->setFixedHeight(60);
    lay->addWidget(banner);

    // navigation
    auto* split = new QSplitter(Qt::Horizontal, central);
    auto* nav = new NavPanel(split);
    auto* group = new QActionGroup(this);
    m_connectionAction = new QAction(UiHelpers::icon(QStringLiteral("connect")), tr("Connection"), this);
    m_connectionAction->setCheckable(true);
    group->addAction(m_connectionAction);
    nav->addGroup(tr("OpenSkiPro2"));
    nav->addAction(m_connectionAction);
    nav->addGroup(tr("Modules"));
    for (int i = 0; i < m_modules.size(); ++i) {
        auto* a = new QAction(m_modules.at(i)->title(), this);
        a->setCheckable(true);
        group->addAction(a);
        nav->addAction(a);
        m_moduleActions << a;
    }
    nav->finish();
    auto* navScroll = new QScrollArea(split);
    navScroll->setWidget(nav);
    navScroll->setWidgetResizable(true);
    navScroll->setFrameShape(QFrame::NoFrame);
    navScroll->setMinimumWidth(240);
    navScroll->setMaximumWidth(300);

    auto* right = new QSplitter(Qt::Vertical, split);
    m_pages = new QStackedWidget(right);
    m_pages->addWidget(buildConnectionPage());
    for (int i = 0; i < m_modules.size(); ++i)
        m_pages->addWidget(buildModulePage(i));
    connect(m_connectionAction, &QAction::triggered, this, [this] { m_pages->setCurrentIndex(0); });
    for (int i = 0; i < m_moduleActions.size(); ++i)
        connect(m_moduleActions.at(i), &QAction::triggered, this, [this, i] { m_pages->setCurrentIndex(i + 1); });

    auto* logBox = new QWidget(right);
    auto* ll = new QVBoxLayout(logBox);
    ll->setContentsMargins(6, 4, 6, 6);
    ll->addWidget(UiHelpers::titleBar(tr("Log"), logBox));
    m_log = new QPlainTextEdit(logBox);
    m_log->setObjectName(QStringLiteral("log"));
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(5000);
    QFont mono(QStringLiteral("Consolas"));
    mono.setPointSize(9);
    m_log->setFont(mono);
    ll->addWidget(m_log);
    right->addWidget(m_pages);
    right->addWidget(logBox);
    right->setStretchFactor(0, 3);
    right->setStretchFactor(1, 1);

    split->addWidget(navScroll);
    split->addWidget(right);
    split->setStretchFactor(1, 1);
    lay->addWidget(split, 1);
    setCentralWidget(central);

    m_statusState = new QLabel(this);
    statusBar()->addPermanentWidget(m_statusState);

    // the first running module, else the connection
    m_connectionAction->setChecked(true);
}

QWidget* MainWindow::buildConnectionPage()
{
    auto* page = new QWidget(m_pages);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->addWidget(UiHelpers::titleBar(tr("Connection to OpenSkiPro2"), page));

    auto* form = new QFormLayout;
    form->setContentsMargins(12, 10, 12, 4);
    form->setVerticalSpacing(8);
    auto* row = new QHBoxLayout();
    m_host = new QLineEdit(page);
    m_host->setFixedWidth(260);
    m_host->setToolTip(tr("PC with OpenSkiPro2 (localhost = this PC); in OpenSkiPro2: Local configuration > API Server"));
    m_port = new QSpinBox(page);
    m_port->setRange(1, 65535);
    row->addWidget(m_host);
    row->addWidget(new QLabel(tr("Port :"), page));
    row->addWidget(m_port);
    row->addStretch();
    form->addRow(tr("Address :"), row);
    auto* buttons = new QHBoxLayout();
    m_connect = UiHelpers::button(QStringLiteral("connect"), tr("Connect"), page);
    m_disconnect = UiHelpers::button(QStringLiteral("close"), tr("Disconnect"), page);
    m_connect->setMinimumWidth(140);
    m_disconnect->setMinimumWidth(140);
    connect(m_connect, &QPushButton::clicked, this, &MainWindow::connectToServer);
    connect(m_disconnect, &QPushButton::clicked, this, &MainWindow::disconnectFromServer);
    buttons->addWidget(m_connect);
    buttons->addWidget(m_disconnect);
    buttons->addStretch();
    form->addRow(QString(), buttons);
    m_state = new QLabel(page);
    form->addRow(tr("State :"), m_state);
    m_event = new QLabel(page);
    form->addRow(tr("Event :"), m_event);
    m_run = new QLabel(page);
    form->addRow(tr("Timing :"), m_run);
    m_clock = new QLabel(page);
    QFont cf(QStringLiteral("Consolas"));
    cf.setPointSize(14);
    cf.setBold(true);
    m_clock->setFont(cf);
    form->addRow(tr("Timing device :"), m_clock);
    lay->addLayout(form);

    auto* info = UiHelpers::infoBox(page);
    info->setWordWrap(true);
    info->setText(tr("Enable the API in OpenSkiPro2 (Local configuration > API Server), enter the address of its PC "
                     "and connect. The modules on the left use the data of the event and of the timing."));
    lay->addWidget(info);
    lay->addStretch();
    return page;
}

QWidget* MainWindow::buildModulePage(int index)
{
    Module* m = m_modules.at(index);
    auto* page = new QWidget(m_pages);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->addWidget(UiHelpers::titleBar(m->title(), page));
    auto* row = new QHBoxLayout();
    auto* run = UiHelpers::button(QStringLiteral("stopwatch"), QString(), page);
    run->setMinimumWidth(140);
    connect(run, &QPushButton::clicked, this, [this, m] {
        m->setRunning(!m->isRunning());
        saveSettings();
        updateModules();
    });
    row->addWidget(run);
    auto* state = UiHelpers::infoBox(page);
    row->addWidget(state, 1);
    lay->addLayout(row);
    lay->addWidget(m->createPage(page), 1);
    m_moduleRunButtons << run;
    m_moduleStates << state;
    return page;
}

void MainWindow::connectToServer()
{
    m_client->setServer(m_host->text().trimmed(), quint16(m_port->value()));
    Log::write(QStringLiteral("api"), tr("connecting to %1:%2").arg(m_client->host()).arg(m_client->port()));
    m_client->start();
    saveSettings();
    updateConnection();
}

void MainWindow::disconnectFromServer()
{
    m_client->stop();
    saveSettings();
    updateConnection();
}

void MainWindow::updateConnection()
{
    const bool connected = m_model->isConnected();
    m_connect->setEnabled(!m_client->isStarted());
    m_disconnect->setEnabled(m_client->isStarted());
    m_host->setEnabled(!m_client->isStarted());
    m_port->setEnabled(!m_client->isStarted());
    const QString state = connected ? tr("Connected to %1:%2").arg(m_client->host()).arg(m_client->port())
        : m_client->isStarted()     ? tr("Not connected - retrying")
                                    : tr("Disconnected");
    m_state->setText(state);
    setStateColor(m_state, connected);
    m_connectionAction->setIcon(UiHelpers::icon(connected ? QStringLiteral("ball_green") : QStringLiteral("ball_red")));

    const QJsonObject ev = m_model->event();
    const QJsonObject general = ev.value(QStringLiteral("general")).toObject();
    const bool open = connected && m_model->status().value(QStringLiteral("event")).toObject().value(QStringLiteral("open")).toBool();
    const QString title1 = general.value(QStringLiteral("title1")).toString();
    const QString title2 = general.value(QStringLiteral("title2")).toString();
    m_title->setText(open ? title1 : connected ? tr("[No event]") : tr("[Not connected to OpenSkiPro2]"));
    m_subtitle->setText(open ? title2 : QString());
    m_event->setText(!connected ? QStringLiteral("-") : open ? QStringLiteral("%1 - %2").arg(title1, title2) : tr("no event open"));

    QString run = connected ? tr("timing window closed") : QStringLiteral("-");
    if (m_model->timingActive()) {
        run = tr("Run %1").arg(m_model->timedRun());
        for (const auto& r : ev.value(QStringLiteral("runs")).toArray())
            if (r.toObject().value(QStringLiteral("run")).toInt() == m_model->timedRun())
                run = r.toObject().value(QStringLiteral("name")).toString();
    }
    m_run->setText(run);
    setStateColor(m_run, m_model->timingActive());
    m_statusState->setText(state + (m_model->timingActive() ? QStringLiteral("  |  ") + run : QString()));
}

void MainWindow::updateModules()
{
    for (int i = 0; i < m_modules.size(); ++i) {
        Module* m = m_modules.at(i);
        m_moduleActions.at(i)->setIcon(UiHelpers::icon(m->isRunning() ? QStringLiteral("ball_green") : QStringLiteral("ball_red")));
        m_moduleActions.at(i)->setToolTip(m->stateText());
        m_moduleRunButtons.at(i)->setText(m->isRunning() ? tr("Stop") : tr("Start"));
        m_moduleRunButtons.at(i)->setIcon(UiHelpers::icon(m->isRunning() ? QStringLiteral("cancel") : QStringLiteral("ok")));
        m_moduleStates.at(i)->setText(m->stateText());
    }
}

void MainWindow::saveSettings()
{
    QSettings& s = Profile::settings();
    s.setValue(QStringLiteral("connection/host"), m_host->text().trimmed());
    s.setValue(QStringLiteral("connection/port"), m_port->value());
    s.setValue(QStringLiteral("connection/connected"), m_client->isStarted());
    for (Module* m : m_modules)
        s.setValue(QStringLiteral("modules/%1").arg(m->id()), m->isRunning());
    s.sync();
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    Profile::settings().setValue(QStringLiteral("window/geometry"), saveGeometry());
    saveSettings();
    for (Module* m : m_modules)
        m->setRunning(false); // boards blanked
    e->accept();
}
