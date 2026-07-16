#ifndef REPOLISTWIDGET_H
#define REPOLISTWIDGET_H

#include <QWidget>
#include <QVector>

class QTreeWidget;
class QTreeWidgetItem;

/** @brief 仓库历史条目 */
struct RepoEntry {
    QString path;   ///< 仓库绝对路径
    QString group;  ///< 所属分组，空值等同于 "Default"
};

/**
 * @brief 左侧仓库切换面板
 *
 * 以树形结构（分组 → 仓库）展示历史仓库列表，
 * 点击仓库条目发射 sigRepoSwitchRequested，
 * 支持分组管理、移动、删除等右键操作。
 * 数据持久化在 QSettings 中（JSON 格式）。
 */
class RepoListWidget : public QWidget {
    Q_OBJECT

public:
    explicit RepoListWidget(QWidget* parent = nullptr);

    /** @brief 标记当前活跃仓库（在列表中高亮） */
    void setCurrentRepo(const QString& path);

    /**
     * @brief 将路径写入历史记录
     *
     * 若路径已存在则移到最前；否则追加到 "Default" 分组最前。
     * 在 MainWindow 成功打开仓库后调用。
     */
    static void recordRepo(const QString& path);

signals:
    /** @brief 用户点击某个仓库条目时发射 */
    void sigRepoSwitchRequested(const QString& path);

private slots:
    void slotAddRepo();
    void slotContextMenu(const QPoint& pos);
    void slotItemClicked(QTreeWidgetItem* item, int col);

private:
    static QVector<RepoEntry> loadEntries();
    static void               saveEntries(const QVector<RepoEntry>& entries);

    void        rebuild();
    QStringList allGroups() const;

    QTreeWidget*       _tree        {nullptr};
    QVector<RepoEntry> _entries;
    QString            _currentPath;
};

#endif // REPOLISTWIDGET_H
