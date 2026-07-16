#ifndef BRANCHLISTWIDGET_H
#define BRANCHLISTWIDGET_H

#include "../core/GitTypes.h"
#include <QTreeWidget>

/**
 * @brief 分支列表面板
 *
 * 以树形结构展示本地分支和远端分支，支持通过右键菜单进行
 * 切换、创建、删除等操作。
 */
class BranchListWidget : public QTreeWidget {
    Q_OBJECT

public:
    explicit BranchListWidget(QWidget* parent = nullptr);

    /** @brief 刷新分支列表 */
    void setBranches(const QVector<Git::Branch>& branches);

signals:
    /** @brief 用户请求切换到指定分支 */
    void sigCheckoutRequested(const QString& branchName);

    /** @brief 用户请求删除分支（force=true 表示强制删除）*/
    void sigDeleteRequested(const QString& branchName, bool force);

    /** @brief 用户请求以此分支为基础新建分支 */
    void sigCreateFromRequested(const QString& fromBranch);

private slots:
    void slotContextMenu(const QPoint& pos);
    void slotItemDoubleClicked(QTreeWidgetItem* item, int column);

private:
    QTreeWidgetItem* _localRoot  {nullptr};
    QTreeWidgetItem* _remoteRoot {nullptr};
};

#endif // BRANCHLISTWIDGET_H
