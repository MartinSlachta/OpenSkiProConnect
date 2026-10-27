#pragma once

#include <QList>
#include <QWidget>

class QAction;
class QVBoxLayout;

// Left navigation panel of the main window (SkiPro: Parameters / Competitors
// / Timing / Edit groups). Buttons mirror QActions shared with the menus.
class NavPanel : public QWidget {
    Q_OBJECT
public:
    explicit NavPanel(QWidget* parent = nullptr);

    void addGroup(const QString& title);
    void addAction(QAction* action, int indent = 0);
    void addSeparator();
    void finish();

private:
    QVBoxLayout* m_layout;
};
