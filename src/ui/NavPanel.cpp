#include "NavPanel.h"

#include <QAction>
#include <QFrame>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

NavPanel::NavPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("navPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(3, 3, 3, 3);
    m_layout->setSpacing(1);
    setMinimumWidth(230);
}

void NavPanel::addGroup(const QString& title)
{
    if (m_layout->count() > 0)
        m_layout->addSpacing(4);
    auto* l = new QLabel(title, this);
    l->setObjectName(QStringLiteral("navHeader"));
    m_layout->addWidget(l);
}

void NavPanel::addAction(QAction* action, int indent)
{
    auto* b = new QToolButton(this);
    b->setObjectName(QStringLiteral("navButton"));
    b->setDefaultAction(action);
    b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    b->setIconSize(QSize(22, 22));
    b->setAutoRaise(true);
    b->setCursor(Qt::PointingHandCursor);
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* row = new QHBoxLayout;
    row->setContentsMargins(12 + indent * 14, 0, 0, 0);
    row->addWidget(b);
    m_layout->addLayout(row);
}

void NavPanel::addSeparator()
{
    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    line->setStyleSheet(QStringLiteral("color: #2f4f7a;"));
    m_layout->addWidget(line);
}

void NavPanel::finish()
{
    m_layout->addStretch();
}
