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
        auto* item = new QTreeWidgetItem({b.name});
        item->setData(0, Qt::UserRole,     b.name);
        item->setData(0, Qt::UserRole + 1, b.isRemote);

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
    if (!isRemote) {
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

    QMenu menu(this);

    if (!isRemote) {
        menu.addAction(QStringLiteral("Checkout"), [this, name] {
            emit sigCheckoutRequested(name);
        });
        menu.addAction(QStringLiteral("New Branch from Here..."), [this, name] {
            emit sigCreateFromRequested(name);
        });
        menu.addSeparator();
        menu.addAction(QStringLiteral("Delete Branch"), [this, name] {
            const auto btn = QMessageBox::question(
                this,
                QStringLiteral("Delete Branch"),
                QStringLiteral("Delete branch '%1'?").arg(name));
            if (btn == QMessageBox::Yes)
                emit sigDeleteRequested(name, false);
        });
        menu.addAction(QStringLiteral("Force Delete Branch"), [this, name] {
            const auto btn = QMessageBox::question(
                this,
                QStringLiteral("Force Delete"),
                QStringLiteral("Force delete branch '%1'?").arg(name));
            if (btn == QMessageBox::Yes)
                emit sigDeleteRequested(name, true);
        });
    } else {
        menu.addAction(QStringLiteral("Checkout as Local Branch..."), [this, name] {
            // strip "origin/" prefix for local name suggestion
            const QString suggestion = name.mid(name.indexOf('/') + 1);
            emit sigCreateFromRequested(name);
            Q_UNUSED(suggestion)
        });
    }

    menu.exec(mapToGlobal(pos));
}
