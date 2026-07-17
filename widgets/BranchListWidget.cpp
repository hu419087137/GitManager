#include "BranchListWidget.h"
#include <QMenu>
#include <QAction>
#include <QInputDialog>
#include <QMessageBox>

BranchListWidget::BranchListWidget(QWidget* parent)
    : QTreeWidget(parent)
{
    setHeaderHidden(true);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setEditTriggers(QAbstractItemView::NoEditTriggers);

    _localRoot  = new QTreeWidgetItem(this, {QStringLiteral("Local")});
    _remoteRoot = new QTreeWidgetItem(this, {QStringLiteral("Remote")});
    _localRoot->setExpanded(true);
    _remoteRoot->setExpanded(true);

    connect(this, &QTreeWidget::customContextMenuRequested,
            this, &BranchListWidget::slotContextMenu);
    connect(this, &QTreeWidget::itemDoubleClicked,
            this, &BranchListWidget::slotItemDoubleClicked);
}

void BranchListWidget::setBranches(const QVector<Git::Branch>& branches)
{
    // 清空子项，保留根节点
    qDeleteAll(_localRoot->takeChildren());
    qDeleteAll(_remoteRoot->takeChildren());

    // 找出当前本地分支的 upstream（远端跟踪分支名，如 "origin/main"）
    QString currentUpstream;
    for (const Git::Branch& b : branches) {
        if (b.isCurrent && !b.isRemote) {
            currentUpstream = b.upstream;
            break;
        }
    }

    for (const Git::Branch& b : branches) {
        QString displayName = b.name;
        if (!b.isRemote && !b.upstream.isEmpty())
            displayName += QStringLiteral("  ↑%1 ↓%2").arg(b.ahead).arg(b.behind);
        auto* item = new QTreeWidgetItem({displayName});
        item->setData(0, Qt::UserRole,     b.name);
        item->setData(0, Qt::UserRole + 1, b.isRemote);
        item->setData(0, Qt::UserRole + 2, b.isCurrent);

        if (b.isCurrent) {
            // 当前本地分支：加粗 + "* " 前缀
            QFont f = item->font(0);
            f.setBold(true);
            item->setFont(0, f);
            item->setText(0, QStringLiteral("* ") + b.name);
        } else if (b.isRemote && !currentUpstream.isEmpty() && b.name == currentUpstream) {
            // 当前分支正在跟踪的远端分支：蓝色加粗 + "→ " 前缀
            QFont f = item->font(0);
            f.setBold(true);
            item->setFont(0, f);
            item->setForeground(0, QColor(QStringLiteral("#58a6ff")));
            item->setText(0, QStringLiteral("→ ") + b.name);
        }

        if (b.isRemote)
            _remoteRoot->addChild(item);
        else
            _localRoot->addChild(item);
    }

    _localRoot->setExpanded(true);
    _remoteRoot->setExpanded(true);
}

void BranchListWidget::slotItemDoubleClicked(QTreeWidgetItem* item, int /*column*/)
{
    if (!item || item == _localRoot || item == _remoteRoot)
        return;

    const bool isRemote = item->data(0, Qt::UserRole + 1).toBool();
    if (!isRemote && _operationsEnabled) {
        const QString name = item->data(0, Qt::UserRole).toString();
        emit sigCheckoutRequested(name);
    }
}

void BranchListWidget::slotContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = itemAt(pos);
    if (!item || item == _localRoot || item == _remoteRoot)
        return;

    const QString name     = item->data(0, Qt::UserRole).toString();
    const bool    isRemote = item->data(0, Qt::UserRole + 1).toBool();
    const bool    isCurrent = item->data(0, Qt::UserRole + 2).toBool();

    QMenu menu(this);

    if (!isRemote) {
        QAction* checkout = menu.addAction(QStringLiteral("Checkout"), [this, name] {
            emit sigCheckoutRequested(name);
        });
        QAction* createBranch = menu.addAction(QStringLiteral("New Branch from Here..."), [this, name] {
            emit sigCreateFromRequested(name);
        });
        checkout->setEnabled(_operationsEnabled && !isCurrent);
        createBranch->setEnabled(_operationsEnabled);

        menu.addSeparator();
        QAction* merge = menu.addAction(QStringLiteral("Merge into Current Branch"),
                                        [this, name] {
            emit sigMergeRequested(name);
        });
        merge->setObjectName(QStringLiteral("branchMergeAction"));
        QAction* rebase = menu.addAction(QStringLiteral("Rebase Current Branch onto Here..."),
                                         [this, name] {
            emit sigRebaseRequested(name);
        });
        rebase->setObjectName(QStringLiteral("branchRebaseAction"));
        merge->setEnabled(_operationsEnabled && !isCurrent);
        rebase->setEnabled(_operationsEnabled && !isCurrent);

        menu.addSeparator();
        QAction* deleteBranch = menu.addAction(QStringLiteral("Delete Branch"), [this, name] {
            const auto btn = QMessageBox::question(
                this,
                QStringLiteral("Delete Branch"),
                QStringLiteral("Delete branch '%1'?").arg(name));
            if (btn == QMessageBox::Yes)
                emit sigDeleteRequested(name, false);
        });
        QAction* forceDelete = menu.addAction(QStringLiteral("Force Delete Branch"), [this, name] {
            const auto btn = QMessageBox::question(
                this,
                QStringLiteral("Force Delete"),
                QStringLiteral("Force delete branch '%1'?").arg(name));
            if (btn == QMessageBox::Yes)
                emit sigDeleteRequested(name, true);
        });
        deleteBranch->setEnabled(_operationsEnabled && !isCurrent);
        forceDelete->setEnabled(_operationsEnabled && !isCurrent);
    } else {
        QAction* checkout = menu.addAction(QStringLiteral("Checkout as Local Branch..."), [this, name] {
            // strip "origin/" prefix for local name suggestion
            const QString suggestion = name.mid(name.indexOf('/') + 1);
            emit sigCreateFromRequested(name);
            Q_UNUSED(suggestion)
        });
        checkout->setEnabled(_operationsEnabled);
        menu.addSeparator();
        QAction* merge = menu.addAction(QStringLiteral("Merge into Current Branch"),
                                        [this, name] {
            emit sigMergeRequested(name);
        });
        merge->setObjectName(QStringLiteral("branchMergeAction"));
        QAction* rebase = menu.addAction(QStringLiteral("Rebase Current Branch onto Here..."),
                                         [this, name] {
            emit sigRebaseRequested(name);
        });
        rebase->setObjectName(QStringLiteral("branchRebaseAction"));
        merge->setEnabled(_operationsEnabled);
        rebase->setEnabled(_operationsEnabled);
    }

    menu.exec(mapToGlobal(pos));
}
