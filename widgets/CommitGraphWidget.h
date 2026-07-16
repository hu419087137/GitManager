#ifndef COMMITGRAPHWIDGET_H
#define COMMITGRAPHWIDGET_H

#include "../core/GitTypes.h"
#include <QAbstractTableModel>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QWidget>

class CommitGraphModel;
class CommitGraphDelegate;

/**
 * @brief 提交历史图形视图
 *
 * 使用 QTreeView + 自定义 Model/Delegate 渲染 Git 提交图。
 * 第 0 列绘制带颜色的 lane 拓扑图，其余列展示 hash、消息、作者、日期。
 */
class CommitGraphWidget : public QWidget {
    Q_OBJECT

public:
    explicit CommitGraphWidget(QWidget* parent = nullptr);

    /** @brief 加载提交列表（已含 lane 布局信息）*/
    void setCommits(const QVector<Git::Commit>& commits);

    /** @brief 清空视图 */
    void clear();

signals:
    /** @brief 用户点击某行时发射 */
    void sigCommitSelected(const Git::Commit& commit);

    /** @brief 用户在某提交处请求新建标签 */
    void sigCreateTagRequested(const QString& commitHash);

    /** @brief 用户请求删除指定标签 */
    void sigDeleteTagRequested(const QString& tagName);

private slots:
    void slotSelectionChanged();
    void slotContextMenu(const QPoint& pos);

private:
    QTreeView*           _view     {nullptr};
    CommitGraphModel*    _model    {nullptr};
    CommitGraphDelegate* _delegate {nullptr};
};

// ============================================================
// Model
// ============================================================

class CommitGraphModel : public QAbstractTableModel {
    Q_OBJECT

public:
    explicit CommitGraphModel(QObject* parent = nullptr);

    /** @brief 列定义 */
    enum Column { ColGraph = 0, ColHash, ColMessage, ColAuthor, ColDate, ColCount };

    void setCommits(const QVector<Git::Commit>& commits);

    const Git::Commit& commitAt(int row) const;
    int maxLaneCount() const { return _maxLane; }

    int      rowCount(const QModelIndex& parent = {}) const override;
    int      columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QVector<Git::Commit> _commits;
    int                  _maxLane {0};
};

// ============================================================
// Delegate — 绘制第 0 列的 lane 图形
// ============================================================

class CommitGraphDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit CommitGraphDelegate(CommitGraphModel* model, QObject* parent = nullptr);

    void  paint(QPainter* painter, const QStyleOptionViewItem& option,
                const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;

private:
    static constexpr int kLaneWidth  = 16; ///< 每条 lane 的像素宽度
    static constexpr int kNodeRadius = 4;  ///< 提交节点圆半径
    static constexpr int kRowHeight  = 22; ///< 行高

    /** @brief lane 列的像素 X 中心 */
    static int laneX(int lane, int cellLeft) {
        return cellLeft + lane * kLaneWidth + kLaneWidth / 2;
    }

    /** @brief 在 lanes 中查找 hash 所在列，未找到返回 -1 */
    static int findLane(const QVector<QString>& lanes, const QString& hash);

    CommitGraphModel* _model {nullptr};
};

#endif // COMMITGRAPHWIDGET_H
