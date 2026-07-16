#include "StatusWidget.h"
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMenu>
#include <QAction>

// UserRole 字段约定
static constexpr int kRolePath    = Qt::UserRole;       // QString: 文件路径
static constexpr int kRoleStaged  = Qt::UserRole + 1;   // bool:    当前是否已暂存
static constexpr int kRoleUntrack = Qt::UserRole + 2;   // bool:    是否为未追踪文件

StatusWidget::StatusWidget(QWidget* parent)
    : QWidget(parent)
{
    _summaryLabel  = new QLabel(QStringLiteral("No changes"), this);
    _list          = new QListWidget(this);
    _stageAllBtn   = new QPushButton(QStringLiteral("Stage All"),   this);
    _unstageAllBtn = new QPushButton(QStringLiteral("Unstage All"), this);

    _list->setContextMenuPolicy(Qt::CustomContextMenu);

    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(_stageAllBtn);
    btnRow->addWidget(_unstageAllBtn);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    layout->addWidget(_summaryLabel);
    layout->addWidget(_list, 1);
    layout->addLayout(btnRow);

    connect(_list, &QListWidget::itemChanged,
            this, &StatusWidget::slotItemChanged);
    connect(_list, &QListWidget::itemClicked,
            this, &StatusWidget::slotItemClicked);
    connect(_list, &QListWidget::customContextMenuRequested,
            this, &StatusWidget::slotContextMenu);
    connect(_stageAllBtn,   &QPushButton::clicked,
            this, &StatusWidget::sigStageAllRequested);
    connect(_unstageAllBtn, &QPushButton::clicked,
            this, &StatusWidget::sigUnstageAllRequested);
}

void StatusWidget::setFiles(const QVector<Git::File>& files)
{
    // 刷新时屏蔽 itemChanged，避免触发 stage/unstage
    _list->blockSignals(true);
    _list->clear();

    int stagedCount   = 0;
    int unstagedCount = 0;

    for (const Git::File& f : files) {
        const bool staged    = f.isStaged();
        const bool unstaged  = f.isUnstaged();
        const bool untracked = (f.workStatus == Git::File::Status::E_Untracked);

        // 取最具代表性的状态用于展示
        const Git::File::Status displayStatus = staged ? f.indexStatus : f.workStatus;
        const auto [badge, color] = statusStyle(displayStatus);

        const QString label = QStringLiteral(" %1  %2").arg(badge).arg(f.path);

        auto* item = new QListWidgetItem(label, _list);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        item->setCheckState(staged ? Qt::Checked : Qt::Unchecked);
        item->setForeground(color);
        item->setData(kRolePath,    f.path);
        item->setData(kRoleStaged,  staged);
        item->setData(kRoleUntrack, untracked);
        item->setToolTip(QStringLiteral("Index: %1  Worktree: %2\n%3")
            .arg(f.statusText()[0])
            .arg(f.statusText()[1])
            .arg(f.path));

        if (staged)   ++stagedCount;
        if (unstaged) ++unstagedCount;
    }

    _list->blockSignals(false);

    const int total = files.size();
    _summaryLabel->setText(
        total == 0
            ? QStringLiteral("No changes")
            : QStringLiteral("%1 file(s)  ·  %2 staged  ·  %3 unstaged")
                  .arg(total).arg(stagedCount).arg(unstagedCount));
}

void StatusWidget::slotItemChanged(QListWidgetItem* item)
{
    if (!item)
        return;

    const QString path       = item->data(kRolePath).toString();
    const bool    wasStaged  = item->data(kRoleStaged).toBool();
    const bool    nowChecked = (item->checkState() == Qt::Checked);

    // 更新缓存状态，避免重复触发
    item->setData(kRoleStaged, nowChecked);

    if (nowChecked && !wasStaged)
        emit sigStageRequested(path);
    else if (!nowChecked && wasStaged)
        emit sigUnstageRequested(path);
}

void StatusWidget::slotItemClicked(QListWidgetItem* item)
{
    if (!item)
        return;

    const QString path   = item->data(kRolePath).toString();
    const bool    staged = item->data(kRoleStaged).toBool();
    emit sigFileSelected(path, staged);
}

void StatusWidget::slotContextMenu(const QPoint& pos)
{
    QListWidgetItem* item = _list->itemAt(pos);
    if (!item)
        return;

    const QString path      = item->data(kRolePath).toString();
    const bool    untracked = item->data(kRoleUntrack).toBool();

    QMenu menu(this);

    menu.addAction(QStringLiteral("Show Diff"), [this, item] {
        slotItemClicked(item);
    });

    if (untracked) {
        menu.addSeparator();
        menu.addAction(QStringLiteral("Add to .gitignore"), [this, path] {
            emit sigIgnoreRequested(path);
        });
    }

    menu.exec(_list->mapToGlobal(pos));
}

QPair<QString, QColor> StatusWidget::statusStyle(Git::File::Status s)
{
    switch (s) {
    case Git::File::Status::E_Modified:  return {"M", QColor("#d29922")};
    case Git::File::Status::E_Added:     return {"A", QColor("#3fb950")};
    case Git::File::Status::E_Deleted:   return {"D", QColor("#f85149")};
    case Git::File::Status::E_Renamed:   return {"R", QColor("#58a6ff")};
    case Git::File::Status::E_Copied:    return {"C", QColor("#58a6ff")};
    case Git::File::Status::E_Untracked: return {"?", QColor("#8b949e")};
    case Git::File::Status::E_Ignored:   return {"!", QColor("#484f58")};
    default:                             return {" ", QColor("#c9d1d9")};
    }
}
