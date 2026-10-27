#pragma once

#include "core/Module.h"
#include "displayconfig.h"

#include <QPointer>
#include <QVector>

class BoardContentController;
class BoardProgramEngine;
class DisplayDriver;
class QComboBox;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;
class QWidget;

// Module Scoreboard (docs/design.md 3): results boards, a port of
// SkiAlpBridgeDisplay - displays with their own TCP driver and their Timing /
// Outside-timing line programs, all fed by one BoardContentController.
class ScoreboardModule : public Module {
    Q_OBJECT
public:
    explicit ScoreboardModule(RaceModel* model, QObject* parent = nullptr);
    ~ScoreboardModule() override;

    QString id() const override { return QStringLiteral("scoreboard"); }
    QString title() const override { return tr("Scoreboard"); }
    QWidget* createPage(QWidget* parent) override;
    void setRunning(bool running) override;
    bool isRunning() const override { return m_running; }
    QString stateText() const override;

    BoardContentController* content() const { return m_content; }
    // Displays and settings of a SkiAlpBridgeDisplay.ini, appended to ours.
    bool importSkiAlpBridgeDisplay(const QString& iniPath, QString* error);

private:
    void load();
    void save();
    void startDisplay(int index);
    void stopDisplay(int index);
    void refreshTable();
    void refreshStatus();
    int selectedRow() const;
    void addDisplay();
    void removeDisplay();
    void editDisplay();
    void editLines();
    void import();

    BoardContentController* m_content;
    QVector<DisplayConfig> m_configs;
    // index-aligned with m_configs, null while the module is stopped
    QVector<QPointer<DisplayDriver>> m_drivers;
    QVector<QPointer<BoardProgramEngine>> m_engines;
    bool m_running = false;

    QPointer<QTableWidget> m_table;
    QPointer<QComboBox> m_rankField;
    QPointer<QSpinBox> m_finishHold;
    QPointer<QSpinBox> m_otherHold;
    QTimer* m_statsTimer;
};
