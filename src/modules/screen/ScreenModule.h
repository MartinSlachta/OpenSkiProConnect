#pragma once

#include "ScreenServer.h"
#include "core/Module.h"

#include <QPointer>

class QComboBox;
class QLabel;
class QLineEdit;
class ScreenData;

// Module Screen (docs/screen-design.md 9): results on screens at the venue. A web server
// in Connect serves the TV board (/board, every TV, rotating pages) and the page of the
// speaker (/speaker); both show the configuration chosen here by hand - one per kind of
// race (*.oscreen in "screens": columns, zones of the TV, colours).
class ScreenModule : public Module {
    Q_OBJECT
public:
    explicit ScreenModule(RaceModel* model, QObject* parent = nullptr);

    QString id() const override { return QStringLiteral("screen"); }
    QString title() const override { return tr("Screen"); }
    QWidget* createPage(QWidget* parent) override;
    void setRunning(bool running) override;
    bool isRunning() const override { return m_running; }
    QString stateText() const override;

    ScreenServer* server() const { return m_server; }
    ScreenData* data() const { return m_data; }
    QString configFolder() const;
    QStringList configFiles() const;
    QString configFile() const { return m_server->configFile(); }
    void setConfigFile(const QString& file);
    // Starts Edge / Chrome in kiosk mode with a page ("board", "speaker") on a monitor
    // (index of QGuiApplication::screens()).
    bool openFullscreen(const QString& page, int screen, QString* error) const;
    QString pageUrl(const QString& page) const; // http://localhost:<port>/<page>

private:
    void load();
    void save();
    void migrateConfigs(); // configurations of 0.3 (columns per zone, speaker files)
    void installExamples();
    void fillConfigs();
    void updateBrowsers();
    void editConfiguration(QWidget* parent);

    ScreenData* m_data;
    ScreenServer* m_server;
    bool m_running = false;
    quint16 m_port = 8090;
    QString m_flags;
    QString m_error;

    QPointer<QComboBox> m_config;
    QPointer<QLabel> m_urls;
    QPointer<QLabel> m_boardInfo;
    QPointer<QLabel> m_speakerInfo;
    QPointer<QLineEdit> m_message;
    QPointer<QComboBox> m_monitor;
};
