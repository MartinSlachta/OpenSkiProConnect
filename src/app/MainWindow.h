#pragma once

#include <QList>
#include <QMainWindow>

class ApiClient;
class Module;
class QAction;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class RaceModel;

// Shell of OpenSkiPro Connect (docs/design.md 2) with the look of OpenSkiPro2:
// banner with the event, blue navigation (connection, modules), a page each,
// the log at the bottom.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    void showPage(int index); // 0 connection, 1.. modules

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void buildUi();
    QWidget* buildConnectionPage();
    QWidget* buildModulePage(int index);
    void connectToServer();
    void disconnectFromServer();
    void updateConnection();
    void updateModules();
    void saveSettings();

    ApiClient* m_client;
    RaceModel* m_model;
    QList<Module*> m_modules;

    QLabel* m_title = nullptr;    // banner: event
    QLabel* m_subtitle = nullptr;
    QLineEdit* m_host = nullptr;
    QSpinBox* m_port = nullptr;
    QPushButton* m_connect = nullptr;
    QPushButton* m_disconnect = nullptr;
    QLabel* m_state = nullptr;
    QLabel* m_event = nullptr;
    QLabel* m_run = nullptr;
    QLabel* m_clock = nullptr;
    QLabel* m_statusState = nullptr; // status bar
    QAction* m_connectionAction = nullptr;
    QList<QAction*> m_moduleActions;
    QList<QPushButton*> m_moduleRunButtons;
    QList<QLabel*> m_moduleStates;
    QStackedWidget* m_pages = nullptr;
    QPlainTextEdit* m_log = nullptr;
};
