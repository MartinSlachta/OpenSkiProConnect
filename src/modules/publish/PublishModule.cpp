#include "PublishModule.h"

#include "PublishSections.h"
#include "ScreenData.h"
#include "core/Log.h"
#include "core/Profile.h"
#include "core/RaceModel.h"
#include "ui/UiHelpers.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QString ball(bool ok)
{
    return QStringLiteral("<img src=\":/icons/%1.svg\" width=\"14\" height=\"14\"> ").arg(ok ? QStringLiteral("ball_green") : QStringLiteral("ball_red"));
}

} // namespace

PublishModule::PublishModule(RaceModel* model, QObject* parent)
    : Module(model, parent)
    , m_data(new ScreenData(model, this))
    , m_client(new PublishClient(this))
    , m_timer(new QTimer(this))
{
    load();
    m_client->setClockSource([model] {
        return QJsonObject{ { QStringLiteral("dayMs"), model->deviceNowMs() }, { QStringLiteral("synced"), model->clockSynced() } };
    });
    connect(m_client, &PublishClient::log, this, [](const QString& t) { Log::write(QStringLiteral("publish"), t); });
    connect(m_client, &PublishClient::stateChanged, this, &Module::stateChanged);

    // several messages of one change give one build; the client adds its own 0.5 s
    m_timer->setSingleShot(true);
    m_timer->setInterval(150);
    connect(m_timer, &QTimer::timeout, this, &PublishModule::rebuild);
    connect(m_data, &ScreenData::stateChanged, this, &PublishModule::schedule);
    connect(model, &RaceModel::resourceChanged, this, &PublishModule::schedule);
    connect(model, &RaceModel::eventChanged, this, [this] {
        updateTarget();
        schedule();
    });
    connect(model, &RaceModel::connectedChanged, this, [this](bool connected) {
        // without OpenSkiPro2 nothing is sent: the pages show "data from ..." after 30 s
        if (!m_running)
            return;
        if (connected)
            m_client->start();
        else
            m_client->stop();
        Q_EMIT stateChanged();
    });
    updateTarget();
}

void PublishModule::load()
{
    QSettings& s = Profile::settings();
    s.beginGroup(id());
    m_server = s.value(QStringLiteral("server"), defaultServer()).toString();
    s.remove(QStringLiteral("config")); // 0.5: the columns came from a screen configuration
    s.endGroup();
}

void PublishModule::save()
{
    QSettings& s = Profile::settings();
    s.beginGroup(id());
    s.setValue(QStringLiteral("server"), m_server);
    s.endGroup();
    s.sync();
}

void PublishModule::setServer(const QString& url)
{
    m_server = url.trimmed();
    save();
    updateTarget();
}

QString PublishModule::eventToken() const
{
    const QJsonObject general = model()->event().value(QStringLiteral("general")).toObject();
    return general.value(QStringLiteral("publish")).toObject().value(QStringLiteral("token")).toString().trimmed();
}

void PublishModule::updateTarget()
{
    const QJsonObject general = model()->event().value(QStringLiteral("general")).toObject();
    const QString token = eventToken();
    const QString date = general.value(QStringLiteral("date")).toString();
    if (token != m_client->token() && m_running)
        Log::write(QStringLiteral("publish"), token.isEmpty() ? tr("the event has no publish token") : tr("publish token of the event: %1...").arg(token.left(6)));
    m_client->setTarget(QUrl(m_server), token, date);
    Q_EMIT stateChanged();
}

void PublishModule::schedule()
{
    if (m_running && !m_timer->isActive())
        m_timer->start();
}

void PublishModule::rebuild()
{
    m_timer->stop();
    m_data->rebuild();
    m_client->setSections(PublishSections::build(model(), m_data));
}

void PublishModule::setRunning(bool running)
{
    if (running == m_running)
        return;
    m_running = running;
    if (running) {
        if (eventToken().isEmpty())
            Log::write(QStringLiteral("publish"), tr("the event has no publish token (OpenSkiPro2: Event > General Parameters > Publish token)"));
        rebuild();
        if (model()->isConnected())
            m_client->start();
    } else {
        m_timer->stop();
        m_client->stop();
    }
    Q_EMIT stateChanged();
}

QString PublishModule::stateText() const
{
    if (!m_running)
        return tr("stopped");
    if (m_client->token().isEmpty())
        return tr("no publish token in the event");
    if (!model()->isConnected())
        return tr("waiting for OpenSkiPro2");
    switch (m_client->state()) {
    case PublishClient::State::Idle:
        return m_client->message().isEmpty() ? tr("waiting") : m_client->message();
    case PublishClient::State::Checking:
        return tr("checking the token");
    case PublishClient::State::Refused:
        return tr("refused: %1").arg(m_client->message());
    case PublishClient::State::Retrying:
        return m_client->message();
    case PublishClient::State::Ready:
    case PublishClient::State::Sending:
        break;
    }
    return m_client->lastAccepted().isValid() ? tr("published %1 - %2").arg(m_client->lastAccepted().toString(QStringLiteral("HH:mm:ss")), m_client->race().title)
                                              : tr("publishing");
}

void PublishModule::editSettings(QWidget* parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(tr("Publish - settings"));
    auto* lay = new QVBoxLayout(&dlg);
    auto* form = new QFormLayout;
    auto* server = new QLineEdit(m_server, &dlg);
    server->setMinimumWidth(320);
    server->setPlaceholderText(defaultServer());
    form->addRow(tr("Server :"), server);
    lay->addLayout(form);
    auto* note = new QLabel(tr("Address of the OpenSkiPro Publish server. The token of each race is entered in "
                               "OpenSkiPro2 (Event > General Parameters > Publish token)."),
                            &dlg);
    note->setWordWrap(true);
    lay->addWidget(note);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, &dlg);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(box->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, server, [server] { server->setText(defaultServer()); });
    lay->addWidget(box);
    dlg.resize(560, 0);
    if (dlg.exec() != QDialog::Accepted)
        return;
    setServer(server->text().trimmed().isEmpty() ? defaultServer() : server->text().trimmed());
}

QWidget* PublishModule::createPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* lay = new QVBoxLayout(page);

    auto* group = new QGroupBox(tr("Race on the internet"), page);
    auto* form = new QFormLayout(group);
    m_token = new QLabel(group);
    m_token->setWordWrap(true);
    form->addRow(tr("Token :"), m_token);

    auto* raceRow = new QHBoxLayout();
    m_race = new QLabel(group);
    m_race->setOpenExternalLinks(true);
    m_race->setTextInteractionFlags(Qt::TextBrowserInteraction);
    raceRow->addWidget(m_race, 1);
    auto* copy = UiHelpers::button(QStringLiteral("export"), tr("Copy address"), group);
    copy->setToolTip(tr("Copies the address of the race page (for your website, a mail, a QR code)."));
    connect(copy, &QPushButton::clicked, this, [this] {
        if (!m_client->race().url.isEmpty())
            QApplication::clipboard()->setText(m_client->race().url);
    });
    raceRow->addWidget(copy);
    form->addRow(tr("Race page :"), raceRow);

    auto* srvRow = new QHBoxLayout();
    m_serverInfo = new QLabel(group);
    srvRow->addWidget(m_serverInfo);
    srvRow->addSpacing(16);
    auto* settingsBtn = UiHelpers::button(QStringLiteral("params"), tr("Settings..."), group);
    connect(settingsBtn, &QPushButton::clicked, this, [this, page] { editSettings(page); });
    srvRow->addWidget(settingsBtn);
    srvRow->addStretch();
    form->addRow(tr("Server :"), srvRow);

    m_status = new QLabel(group);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* statusRow = new QHBoxLayout();
    statusRow->addWidget(m_status, 1);
    auto* again = UiHelpers::button(QStringLiteral("export"), tr("Send again"), group);
    again->setToolTip(tr("Checks the token again and sends the whole race (after an error has been put right)."));
    connect(again, &QPushButton::clicked, this, [this] {
        Log::write(QStringLiteral("publish"), tr("send again"));
        rebuild();
        m_client->sendAgain();
    });
    statusRow->addWidget(again);
    form->addRow(tr("State :"), statusRow);
    lay->addWidget(group);

    auto show = [this] {
        if (!m_token)
            return;
        const QString token = m_client->token();
        if (token.isEmpty())
            m_token->setText(ball(false) + tr("<b>none</b> - enter it in OpenSkiPro2 (Event > General Parameters > Publish token)"));
        else {
            QString state = tr("not checked yet");
            if (m_client->tokenState() == QLatin1String("free"))
                state = tr("free - it will be used for this race at the first send");
            else if (m_client->tokenState() == QLatin1String("bound"))
                state = tr("bound to this race");
            m_token->setText(ball(m_client->state() != PublishClient::State::Refused) + QStringLiteral("<b>%1...</b> - %2").arg(token.left(6).toHtmlEscaped(), state));
        }
        const PublishClient::Race race = m_client->race();
        m_race->setText(race.url.isEmpty() ? QStringLiteral("-")
                                           : QStringLiteral("<a href=\"%1\">%1</a>  %2 %3").arg(race.url.toHtmlEscaped(), race.title.toHtmlEscaped(), race.date));
        m_serverInfo->setText(m_server.toHtmlEscaped());
        QString st;
        const QString msg = m_client->message().toHtmlEscaped();
        switch (m_client->state()) {
        case PublishClient::State::Refused:
            st = ball(false) + QStringLiteral("<span style=\"color:#c00000\"><b>%1</b></span>").arg(msg);
            break;
        case PublishClient::State::Retrying:
            st = ball(false) + msg;
            break;
        default:
            st = ball(m_running && model()->isConnected() && !token.isEmpty()) + stateText().toHtmlEscaped();
            break;
        }
        if (m_client->acceptedCount() > 0)
            st += tr("<br>%1 requests accepted, %2 kB sent").arg(m_client->acceptedCount()).arg((m_client->bytesSent() + 512) / 1024);
        m_status->setText(st);
    };
    connect(this, &Module::stateChanged, m_token, show);
    connect(model(), &RaceModel::connectedChanged, m_token, show);
    show();

    auto* autoInfo = UiHelpers::infoBox(page);
    autoInfo->setWordWrap(true);
    autoInfo->setText(tr("Automatic: once the module runs and the event has a publish token, the race is published by "
                         "itself - start lists, live data (on start, on course, finish), results of every run and the "
                         "total, corrections and DNS / DNF / DSQ also after the timing. The competitor fields shown are those "
                         "marked Public in OpenSkiPro2 (Competitor Fields). One token = one race: it is bound "
                         "to the race at the first send, then the race can be sent again any time. Tokens are in your "
                         "account on the server."));
    lay->addWidget(autoInfo);
    lay->addStretch();
    return page;
}
