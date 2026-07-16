#ifndef COMMITGRAPHWIDGET_H
#define COMMITGRAPHWIDGET_H

#include "../core/GitTypes.h"
#include <QAbstractTableModel>
#include <QHash>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QWidget>

class QAction;
class QCheckBox;
class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
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
    ~CommitGraphWidget() override;

    /** @brief 加载提交列表（已含 lane 布局信息）*/
    void setCommits(const QVector<Git::Commit>& commits);

    /** @brief 用新查询的第一页替换当前提交历史。 */
    void resetHistory(const Git::CommitHistoryPage& page);

    /** @brief 增量追加一页提交历史，不重置当前选择。 */
    void appendHistory(const Git::CommitHistoryPage& page);

    /** @brief 设置历史查询加载状态。 */
    void setHistoryLoading(bool loading);

    /** @brief 更新分支过滤选项。 */
    void setBranches(const QVector<Git::Branch>& branches);

    /** @brief 返回当前界面对应的历史查询。 */
    Git::CommitHistoryQuery historyQuery() const;

    /** @brief 清空视图 */
    void clear();

    /** @brief 按完整或唯一短 hash 选择已加载提交。 */
    void selectCommit(const QString& hash);

signals:
    /** @brief 用户点击某行时发射 */
    void sigCommitSelected(const Git::Commit& commit);

    /** @brief 用户在某提交处请求新建标签 */
    void sigCreateTagRequested(const QString& commitHash);

    /** @brief 用户请求删除指定标签 */
    void sigDeleteTagRequested(const QString& tagName);

    /** @brief 历史搜索或过滤条件发生变化。 */
    void sigHistoryQueryChanged(const Git::CommitHistoryQuery& query);

    /** @brief 用户请求继续加载历史。 */
    void sigLoadMoreRequested();

    /** @brief 用户请求查看指定提交（包括父/子提交）的详情。 */
    void sigCommitDetailsRequested(const QString& commitHash);

    /** @brief 当前提交选择已清空。 */
    void sigCommitSelectionCleared();

private slots:
    void slotSelectionChanged();
    void slotContextMenu(const QPoint& pos);

private:
    void scheduleQueryChanged();
    void emitQueryChanged();
    void updateFooter();
    void updateGraphVisibility();
    void ensureGraphColumnWidth();
    void saveSettings() const;

    QLineEdit*          _searchEdit       {nullptr};
    QComboBox*          _branchCombo      {nullptr};
    QLineEdit*          _authorEdit       {nullptr};
    QLineEdit*          _pathEdit         {nullptr};
    QCheckBox*          _fromDateCheck    {nullptr};
    QDateEdit*          _fromDateEdit     {nullptr};
    QCheckBox*          _toDateCheck      {nullptr};
    QDateEdit*          _toDateEdit       {nullptr};
    QComboBox*          _orderCombo       {nullptr};
    QAction*            _showGraphAction  {nullptr};
    QAction*            _showRefsAction   {nullptr};
    QTimer*             _queryTimer       {nullptr};
    QTreeView*           _view     {nullptr};
    CommitGraphModel*    _model    {nullptr};
    CommitGraphDelegate* _delegate {nullptr};
    QLabel*              _loadedLabel     {nullptr};
    QPushButton*         _loadMoreButton  {nullptr};
    bool                 _hasMore         {false};
    bool                 _historyLoading  {false};
    bool                 _queryPending    {false};
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
    void appendCommits(const QVector<Git::Commit>& commits);
    void setShowReferences(bool show);

    const Git::Commit& commitAt(int row) const;
    int rowForHash(const QString& hash) const;
    QStringList childHashes(const QString& parentHash) const;
    int maxLaneCount() const { return _maxLane; }

    int      rowCount(const QModelIndex& parent = {}) const override;
    int      columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    void rebuildIndex();

    QVector<Git::Commit> _commits;
    QHash<QString, int>  _rowByHash;
    QHash<QString, QStringList> _childrenByParent;
    int                  _maxLane {0};
    bool                 _showReferences {true};
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
