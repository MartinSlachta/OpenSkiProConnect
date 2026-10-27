#pragma once

#include "PublishClient.h"
#include "core/Module.h"

#include <QPointer>

class QLabel;
class QTimer;
class ScreenData;

// Module Publish (docs/publish-design.md): the race open in OpenSkiPro2 live on the
// internet through the OpenSkiPro Publish server. Fully automatic: the token is in the
// event (OpenSkiPro2: General Parameters > Publish token), the server is a setting here;
// the rows are those of the screens with the public competitor fields of the event,
// the pages of the server arrange the columns themselves (docs/fields-design.md).
class PublishModule : public Module {
    Q_OBJECT
public:
    explicit PublishModule(RaceModel* model, QObject* parent = nullptr);

    QString id() const override { return QStringLiteral("publish"); }
    QString title() const override { return tr("Publish"); }
    QWidget* createPage(QWidget* parent) override;
    void setRunning(bool running) override;
    bool isRunning() const override { return m_running; }
    QString stateText() const override;

    PublishClient* client() const { return m_client; }
    ScreenData* data() const { return m_data; }
    static QString defaultServer() { return QStringLiteral("https://publish.slachta.sk"); }
    QString serverText() const { return m_server; }
    void setServer(const QString& url);
    QString eventToken() const;

    // Builds the sections now (tests; normally after a short delay).
    void rebuild();

private:
    void load();
    void save();
    void updateTarget();
    void schedule();
    void editSettings(QWidget* parent);

    ScreenData* m_data;
    PublishClient* m_client;
    QTimer* m_timer;
    bool m_running = false;
    QString m_server;

    QPointer<QLabel> m_token;
    QPointer<QLabel> m_race;
    QPointer<QLabel> m_serverInfo;
    QPointer<QLabel> m_status;
};
