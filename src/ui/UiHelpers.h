#pragma once

#include <QIcon>
#include <QString>

class QLabel;
class QPushButton;
class QTableView;
class QWidget;

// The look of OpenSkiPro2 (blue title bars and navigation, big icon buttons,
// yellow info boxes) - the same helpers and style sheet, so both programs look alike.
namespace UiHelpers {

QIcon icon(const QString& name);
QPushButton* button(const QString& iconName, const QString& text, QWidget* parent = nullptr);
QLabel* titleBar(const QString& text, QWidget* parent = nullptr); // blue gradient bar
QLabel* infoBox(QWidget* parent = nullptr);                       // yellow box with bold text
void styleGrid(QTableView* view);
QString appStyleSheet();

} // namespace UiHelpers
