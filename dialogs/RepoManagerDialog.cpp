#include "RepoManagerDialog.h"

#include <QTreeWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
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

static const QString kSettingsKey  = QStringLiteral("repos");
static const QString kDefaultGroup = QStringLiteral("Default");

// ============================================================
// 持久化 —— 静态方法
// ============================================================

QVector<RepoEntry> RepoManagerDialog::loadEntries()
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

void RepoManagerDialog::saveEntries(const QVector<RepoEntry>& entries)
{
    QJsonArray arr;
    for (const RepoEntry& e : entries) {
        QJsonObject o;
        o[QStringLiteral("path")]  = e.path;
        o[QStringLiteral("group")] = e.group;
        arr.append(o);
    }
    QSettings().setValue(kSettingsKey,
                         QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void RepoManagerDialog::recordRepo(const QString& path)
{
    auto entries = loadEntries();

    // 若已存在则移到最前
    for (int i = 0; i < entries.size(); ++i) {
        if (entries[i].path == path) {
            if (i != 0) {
                const RepoEntry e = entries.takeAt(i);
                entries.prepend(e);
                saveEntries(entries);
            }
            return;
        }
    }

    // 不存在则追加到最前，归入 Default 分组
    entries.prepend({path, kDefaultGroup});
    saveEntries(entries);
}

// ============================================================
// 构造 / UI
// ============================================================

RepoManagerDialog::RepoManagerDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Repositories"));
    resize(640, 400);

    _entries = loadEntries();

    // ---- 树控件 ----
    _tree = new QTreeWidget(this);
    _tree->setColumnCount(2);
    _tree->setHeaderLabels({QStringLiteral("Name"), QStringLiteral("Path")});
    _tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    _tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    _tree->setContextMenuPolicy(Qt::CustomContextMenu);
    _tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _tree->setSelectionBehavior(QAbstractItemView::SelectRows);

    connect(_tree, &QTreeWidget::customContextMenuRequested,
            this, &RepoManagerDialog::slotContextMenu);
    connect(_tree, &QTreeWidget::itemDoubleClicked,
            this, &RepoManagerDialog::slotItemDoubleClicked);
    connect(_tree->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &RepoManagerDialog::slotSelectionChanged);

    // ---- 底部按钮 ----
    auto* browseBtn = new QPushButton(QStringLiteral("Browse New…"), this);
    auto* newGrpBtn = new QPushButton(QStringLiteral("New Group"),   this);
    _openBtn        = new QPushButton(QStringLiteral("Open"),        this);
    auto* cancelBtn = new QPushButton(QStringLiteral("Cancel"),      this);

    _openBtn->setEnabled(false);
    _openBtn->setDefault(true);

    connect(browseBtn, &QPushButton::clicked, this, &RepoManagerDialog::slotBrowse);
    connect(newGrpBtn, &QPushButton::clicked, this, &RepoManagerDialog::slotNewGroup);
    connect(_openBtn,  &QPushButton::clicked, this, &RepoManagerDialog::slotOpenSelected);
    connect(cancelBtn, &QPushButton::clicked, this, &RepoManagerDialog::reject);

    auto* btnLayout = new QHBoxLayout;
    btnLayout->addWidget(browseBtn);
    btnLayout->addWidget(newGrpBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(_openBtn);
    btnLayout->addWidget(cancelBtn);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(_tree);
    mainLayout->addLayout(btnLayout);

    rebuildTree();
}

// ============================================================
// 内部辅助
// ============================================================

QStringList RepoManagerDialog::allGroups() const
{
    QStringList groups;
    groups.append(kDefaultGroup);
    for (const RepoEntry& e : _entries) {
        if (!groups.contains(e.group))
            groups.append(e.group);
    }
    return groups;
}

void RepoManagerDialog::rebuildTree()
{
    _tree->clear();

    const QStringList groups = allGroups();

    for (const QString& g : groups) {
        auto* groupItem = new QTreeWidgetItem(_tree);
        groupItem->setText(0, g);
        groupItem->setData(0, Qt::UserRole,     g);
        groupItem->setData(0, Qt::UserRole + 1, true);  // isGroup
        QFont f = groupItem->font(0);
        f.setBold(true);
        groupItem->setFont(0, f);
        groupItem->setExpanded(true);

        for (const RepoEntry& e : _entries) {
            if (e.group != g)
                continue;
            auto* item = new QTreeWidgetItem(groupItem);
            item->setText(0, QFileInfo(e.path).fileName());
            item->setText(1, e.path);
            item->setData(0, Qt::UserRole,     e.path);
            item->setData(0, Qt::UserRole + 1, false);  // isRepo
        }
    }
}

void RepoManagerDialog::tryAccept(const QString& path)
{
    _selectedPath = path;
    accept();
}

// ============================================================
// 槽函数
// ============================================================

void RepoManagerDialog::slotSelectionChanged()
{
    const auto sel = _tree->selectedItems();
    if (sel.isEmpty()) {
        _openBtn->setEnabled(false);
        return;
    }
    const bool isRepo = !sel.first()->data(0, Qt::UserRole + 1).toBool();
    _openBtn->setEnabled(isRepo);
}

void RepoManagerDialog::slotItemDoubleClicked(QTreeWidgetItem* item, int /*col*/)
{
    if (!item)
        return;
    const bool isGroup = item->data(0, Qt::UserRole + 1).toBool();
    if (!isGroup)
        tryAccept(item->data(0, Qt::UserRole).toString());
}

void RepoManagerDialog::slotOpenSelected()
{
    const auto sel = _tree->selectedItems();
    if (sel.isEmpty())
        return;
    const bool isGroup = sel.first()->data(0, Qt::UserRole + 1).toBool();
    if (!isGroup)
        tryAccept(sel.first()->data(0, Qt::UserRole).toString());
}

void RepoManagerDialog::slotBrowse()
{
    const QString path = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("Open Git Repository"),
        QDir::homePath());

    if (path.isEmpty())
        return;

    tryAccept(path);
}

void RepoManagerDialog::slotNewGroup()
{
    const QString name = QInputDialog::getText(
        this,
        QStringLiteral("New Group"),
        QStringLiteral("Group name:")).trimmed();

    if (name.isEmpty() || allGroups().contains(name))
        return;

    // 用一个空占位 entry 让分组出现在树里；实际无路径，不保存
    // 只需要让 allGroups() 返回它 —— 由于 allGroups() 从 _entries 派生，
    // 添加一个 path="" 的条目会被 loadEntries 过滤掉，所以改用单独的 groups 列表。
    // 更简单的方案：直接在树里添加分组节点，不持久化空分组
    auto* groupItem = new QTreeWidgetItem(_tree);
    groupItem->setText(0, name);
    groupItem->setData(0, Qt::UserRole,     name);
    groupItem->setData(0, Qt::UserRole + 1, true);
    QFont f = groupItem->font(0);
    f.setBold(true);
    groupItem->setFont(0, f);
    groupItem->setExpanded(true);
}

void RepoManagerDialog::slotContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = _tree->itemAt(pos);

    QMenu menu(this);

    if (!item) {
        // 空白区域：新建分组
        menu.addAction(QStringLiteral("New Group…"), this, &RepoManagerDialog::slotNewGroup);
        menu.exec(_tree->viewport()->mapToGlobal(pos));
        return;
    }

    const bool isGroup = item->data(0, Qt::UserRole + 1).toBool();

    if (isGroup) {
        const QString groupName = item->data(0, Qt::UserRole).toString();

        menu.addAction(QStringLiteral("Rename Group…"), [this, item, groupName]() {
            const QString newName = QInputDialog::getText(
                this,
                QStringLiteral("Rename Group"),
                QStringLiteral("New name:"),
                QLineEdit::Normal,
                groupName).trimmed();
            if (newName.isEmpty() || newName == groupName)
                return;
            // 更新所有该分组的条目
            for (RepoEntry& e : _entries) {
                if (e.group == groupName)
                    e.group = newName;
            }
            saveEntries(_entries);
            rebuildTree();
        });

        menu.addSeparator();

        menu.addAction(QStringLiteral("Delete Group"), [this, item, groupName]() {
            // 将该分组下的仓库移入 Default
            bool hasChildren = (item->childCount() > 0);
            if (hasChildren) {
                const auto btn = QMessageBox::question(
                    this,
                    QStringLiteral("Delete Group"),
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
            rebuildTree();
        });

    } else {
        // 仓库条目
        const QString path = item->data(0, Qt::UserRole).toString();

        menu.addAction(QStringLiteral("Open"), [this, path]() {
            tryAccept(path);
        });

        // "移动到分组" 子菜单
        auto* moveMenu = menu.addMenu(QStringLiteral("Move to Group"));
        for (const QString& g : allGroups()) {
            moveMenu->addAction(g, [this, path, g]() {
                for (RepoEntry& e : _entries) {
                    if (e.path == path) {
                        e.group = g;
                        break;
                    }
                }
                saveEntries(_entries);
                rebuildTree();
            });
        }
        moveMenu->addSeparator();
        moveMenu->addAction(QStringLiteral("New Group…"), [this, path]() {
            const QString name = QInputDialog::getText(
                this,
                QStringLiteral("New Group"),
                QStringLiteral("Group name:")).trimmed();
            if (name.isEmpty())
                return;
            for (RepoEntry& e : _entries) {
                if (e.path == path) {
                    e.group = name;
                    break;
                }
            }
            saveEntries(_entries);
            rebuildTree();
        });

        menu.addSeparator();

        menu.addAction(QStringLiteral("Remove from List"), [this, path]() {
            _entries.erase(
                std::remove_if(_entries.begin(), _entries.end(),
                               [&path](const RepoEntry& e){ return e.path == path; }),
                _entries.end());
            saveEntries(_entries);
            rebuildTree();
        });
    }

    menu.exec(_tree->viewport()->mapToGlobal(pos));
}
