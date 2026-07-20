#include "WelcomeWidget.h"
#include <QFont>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

WelcomeWidget::WelcomeWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("welcomeWidget"));
    auto* title = new QLabel(QStringLiteral("Git Manager"), this);
    QFont titleFont = title->font();
    titleFont.setPointSize(22);
    titleFont.setBold(true);
    title->setFont(titleFont);
    title->setAlignment(Qt::AlignCenter);
    auto* open = new QPushButton(QStringLiteral("Open Repository"), this);
    auto* init = new QPushButton(QStringLiteral("Initialize Repository"), this);
    auto* clone = new QPushButton(QStringLiteral("Clone Repository"), this);
    open->setObjectName(QStringLiteral("welcomeOpenButton"));
    init->setObjectName(QStringLiteral("welcomeInitButton"));
    clone->setObjectName(QStringLiteral("welcomeCloneButton"));
    for (QPushButton* button : {open, init, clone}) button->setMinimumWidth(240);
    auto* layout = new QVBoxLayout(this);
    layout->addStretch(2);
    layout->addWidget(title);
    layout->addSpacing(24);
    layout->addWidget(open, 0, Qt::AlignHCenter);
    layout->addWidget(init, 0, Qt::AlignHCenter);
    layout->addWidget(clone, 0, Qt::AlignHCenter);
    layout->addStretch(3);
    connect(open, &QPushButton::clicked, this, &WelcomeWidget::sigOpenRequested);
    connect(init, &QPushButton::clicked, this, &WelcomeWidget::sigInitRequested);
    connect(clone, &QPushButton::clicked, this, &WelcomeWidget::sigCloneRequested);
}
