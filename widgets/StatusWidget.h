#ifndef STATUSWIDGET_H
#define STATUSWIDGET_H

#include "../core/GitTypes.h"
#include <QWidget>

class QListWidget;
class QListWidgetItem;
class QPushButton;
class QLabel;

/**
 * @brief 按冲突、工作区和暂存区分组的文件状态面板
 *
 * 用单一列表展示所有变更文件：
 *   - ☑ 勾选   → 已暂存，将被提交
 *   - ☐ 不勾选 → 未暂存，本次忽略
 *
 * 勾选状态变更时自动调用 stage / unstage；
 * 右键菜单可将未追踪文件加入 .gitignore。
 */
class StatusWidget : public QWidget {
    Q_OBJECT

public:
    explicit StatusWidget(QWidget* parent = nullptr);

    /** @brief 刷新文件列表（不触发 stage/unstage 信号）*/
    void setFiles(const QVector<Git::File>& files);

signals:
    /** @brief 用户单击文件行，请求显示 diff */
    void sigFileSelected(const QString& filePath, bool staged, bool untracked);

    /** @brief 用户勾选文件，请求暂存 */
    void sigStageRequested(const QString& filePath);

    /** @brief 用户取消勾选，请求取消暂存 */
    void sigUnstageRequested(const QString& filePath);

    /** @brief 全部暂存 */
    void sigStageAllRequested();

    /** @brief 全部取消暂存 */
    void sigUnstageAllRequested();

    /** @brief 将文件加入 .gitignore */
    void sigIgnoreRequested(const QString& filePath);

private slots:
    void slotItemChanged(QListWidgetItem* item);
    void slotItemClicked(QListWidgetItem* item);
    void slotContextMenu(const QPoint& pos);

private:
    /** @brief 返回状态对应的显示字母和颜色 */
    static QPair<QString, QColor> statusStyle(Git::File::Status s);

    QLabel*      _summaryLabel {nullptr};
    QListWidget* _list         {nullptr};
    QPushButton* _stageAllBtn  {nullptr};
    QPushButton* _unstageAllBtn{nullptr};
};

#endif // STATUSWIDGET_H
