#include "UiHelpers.h"

#include <QApplication>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTableView>

namespace {

// Text of the cells a few pixels away from the cell borders (as in OpenSkiPro2).
class PaddedDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    static constexpr int kPad = 4;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem bg(option);
        initStyleOption(&bg, index);
        const QWidget* w = option.widget;
        QStyle* style = w ? w->style() : QApplication::style();
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &bg, painter, w);
        QStyleOptionViewItem inner(option);
        inner.rect.adjust(kPad, 0, -kPad, 0);
        QStyledItemDelegate::paint(painter, inner, index);
    }
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        return QStyledItemDelegate::sizeHint(option, index) + QSize(2 * kPad, 0);
    }
};

} // namespace

namespace UiHelpers {

QIcon icon(const QString& name)
{
    return QIcon(QStringLiteral(":/icons/%1.svg").arg(name));
}

QPushButton* button(const QString& iconName, const QString& text, QWidget* parent)
{
    auto* b = new QPushButton(icon(iconName), text, parent);
    b->setIconSize(QSize(22, 22));
    b->setMinimumHeight(34);
    return b;
}

QLabel* titleBar(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("titleBar"));
    l->setMinimumHeight(22);
    l->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    return l;
}

QLabel* infoBox(QWidget* parent)
{
    auto* l = new QLabel(parent);
    l->setObjectName(QStringLiteral("infoBox"));
    l->setMinimumHeight(28);
    return l;
}

void styleGrid(QTableView* view)
{
    view->setAlternatingRowColors(true);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->verticalHeader()->setVisible(false);
    view->verticalHeader()->setDefaultSectionSize(24);
    view->horizontalHeader()->setHighlightSections(false);
    view->setWordWrap(false);
    view->setItemDelegate(new PaddedDelegate(view));
}

QString appStyleSheet()
{
    // the style sheet of OpenSkiPro2 (src/ui/UiHelpers.cpp there)
    return QStringLiteral(
        "QLabel#titleBar { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #dce8f6, stop:1 #a9c4e6);"
        "  border: 1px solid #3c5f8c; border-radius: 2px; padding: 1px 6px; font-weight: bold; font-size: 10pt; color: #10233d; }"
        "QLabel#infoBox { background: #ffffe1; border: 1px solid #b9b98a; border-radius: 2px; font-weight: bold; padding: 4px; }"
        "QWidget#navPanel { background: #bcd4ee; }"
        "QLabel#navHeader { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #e3edf8, stop:1 #b2cae8);"
        "  border: 1px solid #2f4f7a; border-radius: 2px; font-weight: bold; font-size: 10pt; padding: 1px 4px; color: #0b1d33; }"
        "QToolButton#navButton { border: none; padding: 3px 4px; text-align: left; color: #10233d; }"
        "QToolButton#navButton:hover { color: #0040c0; text-decoration: underline; }"
        "QToolButton#navButton:checked { font-weight: bold; color: #0040c0; }"
        "QToolButton#navButton:disabled { color: #7a8ca3; }"
        "QWidget#headerBanner { background: white; border-bottom: 2px solid #3c5f8c; }"
        "QTableView { gridline-color: #c8d3e0; selection-background-color: #3d6fb4; alternate-background-color: #eef4fb; }"
        "QPlainTextEdit#log { background: #ffffe1; }"
        "QHeaderView::section { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #f4f7fb, stop:1 #d5e0ee);"
        "  border: 1px solid #a7b8cf; padding: 2px 4px; }");
}

} // namespace UiHelpers
