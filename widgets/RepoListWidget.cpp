#include "RepoListWidget.h"

#include <QTreeWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QMenu>
#include <QInputDialog>
#include <QMessageBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDir>
#include <algorithm>

static const QString kSettingsKey  = QStringLiteral("repos");
static const QString kDefaultGroup = QStringLiteral("Default");

// ============================================================
// 持久化（静态）
// ============================================================

QVector<RepoEntry> RepoListWidget::loadEntries()
{
    const QByteArray raw = QSettings().value(kSettingsKey).toByteArray();
    const QJsonArray arr = QJsonDocument::fromJson(raw).array();

    QVector<RepoEntry> result;
    result.reserve(arr.size());
    for (const auto& v : arr) {
        const QJsonObject o = v.toObject();
        const QString path  = o[QStringLiteral("path")].toString();
        if (!path.isEmpty())
            result.append({path, o[QStringLiteral("group")].toString()});
    }
    return result;
}

void RepoListWidget::saveEntries(const QVector<RepoEntry>& entries)
{
    QJsonArray arr;
    for (const RepoEntry& e : entries) {
        QJsonObject o;
        o[QStringLiteral("path")]  = e.path;
        o[QStringLiteral("group")] = e.group;
        arr.append(o);
    }
    QSettings s;
    s.setValue(kSettingsKey, QJsonDocument(arr).toJson(QJsonDocument::Compact));
    s.sync();  // 立即写入，防止进程退出前数据丢失
}

void RepoListWidget::recordRepo(const QString& path)
{
    auto entries = loadEntries();
    const QString cleanPath = QDir::cleanPath(path);
    QString group = kDefaultGroup;
    entries.erase(std::remove_if(entries.begin(), entries.end(),
        [&cleanPath, &group](const RepoEntry& entry) {
            if (QDir::cleanPath(entry.path).compare(
                    cleanPath, Qt::CaseInsensitive) != 0) return false;
            group = entry.group.isEmpty() ? kDefaultGroup : entry.group;
            return true;
        }), entries.end());
    entries.append({cleanPath, group});
    saveEntries(entries);
}

QStringList RepoListWidget::recentRepositoryPaths(int limit)
{
    QStringList paths;
    if (limit <= 0)
        return paths;

    const auto entries = loadEntries();
    for (auto it = entries.crbegin(); it != entries.crend() && paths.size() < limit; ++it) {
        const QString path = QDir::cleanPath(it->path);
        if (QFileInfo(path).isDir() && !paths.contains(path, Qt::CaseInsensitive))
            paths.append(path);
    }
    return paths;
}

// ============================================================
// 构造 / UI
// ============================================================

RepoListWidget::RepoListWidget(QWidget* parent)
    : QWidget(parent)
{
    // ---- 标题栏 ----
    auto* titleLabel = new QLabel(QStringLiteral("Repositories"), this);
    QFont tf = titleLabel->font();
    tf.setBold(true);
    titleLabel->setFont(tf);

    auto* addBtn = new QToolButton(this);
    addBtn->setText(QStringLiteral("+"));
    addBtn->setToolTip(QStringLiteral("Add repository…"));
    addBtn->setAccessibleName(QStringLiteral("Add repository"));
    addBtn->setAutoRaise(true);

    auto* headerLayout = new QHBoxLayout;
    headerLayout->setContentsMargins(6, 4, 4, 4);
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(addBtn);

    // ---- 树控件 ----
    _tree = new QTreeWidget(this);
    _tree->setAccessibleName(QStringLiteral("Repositories"));
    _tree->setHeaderHidden(true);
    _tree->setIndentation(12);
    _tree->setContextMenuPolicy(Qt::CustomContextMenu);
    _tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _tree->setSelectionBehavior(QAbstractItemView::SelectRows);

    connect(addBtn, &QToolButton::clicked, this, &RepoListWidget::slotAddRepo);
    connect(_tree, &QTreeWidget::customContextMenuRequested,
            this, &RepoListWidget::slotContextMenu);
    connect(_tree, &QTreeWidget::itemClicked,
            this, &RepoListWidget::slotItemClicked);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(headerLayout);
    layout->addWidget(_tree);

    _entries = loadEntries();
    rebuild();
}

// ============================================================
// 内部辅助
// ============================================================

QStringList RepoListWidget::allGroups() const
{
    QStringList groups;
    groups.append(kDefaultGroup);
    for (const RepoEntry& e : _entries) {
        if (!groups.contains(e.group))
            groups.append(e.group);
    }
    return groups;
}

void RepoListWidget::rebuild()
{
    _tree->clear();

    for (const QString& g : allGroups()) {
        // 检查该分组是否有条目
        bool hasEntry = false;
        for (const RepoEntry& e : _entries) {
            if (e.group == g) { hasEntry = true; break; }
        }
        if (!hasEntry)
            continue;

        auto* groupItem = new QTreeWidgetItem(_tree);
        groupItem->setText(0, g);
        groupItem->setData(0, Qt::UserRole,     g);
        groupItem->setData(0, Qt::UserRole + 1, true);  // isGroup
        QFont gf = groupItem->font(0);
        gf.setBold(true);
        groupItem->setFont(0, gf);

        for (const RepoEntry& e : _entries) {
            if (e.group != g)
                continue;

            const QString name = QFileInfo(e.path).fileName();
            auto* item = new QTreeWidgetItem(groupItem);
            item->setText(0, name);
            item->setToolTip(0, e.path);
            item->setData(0, Qt::UserRole,     e.path);
            item->setData(0, Qt::UserRole + 1, false);  // isRepo

            if (e.path == _currentPath) {
                QFont f = item->font(0);
                f.setBold(true);
                item->setFont(0, f);
                item->setForeground(0, QColor(QStringLiteral("#3fb950")));
                _tree->setCurrentItem(item);  // 同步选中，方便视觉定位
            }
        }

        // 子项全部加入后再展开，确保展开状态生效
        groupItem->setExpanded(true);
    }
}

void RepoListWidget::setCurrentRepo(const QString& path)
{
    _currentPath = path;

    if (path.isEmpty()) {
        rebuild();
        return;
    }

    // 仅追加新仓库，不改变现有排列顺序
    bool found = false;
    for (const RepoEntry& e : _entries) {
        if (e.path == path) { found = true; break; }
    }
    if (!found)
        _entries.append({path, kDefaultGroup});

    rebuild();
}

// ============================================================
// 槽函数
// ============================================================

void RepoListWidget::slotItemClicked(QTreeWidgetItem* item, int /*col*/)
{
    if (!item)
        return;
    const bool isGroup = item->data(0, Qt::UserRole + 1).toBool();
    if (!isGroup)
        emit sigRepoSwitchRequested(item->data(0, Qt::UserRole).toString());
}

void RepoListWidget::slotAddRepo()
{
    const QString path = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("Add Git Repository"),
        QDir::homePath());
    if (!path.isEmpty())
        emit sigRepoSwitchRequested(path);
}

void RepoListWidget::slotContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = _tree->itemAt(pos);
    QMenu menu(this);

    if (!item) {
        // 空白区域
        menu.addAction(QStringLiteral("Add Repository…"), this, &RepoListWidget::slotAddRepo);
        menu.addAction(QStringLiteral("New Group…"), [this]() {
            const QString name = QInputDialog::getText(
                this, QStringLiteral("New Group"),
                QStringLiteral("Group name:")).trimmed();
            if (name.isEmpty() || allGroups().contains(name))
                return;
            // 空分组不持久化，仅提示用户移动仓库到该分组
            QMessageBox::information(
                this, QStringLiteral("New Group"),
                QStringLiteral("Group '%1' created. Move a repository into it via right-click.").arg(name));
        });
        menu.exec(_tree->viewport()->mapToGlobal(pos));
        return;
    }

    const bool isGroup = item->data(0, Qt::UserRole + 1).toBool();

    if (isGroup) {
        const QString groupName = item->data(0, Qt::UserRole).toString();

        menu.addAction(QStringLiteral("Rename Group…"), [this, groupName]() {
            const QString newName = QInputDialog::getText(
                this, QStringLiteral("Rename Group"),
                QStringLiteral("New name:"),
                QLineEdit::Normal, groupName).trimmed();
            if (newName.isEmpty() || newName == groupName)
                return;
            for (RepoEntry& e : _entries) {
                if (e.group == groupName)
                    e.group = newName;
            }
            saveEntries(_entries);
            rebuild();
        });

        menu.addSeparator();

        menu.addAction(QStringLiteral("Delete Group"), [this, item, groupName]() {
            if (item->childCount() > 0) {
                const auto btn = QMessageBox::question(
                    this, QStringLiteral("Delete Group"),
                    QStringLiteral("Move all repos in '%1' to 'Default' and delete the group?")
                        .arg(groupName));
                if (btn != QMessageBox::Yes)
                    return;
                for (RepoEntry& e : _entries) {
                    if (e.group == groupName)
                        e.group = kDefaultGroup;
                }
            }
            saveEntries(_entries);
            rebuild();
        });

    } else {
        const QString path = item->data(0, Qt::UserRole).toString();

        menu.addAction(QStringLiteral("Open"), [this, path]() {
            emit sigRepoSwitchRequested(path);
        });

        // 移动到分组子菜单
        auto* moveMenu = menu.addMenu(QStringLiteral("Move to Group"));
        for (const QString& g : allGroups()) {
            moveMenu->addAction(g, [this, path, g]() {
                for (RepoEntry& e : _entries) {
                    if (e.path == path) { e.group = g; break; }
                }
                saveEntries(_entries);
                rebuild();
            });
        }
        moveMenu->addSeparator();
        moveMenu->addAction(QStringLiteral("New Group…"), [this, path]() {
            const QString name = QInputDialog::getText(
                this, QStringLiteral("New Group"),
                QStringLiteral("Group name:")).trimmed();
            if (name.isEmpty())
                return;
            for (RepoEntry& e : _entries) {
                if (e.path == path) { e.group = name; break; }
            }
            saveEntries(_entries);
            rebuild();
        });

        menu.addSeparator();

        menu.addAction(QStringLiteral("Remove from List"), [this, path]() {
            _entries.erase(
                std::remove_if(_entries.begin(), _entries.end(),
                    [&path](const RepoEntry& e){ return e.path == path; }),
                _entries.end());
            if (_currentPath == path)
                _currentPath.clear();
            saveEntries(_entries);
            rebuild();
        });
    }

    menu.exec(_tree->viewport()->mapToGlobal(pos));
}
