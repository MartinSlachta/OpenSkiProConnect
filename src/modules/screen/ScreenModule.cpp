#include "ScreenModule.h"
#include "ScreenData.h"
#include "ScreenEditor.h"

#include "core/Log.h"
#include "core/Profile.h"
#include "ui/UiHelpers.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

ScreenModule::ScreenModule(RaceModel* model, QObject* parent)
    : Module(model, parent)
    , m_data(new ScreenData(model, this))
    , m_server(new ScreenServer(m_data, this))
{
    migrateConfigs();
    installExamples();
    load();
    connect(m_server, &ScreenServer::clientsChanged, this, [this] {
        updateBrowsers();
        Q_EMIT stateChanged();
    });
}

QString ScreenModule::configFolder() const
{
    return QDir(Profile::folder()).filePath(QStringLiteral("screens"));
}

QStringList ScreenModule::configFiles() const
{
    return QDir(configFolder()).entryList({ QStringLiteral("*.oscreen") }, QDir::Files, QDir::Name);
}

// Configurations of 0.3 (columns per zone, a speaker configuration, a configuration per
// board): copied to screens/0.3-backup; the delivered examples and the speaker files go
// (the new examples come instead), an own configuration of the user is converted (the
// columns of its ranking zone).
void ScreenModule::migrateConfigs()
{
    const QDir dir(configFolder());
    QStringList examples;
    for (const auto& e : ScreenConfig::examples())
        examples << e.first;
    examples << QStringLiteral("Speaker.oscreen");
    for (const QString& f : configFiles()) {
        QFile file(dir.filePath(f));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        if (!ScreenConfig::isVersion1(o))
            continue;
        dir.mkpath(QStringLiteral("0.3-backup"));
        const QString backup = dir.filePath(QStringLiteral("0.3-backup/") + f);
        if (!QFileInfo::exists(backup))
            QFile::copy(dir.filePath(f), backup);
        if (examples.contains(f) || o.value(QStringLiteral("kind")).toString() == QLatin1String("speaker")) {
            QFile::remove(dir.filePath(f));
        } else {
            ScreenConfig::fromJson(o).save(dir.filePath(f), nullptr);
        }
        Log::write(QStringLiteral("screen"), tr("configuration %1 of 0.3 converted (the old one in screens/0.3-backup)").arg(f));
    }
}

// The delivered examples, written once (the user's changes are never overwritten).
void ScreenModule::installExamples()
{
    QDir().mkpath(QDir(configFolder()).filePath(QStringLiteral("images")));
    for (const auto& e : ScreenConfig::examples()) {
        const QString path = QDir(configFolder()).filePath(e.first);
        if (!QFileInfo::exists(path))
            e.second.save(path, nullptr);
    }
}

void ScreenModule::load()
{
    QSettings& s = Profile::settings();
    s.beginGroup(id());
    m_port = quint16(s.value(QStringLiteral("port"), 8090).toUInt());
    m_flags = s.value(QStringLiteral("flags"), QDir(Profile::folder()).filePath(QStringLiteral("flags"))).toString();
    QString config = s.value(QStringLiteral("config")).toString();
    if (config.isEmpty() && s.contains(QStringLiteral("boards/size"))) {
        // 0.3: a configuration per board - the one of the first board (for FIS events if it had one)
        const int n = s.beginReadArray(QStringLiteral("boards"));
        for (int i = 0; i < n && config.isEmpty(); ++i) {
            s.setArrayIndex(i);
            const QString fis = s.value(QStringLiteral("fisConfig")).toString();
            const QString plain = s.value(QStringLiteral("config")).toString();
            for (const QString& f : { fis, plain })
                if (config.isEmpty() && !f.isEmpty() && QFileInfo::exists(QDir(configFolder()).filePath(f)))
                    config = f;
        }
        s.endArray();
    }
    s.remove(QStringLiteral("boards"));
    s.endGroup();
    if (config.isEmpty() || !QFileInfo::exists(QDir(configFolder()).filePath(config)))
        config = configFiles().contains(QStringLiteral("FIS race.oscreen")) ? QStringLiteral("FIS race.oscreen") : configFiles().value(0);
    m_server->setFolders(configFolder(), m_flags);
    m_server->setConfigFile(config);
    save();
}

void ScreenModule::save()
{
    QSettings& s = Profile::settings();
    s.beginGroup(id());
    s.setValue(QStringLiteral("port"), int(m_port));
    s.setValue(QStringLiteral("flags"), m_flags);
    s.setValue(QStringLiteral("config"), m_server->configFile());
    s.endGroup();
    s.sync();
}

void ScreenModule::setConfigFile(const QString& file)
{
    if (file == m_server->configFile())
        return;
    m_server->setConfigFile(file);
    save();
    Log::write(QStringLiteral("screen"), tr("configuration %1").arg(QFileInfo(file).completeBaseName()));
}

void ScreenModule::setRunning(bool running)
{
    if (running == m_running)
        return;
    m_error.clear();
    if (running) {
        QString err;
        if (!m_server->start(m_port, &err)) {
            m_error = tr("port %1: %2").arg(m_port).arg(err);
            Log::write(QStringLiteral("screen"), tr("cannot start the web server: %1").arg(m_error));
            Q_EMIT stateChanged();
            return;
        }
        Log::write(QStringLiteral("screen"), tr("web server on port %1").arg(m_port));
    } else {
        m_server->stop();
        Log::write(QStringLiteral("screen"), tr("web server stopped"));
    }
    m_running = running;
    updateBrowsers();
    Q_EMIT stateChanged();
}

QString ScreenModule::stateText() const
{
    if (!m_error.isEmpty())
        return tr("Error: %1").arg(m_error);
    if (!m_running)
        return tr("Stopped");
    return tr("Web server on port %1 - %n browser(s)", nullptr, m_server->clientCount()).arg(m_port);
}

QString ScreenModule::pageUrl(const QString& page) const
{
    return QStringLiteral("http://localhost:%1/%2").arg(m_port).arg(page);
}

bool ScreenModule::openFullscreen(const QString& page, int screen, QString* error) const
{
    QStringList browsers;
    for (const QString& base : { qEnvironmentVariable("ProgramFiles(x86)"), qEnvironmentVariable("ProgramFiles"), qEnvironmentVariable("LOCALAPPDATA") }) {
        if (base.isEmpty())
            continue;
        browsers << QDir(base).filePath(QStringLiteral("Microsoft/Edge/Application/msedge.exe"))
                 << QDir(base).filePath(QStringLiteral("Google/Chrome/Application/chrome.exe"));
    }
    QString exe;
    for (const QString& b : browsers)
        if (QFileInfo::exists(b)) {
            exe = b;
            break;
        }
    if (exe.isEmpty()) {
        if (error)
            *error = tr("Neither Microsoft Edge nor Google Chrome was found.");
        return false;
    }
    const QList<QScreen*> screens = QGuiApplication::screens();
    const QRect g = screens.value(qBound(0, screen, int(screens.size()) - 1))->geometry();
    // an own profile per page and monitor: a new window on the right monitor, not a tab of a running browser
    const QString profile = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                .filePath(QStringLiteral("osc-kiosk-%1-%2").arg(QString(page).replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("_"))).arg(screen));
    QStringList args{ QStringLiteral("--kiosk"), pageUrl(page), QStringLiteral("--no-first-run"),
                      QStringLiteral("--window-position=%1,%2").arg(g.x()).arg(g.y()),
                      QStringLiteral("--user-data-dir=") + QDir::toNativeSeparators(profile) };
    if (exe.contains(QLatin1String("msedge")))
        args << QStringLiteral("--edge-kiosk-type=fullscreen");
    if (!QProcess::startDetached(exe, args)) {
        if (error)
            *error = tr("Cannot start %1.").arg(exe);
        return false;
    }
    return true;
}

void ScreenModule::updateBrowsers()
{
    if (m_urls) {
        QStringList links;
        for (const QString& u : m_server->urls())
            links << QStringLiteral("<a href=\"%1\">%1</a>").arg(u);
        m_urls->setText(m_running ? links.join(QStringLiteral("&nbsp;&nbsp;&nbsp;")) : tr("the web server is stopped"));
    }
    auto info = [this](QLabel* l, const QString& page) {
        if (l)
            l->setText(m_running ? tr("%n browser(s)", nullptr, m_server->clientCount(page)) : QStringLiteral("-"));
    };
    info(m_boardInfo, QStringLiteral("board"));
    info(m_speakerInfo, QStringLiteral("speaker"));
}

void ScreenModule::fillConfigs()
{
    if (!m_config)
        return;
    m_config->blockSignals(true);
    m_config->clear();
    for (const QString& f : configFiles())
        m_config->addItem(QFileInfo(f).completeBaseName(), f);
    const int i = m_config->findData(m_server->configFile());
    if (i < 0 && !m_server->configFile().isEmpty())
        m_config->addItem(tr("%1 (missing)").arg(m_server->configFile()), m_server->configFile());
    m_config->setCurrentIndex(qMax(0, m_config->findData(m_server->configFile())));
    m_config->blockSignals(false);
}

void ScreenModule::editConfiguration(QWidget* parent)
{
    ScreenEditor dlg(this, m_server->configFile(), parent);
    dlg.exec();
    if (!configFiles().contains(m_server->configFile())) // deleted or renamed in the editor
        setConfigFile(configFiles().value(0));
    m_server->reloadConfigs();
    fillConfigs();
}

QWidget* ScreenModule::createPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* lay = new QVBoxLayout(page);

    auto* info = UiHelpers::infoBox(page);
    info->setWordWrap(true);
    info->setText(tr("Every screen is a web browser (PC, smart TV, Raspberry Pi, tablet). Every TV opens the TV board "
                     "http://<this PC>:<port>/board - it pages the ranking by itself and changes with the race (start list "
                     "before, live during the timing, results after). The speaker opens /speaker: everything at once, "
                     "nothing redraws under him, he scrolls the results himself. Both show the configuration chosen below "
                     "(one per kind of race): the same columns everywhere."));
    lay->addWidget(info);

    auto* server = new QGroupBox(tr("Web server"), page);
    auto* form = new QFormLayout(server);
    auto* portRow = new QHBoxLayout();
    auto* port = new QSpinBox(server);
    port->setRange(1, 65535);
    port->setValue(m_port);
    portRow->addWidget(port);
    portRow->addStretch();
    connect(port, &QSpinBox::editingFinished, this, [this, port] {
        if (port->value() == m_port)
            return;
        m_port = quint16(port->value());
        save();
        if (m_running) { // listen on the new port
            setRunning(false);
            setRunning(true);
        }
    });
    form->addRow(tr("Port :"), portRow);
    m_urls = new QLabel(server);
    m_urls->setOpenExternalLinks(true);
    m_urls->setTextInteractionFlags(Qt::TextBrowserInteraction);
    form->addRow(tr("Addresses :"), m_urls);
    auto* flagsRow = new QHBoxLayout();
    auto* flags = new QLineEdit(m_flags, server);
    flags->setToolTip(tr("Folder with the flags of the nations, one file per code: SVK.png, CZE.svg ... (png, svg, gif, jpg, bmp)"));
    flagsRow->addWidget(flags, 1);
    auto* browse = UiHelpers::button(QStringLiteral("folder"), QString(), server);
    flagsRow->addWidget(browse);
    auto setFlags = [this, flags] {
        m_flags = flags->text().trimmed();
        m_server->setFolders(configFolder(), m_flags);
        save();
    };
    connect(flags, &QLineEdit::editingFinished, this, setFlags);
    connect(browse, &QPushButton::clicked, this, [flags, setFlags, page] {
        const QString d = QFileDialog::getExistingDirectory(page, tr("Folder of the flags"), flags->text());
        if (!d.isEmpty()) {
            flags->setText(QDir::toNativeSeparators(d));
            setFlags();
        }
    });
    form->addRow(tr("Flags :"), flagsRow);
    lay->addWidget(server);

    auto* screens = new QGroupBox(tr("Screens"), page);
    auto* grid = new QGridLayout(screens);
    grid->addWidget(new QLabel(tr("Configuration :"), screens), 0, 0);
    auto* cfgRow = new QHBoxLayout();
    m_config = new QComboBox(screens);
    m_config->setMinimumWidth(240);
    m_config->setToolTip(tr("The kind of race: which columns every screen shows, the zones and colours of the TV."));
    connect(m_config, &QComboBox::activated, this, [this] { setConfigFile(m_config->currentData().toString()); });
    cfgRow->addWidget(m_config);
    auto* edit = UiHelpers::button(QStringLiteral("modify"), tr("Edit configuration..."), screens);
    edit->setToolTip(tr("Columns, zones of the TV, colours and paging - also new configurations (Save as...)."));
    connect(edit, &QPushButton::clicked, this, [this, page] { editConfiguration(page); });
    cfgRow->addWidget(edit);
    cfgRow->addStretch();
    grid->addLayout(cfgRow, 0, 1, 1, 5);

    m_monitor = new QComboBox(screens);
    const QList<QScreen*> monitors = QGuiApplication::screens();
    for (int i = 0; i < monitors.size(); ++i)
        m_monitor->addItem(tr("Monitor %1 (%2x%3)").arg(i + 1).arg(monitors.at(i)->geometry().width()).arg(monitors.at(i)->geometry().height()));
    m_monitor->setCurrentIndex(monitors.size() > 1 ? 1 : 0);
    auto pageRow = [this, screens, grid, page](int row, const QString& label, const QString& path, QPointer<QLabel>& count) {
        grid->addWidget(new QLabel(label, screens), row, 0);
        auto* address = new QLabel(QStringLiteral("<b>/%1</b>").arg(path), screens);
        grid->addWidget(address, row, 1);
        count = new QLabel(screens);
        grid->addWidget(count, row, 2);
        auto* open = UiHelpers::button(QStringLiteral("preview"), tr("Open in browser"), screens);
        connect(open, &QPushButton::clicked, this, [this, page, path] {
            if (!m_running)
                QMessageBox::information(page, tr("Screen"), tr("Start the module first (the web server)."));
            else
                QDesktopServices::openUrl(QUrl(pageUrl(path)));
        });
        grid->addWidget(open, row, 3);
        auto* full = UiHelpers::button(QStringLiteral("landscape"), tr("Fullscreen on"), screens);
        full->setToolTip(tr("Starts Microsoft Edge (or Chrome) in kiosk mode with the page on the chosen monitor (Alt+F4 closes it)."));
        connect(full, &QPushButton::clicked, this, [this, page, path] {
            QString err;
            if (!m_running)
                QMessageBox::information(page, tr("Screen"), tr("Start the module first (the web server)."));
            else if (!openFullscreen(path, m_monitor->currentIndex(), &err))
                QMessageBox::warning(page, tr("Screen"), err);
        });
        grid->addWidget(full, row, 4);
    };
    pageRow(1, tr("TV board :"), QStringLiteral("board"), m_boardInfo);
    pageRow(2, tr("Speaker :"), QStringLiteral("speaker"), m_speakerInfo);
    grid->addWidget(m_monitor, 1, 5, 2, 1, Qt::AlignVCenter);
    grid->setColumnStretch(6, 1);

    auto* msgRow = new QHBoxLayout();
    m_message = new QLineEdit(m_server->message(), screens);
    m_message->setPlaceholderText(tr("e.g. Start delayed by 10 minutes"));
    msgRow->addWidget(m_message, 1);
    auto* show = UiHelpers::button(QStringLiteral("comment"), tr("Show"), screens);
    connect(show, &QPushButton::clicked, this, [this] { m_server->setMessage(m_message->text().trimmed()); });
    msgRow->addWidget(show);
    auto* hide = UiHelpers::button(QStringLiteral("cancel"), tr("Hide"), screens);
    connect(hide, &QPushButton::clicked, this, [this] { m_server->setMessage(QString()); });
    msgRow->addWidget(hide);
    grid->addWidget(new QLabel(tr("Message :"), screens), 3, 0);
    grid->addLayout(msgRow, 3, 1, 1, 6);
    lay->addWidget(screens);
    lay->addStretch(1);

    fillConfigs();
    updateBrowsers();
    return page;
}
