#include "WelcomeWidget.h"
#include <QFont>
#include <QLabel>
#include <QPushButton>
#include <QFileInfo>
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
    open->setAccessibleName(QStringLiteral("Open existing repository"));
    init->setAccessibleName(QStringLiteral("Initialize new repository"));
    clone->setAccessibleName(QStringLiteral("Clone remote repository"));
    for (QPushButton* button : {open, init, clone}) button->setMinimumWidth(240);
    auto* layout = new QVBoxLayout(this);
    layout->addStretch(2);
    layout->addWidget(title);
    layout->addSpacing(24);
    layout->addWidget(open, 0, Qt::AlignHCenter);
    layout->addWidget(init, 0, Qt::AlignHCenter);
    layout->addWidget(clone, 0, Qt::AlignHCenter);
    _recentLayout = new QVBoxLayout;
    _recentLayout->setSpacing(4);
    layout->addSpacing(16);
    layout->addLayout(_recentLayout);
    layout->addStretch(3);
    connect(open, &QPushButton::clicked, this, &WelcomeWidget::sigOpenRequested);
    connect(init, &QPushButton::clicked, this, &WelcomeWidget::sigInitRequested);
    connect(clone, &QPushButton::clicked, this, &WelcomeWidget::sigCloneRequested);
}

void WelcomeWidget::setRecentRepositories(const QStringList& paths)
{
    while (QLayoutItem* item = _recentLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    if (paths.isEmpty())
        return;

    auto* heading = new QLabel(QStringLiteral("Recent Repositories"), this);
    heading->setAlignment(Qt::AlignCenter);
    _recentLayout->addWidget(heading);
    for (qsizetype index = 0; index < paths.size(); ++index) {
        const QString& path = paths.at(index);
        auto* button = new QPushButton(QFileInfo(path).fileName(), this);
        button->setObjectName(
            QStringLiteral("welcomeRecentButton_%1").arg(index));
        button->setToolTip(path);
        button->setAccessibleName(
            QStringLiteral("Open recent repository %1").arg(path));
        button->setMinimumWidth(240);
        button->setMaximumWidth(420);
        _recentLayout->addWidget(button, 0, Qt::AlignHCenter);
        connect(button, &QPushButton::clicked, this,
                [this, path] { emit sigRecentRepositoryRequested(path); });
    }
}
