#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "core/GitManager.h"
#include "core/GitTypes.h"
#include <QMainWindow>

class CommitGraphWidget;
class BranchListWidget;
class RepoListWidget;
class StatusWidget;
class DiffWidget;
class TerminalWidget;
class QSplitter;
class QLabel;
class QTabWidget;
class QAction;

/**
 * @brief 主窗口
 *
 * 布局：
 *   左列上：仓库切换列表（RepoListWidget）
 *   左列下：分支列表（BranchListWidget）
 *   中：提交图（CommitGraphWidget）
 *   右：状态/diff 面板（StatusWidget + DiffWidget，Tab 切换）
 *
 * 工具栏提供打开仓库、刷新、Pull、Push、Commit 操作。
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    // ---- 工具栏操作 ----
    void slotOpenRepo();
    void slotInitRepo();
    void slotCloneRepo();
    void slotFetch();
    void slotRemote();
    void slotStash();
    void slotRepositoryOperation();
    void slotRefresh();
    void slotPull();
    void slotPush();
    void slotCommit();
    void slotCancelOperation();

    // ---- 仓库切换 ----
    void slotRepoSelected(const QString& path);

    // ---- 子控件信号 ----
    void slotCommitSelected(const Git::Commit& commit);
    void slotFileSelected(const QString& filePath, bool staged, bool untracked);
    void slotStageFile(const QString& filePath);
    void slotUnstageFile(const QString& filePath);
    void slotStageAll();
    void slotUnstageAll();
    void slotIgnoreFile(const QString& filePath);
    void slotDiscardFile(const QString& filePath, bool untracked);
    void slotResolveFile(const QString& filePath, bool ours);
    void slotCheckoutBranch(const QString& branchName);
    void slotDeleteBranch(const QString& branchName, bool force);
    void slotCreateBranch(const QString& fromBranch);

    // ---- 标签操作 ----
    void slotCreateTag(const QString& commitHash);
    void slotDeleteTag(const QString& tagName);

    // ---- 错误处理 ----
    void slotGitError(const QString& message);
    void slotRepositoryOpened(const QString& path, bool success, const QString& error);
    void slotStateReady(const Git::RepositoryState& state);
    void slotOperationFinished(const QString& operation, bool success, const QString& message);
    void slotBusyChanged(bool busy);

private:
    enum class DiffSource { None, File, Commit };

    void setupToolBar();
    void setupCentralWidget();
    void connectSignals();
    void beginRepositoryTransition(const QString& path);

    /** @brief 打开仓库并刷新所有视图 */
    void openRepo(const QString& path);

    /** @brief 刷新提交图、分支列表、文件状态 */
    void refresh();

    /** @brief 在状态栏显示临时消息 */
    void showStatus(const QString& msg);

    Git::GitManager*   _git              {nullptr};

    RepoListWidget*    _repoList         {nullptr};
    CommitGraphWidget* _commitGraph      {nullptr};
    BranchListWidget*  _branchList       {nullptr};
    StatusWidget*      _statusWidget     {nullptr};
    DiffWidget*        _diffWidget       {nullptr};
    QTabWidget*        _rightTabs        {nullptr};
    TerminalWidget*    _terminalWidget   {nullptr};
    QLabel*            _repoLabel        {nullptr};
    QAction*           _cancelAction     {nullptr};
    QList<QAction*>    _repositoryActions;
    Git::RepositoryState _state;
    QStringList _stashes;
    DiffSource _requestedDiffSource {DiffSource::None};
    DiffSource _displayedDiffSource {DiffSource::None};
};

#endif // MAINWINDOW_H
