#ifndef REPOMANAGERDIALOG_H
#define REPOMANAGERDIALOG_H

#include <QDialog>
#include <QVector>

class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;

/** @brief 仓库历史条目 */
struct RepoEntry {
    QString path;   ///< 仓库绝对路径
    QString group;  ///< 所属分组名，空则归入 "Default"
};

/**
 * @brief 仓库管理对话框
 *
 * 展示历史仓库列表，支持分组管理。用户可：
 *  - 从列表中选择并打开已有仓库
 *  - 通过文件对话框浏览新仓库
 *  - 右键菜单管理分组、移动、删除条目
 *
 * 历史记录持久化在 QSettings 中（JSON 格式）。
 */
class RepoManagerDialog : public QDialog {
    Q_OBJECT

public:
    explicit RepoManagerDialog(QWidget* parent = nullptr);

    /** @brief 用户最终确认打开的仓库路径；取消则为空 */
    QString selectedPath() const { return _selectedPath; }

    /**
     * @brief 将仓库路径写入历史记录
     *
     * 若路径已存在则移到列表最前，否则追加到 "Default" 分组最前。
     * 通常在 MainWindow 成功 openRepository 后调用。
     */
    static void recordRepo(const QString& path);

private slots:
    void slotBrowse();
    void slotOpenSelected();
    void slotNewGroup();
    void slotContextMenu(const QPoint& pos);
    void slotItemDoubleClicked(QTreeWidgetItem* item, int col);
    void slotSelectionChanged();

private:
    static QVector<RepoEntry> loadEntries();
    static void               saveEntries(const QVector<RepoEntry>& entries);

    void rebuildTree();
    void tryAccept(const QString& path);
    QStringList allGroups() const;

    QTreeWidget*       _tree     {nullptr};
    QPushButton*       _openBtn  {nullptr};
    QVector<RepoEntry> _entries;
    QString            _selectedPath;
};

#endif // REPOMANAGERDIALOG_H
