#include "StatusWidget.h"
#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>

namespace {
constexpr int kRolePath = Qt::UserRole;
constexpr int kRoleStaged = Qt::UserRole + 1;
constexpr int kRoleUntracked = Qt::UserRole + 2;
constexpr int kRoleHeader = Qt::UserRole + 3;
constexpr int kRoleConflict = Qt::UserRole + 4;

QListWidgetItem* addHeader(QListWidget* list, const QString& title, int count)
{
    auto* item = new QListWidgetItem(QStringLiteral("%1 (%2)").arg(title).arg(count), list);
    item->setFlags(Qt::NoItemFlags);
    item->setData(kRoleHeader, true);
    QFont font = item->font();
    font.setBold(true);
    item->setFont(font);
    item->setForeground(QColor(QStringLiteral("#8b949e")));
    return item;
}
}

StatusWidget::StatusWidget(QWidget* parent) : QWidget(parent)
{
    _summaryLabel = new QLabel(QStringLiteral("No changes"), this);
    _list = new QListWidget(this);
    _stageAllBtn = new QPushButton(QStringLiteral("Stage All"), this);
    _unstageAllBtn = new QPushButton(QStringLiteral("Unstage All"), this);
    _summaryLabel->setAccessibleName(QStringLiteral("Working tree summary"));
    _list->setAccessibleName(QStringLiteral("Changed files"));
    _stageAllBtn->setAccessibleName(QStringLiteral("Stage all changed files"));
    _unstageAllBtn->setAccessibleName(QStringLiteral("Unstage all staged files"));
    _list->setContextMenuPolicy(Qt::CustomContextMenu);
    _list->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    _list->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    _list->setTextElideMode(Qt::ElideNone);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(_stageAllBtn);
    buttons->addWidget(_unstageAllBtn);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(_summaryLabel);
    layout->addWidget(_list, 1);
    layout->addLayout(buttons);

    connect(_list, &QListWidget::itemChanged, this, &StatusWidget::slotItemChanged);
    connect(_list, &QListWidget::itemClicked, this, &StatusWidget::slotItemClicked);
    connect(_list, &QListWidget::customContextMenuRequested, this, &StatusWidget::slotContextMenu);
    connect(_stageAllBtn, &QPushButton::clicked, this, &StatusWidget::sigStageAllRequested);
    connect(_unstageAllBtn, &QPushButton::clicked, this, &StatusWidget::sigUnstageAllRequested);
}

void StatusWidget::setFiles(const QVector<Git::File>& files)
{
    _list->blockSignals(true);
    _list->clear();
    QVector<Git::File> conflicts, changes, staged;
    for (const auto& file : files) {
        if (file.conflicted) conflicts << file;
        else {
            if (file.isUnstaged()) changes << file;
            if (file.isStaged()) staged << file;
        }
    }

    auto addRows = [this](const QString& title, const QVector<Git::File>& entries, bool isStaged) {
        if (entries.isEmpty()) return;
        addHeader(_list, title, entries.size());
        for (const auto& file : entries) {
            const bool untracked = file.workStatus == Git::File::Status::E_Untracked;
            const auto state = isStaged ? file.indexStatus : file.workStatus;
            const auto [badge, color] = statusStyle(state);
            auto* item = new QListWidgetItem(QStringLiteral("  %1  %2").arg(badge, file.path), _list);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
            item->setCheckState(isStaged ? Qt::Checked : Qt::Unchecked);
            item->setForeground(color);
            item->setData(kRolePath, file.path);
            item->setData(kRoleStaged, isStaged);
            item->setData(kRoleUntracked, untracked);
            item->setData(kRoleConflict, file.conflicted);
            item->setToolTip(QStringLiteral("Index: %1  Worktree: %2\n%3")
                .arg(file.statusText()[0]).arg(file.statusText()[1]).arg(file.path));
        }
    };

    addRows(QStringLiteral("Merge Changes"), conflicts, false);
    addRows(QStringLiteral("Changes"), changes, false);
    addRows(QStringLiteral("Staged Changes"), staged, true);
    _list->blockSignals(false);
    _summaryLabel->setText(files.isEmpty() ? QStringLiteral("No changes")
        : QStringLiteral("%1 file(s) · %2 staged · %3 unstaged · %4 conflicts")
              .arg(files.size()).arg(staged.size()).arg(changes.size()).arg(conflicts.size()));
}

void StatusWidget::slotItemChanged(QListWidgetItem* item)
{
    if (!item || item->data(kRoleHeader).toBool()) return;
    const bool staged = item->data(kRoleStaged).toBool();
    const bool checked = item->checkState() == Qt::Checked;
    if (checked && !staged) emit sigStageRequested(item->data(kRolePath).toString());
    else if (!checked && staged) emit sigUnstageRequested(item->data(kRolePath).toString());
}

void StatusWidget::slotItemClicked(QListWidgetItem* item)
{
    if (!item || item->data(kRoleHeader).toBool()) return;
    emit sigFileSelected(item->data(kRolePath).toString(), item->data(kRoleStaged).toBool(),
                         item->data(kRoleUntracked).toBool());
}

void StatusWidget::slotContextMenu(const QPoint& pos)
{
    auto* item = _list->itemAt(pos);
    if (!item || item->data(kRoleHeader).toBool()) return;
    const QString path = item->data(kRolePath).toString();
    QMenu menu(this);
    menu.addAction(QStringLiteral("Show Diff"), [this, item] { slotItemClicked(item); });
    menu.addAction(QStringLiteral("Open File"),
                   [this, path] { emit sigOpenFileRequested(path); });
    menu.addAction(QStringLiteral("Open Containing Folder"),
                   [this, path] { emit sigRevealFileRequested(path); });
    auto* copyMenu = menu.addMenu(QStringLiteral("Copy Path"));
    copyMenu->addAction(QStringLiteral("Relative Path"),
                        [this, path] { emit sigCopyPathRequested(path, false); });
    copyMenu->addAction(QStringLiteral("Absolute Path"),
                        [this, path] { emit sigCopyPathRequested(path, true); });
    if (item->data(kRoleUntracked).toBool()) {
        menu.addSeparator();
        menu.addAction(QStringLiteral("Add to .gitignore"), [this, path] { emit sigIgnoreRequested(path); });
    }
    menu.addSeparator();
    const bool untracked = item->data(kRoleUntracked).toBool();
    menu.addAction(untracked ? QStringLiteral("Delete Untracked File") : QStringLiteral("Discard Changes"),
                   [this, path, untracked] { emit sigDiscardRequested(path, untracked); });
    if (item->data(kRoleConflict).toBool()) {
        menu.addSeparator();
        menu.addAction(QStringLiteral("Accept Current"), [this, path] { emit sigResolveRequested(path, true); });
        menu.addAction(QStringLiteral("Accept Incoming"), [this, path] { emit sigResolveRequested(path, false); });
        menu.addAction(QStringLiteral("Open in External Merge Tool"),
                       [this, path] { emit sigExternalMergeRequested(path); });
    }
    menu.exec(_list->mapToGlobal(pos));
}

QPair<QString, QColor> StatusWidget::statusStyle(Git::File::Status status)
{
    using S = Git::File::Status;
    switch (status) {
    case S::E_Modified: return {"M", QColor("#d29922")};
    case S::E_Added: return {"A", QColor("#3fb950")};
    case S::E_Deleted: return {"D", QColor("#f85149")};
    case S::E_Renamed: return {"R", QColor("#58a6ff")};
    case S::E_Copied: return {"C", QColor("#58a6ff")};
    case S::E_TypeChanged: return {"T", QColor("#d2a8ff")};
    case S::E_Unmerged: return {"U", QColor("#f85149")};
    case S::E_Untracked: return {"?", QColor("#8b949e")};
    case S::E_Ignored: return {"!", QColor("#484f58")};
    default: return {" ", QColor("#c9d1d9")};
    }
}
