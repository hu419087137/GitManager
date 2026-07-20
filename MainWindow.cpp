#include "MainWindow.h"
#include "widgets/CommitGraphWidget.h"
#include "widgets/BranchListWidget.h"
#include "widgets/RepoListWidget.h"
#include "widgets/StatusWidget.h"
#include "widgets/CompareWidget.h"
#include "widgets/DiffWidget.h"
#include "widgets/TerminalWidget.h"
#include "dialogs/CommitDialog.h"
#include "dialogs/BranchDialog.h"
#include "dialogs/RebaseDialog.h"
#include "dialogs/ResetDialog.h"
#include "dialogs/WorktreeDialog.h"
#include "dialogs/SubmoduleDialog.h"
#include "dialogs/LfsDialog.h"
#include "dialogs/HostingDialog.h"
#include "dialogs/HostingChangesDialog.h"
#include "dialogs/HostingIssuesDialog.h"
#include "dialogs/HostingReviewDialog.h"
#include "dialogs/GitDiagnosticsDialog.h"
#include "dialogs/HooksDialog.h"
#include "dialogs/ExternalToolsDialog.h"
#include "core/RepositoryWatcher.h"
#include "core/CredentialStore.h"
#include "core/ExternalToolService.h"
#include "core/AppSettings.h"
#include "dialogs/SettingsDialog.h"
#include "widgets/NotificationWidget.h"
#include "widgets/WelcomeWidget.h"

#include <QToolBar>
#include <QAction>
#include <QEvent>
#include <QMenu>
#include <QSplitter>
#include <QTabWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QVBoxLayout>
#include <QSettings>
#include <QDir>
#include <QLineEdit>
#include <QRegularExpression>
#include <QToolButton>
#include <QDesktopServices>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {

bool supportsContinueAbort(Git::RepositoryOperation operation)
{
    return operation == Git::RepositoryOperation::Merge
        || operation == Git::RepositoryOperation::Rebase
        || operation == Git::RepositoryOperation::CherryPick
        || operation == Git::RepositoryOperation::Revert;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Git Manager"));
    resize(1280, 760);

    _git = new Git::GitManager(this);
    _watcher = new Git::RepositoryWatcher(this);

    setupToolBar();
    setupCentralWidget();
    connectSignals();
    Git::AppSettings::restoreWindow(this);
    const int activePanel = property("activePanel").toInt();
    if (_rightTabs && activePanel >= 0 && activePanel < _rightTabs->count())
        _rightTabs->setCurrentIndex(activePanel);

    statusBar()->showMessage(QStringLiteral("Ready — open a repository to begin."));

    // 恢复上次打开的仓库
    const QString lastRepo = QSettings().value(QStringLiteral("lastRepo")).toString();
    if (!lastRepo.isEmpty())
        openRepo(lastRepo);
}

// ============================================================
// Setup
// ============================================================

void MainWindow::setupToolBar()
{
    auto* tb = addToolBar(QStringLiteral("Main"));
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    auto* openAction    = tb->addAction(QStringLiteral("Open Repo"));
    auto* refreshAction = tb->addAction(QStringLiteral("Refresh"));
    tb->addSeparator();

    auto* gitMenu = new QMenu(QStringLiteral("Git"), tb);
    auto* initAction = gitMenu->addAction(QStringLiteral("Init"));
    auto* cloneAction = gitMenu->addAction(QStringLiteral("Clone"));
    gitMenu->addSeparator();
    auto* pullAction = gitMenu->addAction(QStringLiteral("Pull"));
    auto* fetchAction = gitMenu->addAction(QStringLiteral("Fetch"));
    auto* pushAction = gitMenu->addAction(QStringLiteral("Push"));
    auto* forcePushAction = gitMenu->addAction(QStringLiteral("Force Push with Lease"));
    gitMenu->addSeparator();
    auto* commitAction = gitMenu->addAction(QStringLiteral("Commit"));
    auto* stashAction = gitMenu->addAction(QStringLiteral("Stash"));
    auto* remoteAction = gitMenu->addAction(QStringLiteral("Remote"));
    auto* worktreeAction = gitMenu->addAction(QStringLiteral("Worktree"));
    auto* submoduleAction = gitMenu->addAction(QStringLiteral("Submodule"));
    auto* lfsAction = gitMenu->addAction(QStringLiteral("Git LFS"));
    auto* hostingAction = gitMenu->addAction(QStringLiteral("Hosting"));
    auto* diagnosticsAction = gitMenu->addAction(QStringLiteral("Connection Diagnostics"));
    auto* hooksAction = gitMenu->addAction(QStringLiteral("Hooks"));
    auto* externalDiffAction = gitMenu->addAction(QStringLiteral("Open Current Diff Externally"));
    auto* externalToolsAction = gitMenu->addAction(QStringLiteral("External Diff / Merge Tools"));
    auto* settingsAction = gitMenu->addAction(QStringLiteral("Settings"));
    _operationAction = gitMenu->addAction(QStringLiteral("Continue/Abort"));

    auto* gitButton = new QToolButton(tb);
    gitButton->setText(QStringLiteral("Git"));
    gitButton->setPopupMode(QToolButton::InstantPopup);
    gitButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    gitButton->setMenu(gitMenu);
    QAction* gitMenuAction = tb->addWidget(gitButton);

    _cancelAction       = tb->addAction(QStringLiteral("Cancel"));
    _cancelAction->setEnabled(false);
    _operationAction->setEnabled(false);
    _repositoryActions = {refreshAction, gitMenuAction, pullAction, fetchAction,
                          pushAction, forcePushAction, commitAction, stashAction, remoteAction,
                          worktreeAction, submoduleAction, lfsAction, hostingAction,
                          diagnosticsAction, _operationAction};
    _repositoryActions.append(hooksAction);
    _repositoryActions.append(externalDiffAction);
    _repositoryActions.append(externalToolsAction);
    _inactiveDuringOperationActions = {pullAction, pushAction, forcePushAction,
                                       commitAction, stashAction};

    _repoLabel = new QLabel(QStringLiteral("  No repository"), this);
    statusBar()->addPermanentWidget(_repoLabel);

    connect(openAction,    &QAction::triggered, this, &MainWindow::slotOpenRepo);
    connect(initAction, &QAction::triggered, this, &MainWindow::slotInitRepo);
    connect(cloneAction, &QAction::triggered, this, &MainWindow::slotCloneRepo);
    connect(refreshAction, &QAction::triggered, this, &MainWindow::slotRefresh);
    connect(pullAction,    &QAction::triggered, this, &MainWindow::slotPull);
    connect(fetchAction, &QAction::triggered, this, &MainWindow::slotFetch);
    connect(pushAction,    &QAction::triggered, this, &MainWindow::slotPush);
    connect(forcePushAction, &QAction::triggered,
            this, &MainWindow::slotPushWithLease);
    connect(commitAction,  &QAction::triggered, this, &MainWindow::slotCommit);
    connect(stashAction, &QAction::triggered, this, &MainWindow::slotStash);
    connect(remoteAction, &QAction::triggered, this, &MainWindow::slotRemote);
    connect(worktreeAction, &QAction::triggered, this, &MainWindow::slotWorktree);
    connect(submoduleAction, &QAction::triggered, this, &MainWindow::slotSubmodule);
    connect(lfsAction, &QAction::triggered, this, &MainWindow::slotLfs);
    connect(hostingAction, &QAction::triggered, this, &MainWindow::slotHosting);
    connect(diagnosticsAction, &QAction::triggered,
            this, &MainWindow::slotGitDiagnostics);
    connect(hooksAction, &QAction::triggered, this, &MainWindow::slotHooks);
    connect(externalDiffAction, &QAction::triggered,
            this, &MainWindow::slotExternalDiff);
    connect(externalToolsAction, &QAction::triggered,
            this, &MainWindow::slotExternalTools);
    connect(settingsAction, &QAction::triggered, this, [this] {
        SettingsDialog(this).exec();
    });
    connect(_operationAction, &QAction::triggered,
            this, &MainWindow::slotRepositoryOperation);
    connect(_cancelAction, &QAction::triggered, this, &MainWindow::slotCancelOperation);
}

MainWindow::~MainWindow()
{
    if (_rightTabs)
        setProperty("activePanel", _rightTabs->currentIndex());
    Git::AppSettings::saveWindow(this);
}

#ifdef Q_OS_WIN
bool MainWindow::nativeEvent(const QByteArray& eventType, void* message,
                             qintptr* result)
{
    Q_UNUSED(eventType);
    auto* msg = static_cast<MSG*>(message);
    if (msg && msg->message == WM_NCLBUTTONDBLCLK
        && msg->wParam == HTCAPTION) {
        if (isMaximized())
            showNormal();
        else
            showMaximized();
        if (result)
            *result = 0;
        return true;
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif

bool MainWindow::event(QEvent* event)
{
    if (event && event->type() == QEvent::WindowActivate && _watcher)
        _watcher->notifyWindowActivated();
    return QMainWindow::event(event);
}

void MainWindow::setupCentralWidget()
{
    _repoList     = new RepoListWidget(this);
    _commitGraph  = new CommitGraphWidget(this);
    _branchList   = new BranchListWidget(this);
    _statusWidget = new StatusWidget(this);
    _diffWidget   = new DiffWidget(this);
    _compareWidget = new CompareWidget(this);
    _terminalWidget = new TerminalWidget(this);
    _notification = new NotificationWidget(this);
    _welcomeWidget = new WelcomeWidget(this);

    auto* diffPanel = new QWidget(this);
    auto* diffLayout = new QVBoxLayout(diffPanel);
    diffLayout->setContentsMargins(0, 0, 0, 0);
    diffLayout->setSpacing(0);
    diffLayout->addWidget(_compareWidget);
    diffLayout->addWidget(_diffWidget, 1);

    // 右侧 Tab：Status | Diff
    _rightTabs = new QTabWidget(this);
    _rightTabs->addTab(_statusWidget, QStringLiteral("Status"));
    _rightTabs->addTab(diffPanel, QStringLiteral("Diff"));
    connect(_rightTabs, &QTabWidget::currentChanged, this, [this](int index) {
        setProperty("activePanel", index);
    });

    // 内部 splitter：中（提交图）| 右（status/diff）
    auto* innerSplitter = new QSplitter(Qt::Horizontal, this);
    innerSplitter->setObjectName(QStringLiteral("contentSplitterV2"));
    innerSplitter->addWidget(_commitGraph);
    innerSplitter->addWidget(_rightTabs);
    innerSplitter->setStretchFactor(0, 5);
    innerSplitter->setStretchFactor(1, 1);
    innerSplitter->setSizes({920, 260});

    // 左列 splitter：上（仓库列表）| 下（分支列表）
    auto* leftSplitter = new QSplitter(Qt::Vertical, this);
    leftSplitter->setObjectName(QStringLiteral("navigationSplitter"));
    leftSplitter->addWidget(_repoList);
    leftSplitter->addWidget(_branchList);
    leftSplitter->setStretchFactor(0, 1);
    leftSplitter->setStretchFactor(1, 1);
    leftSplitter->setSizes({220, 260});

    // 外部横向 splitter：左列 | 内容区
    auto* outerSplitter = new QSplitter(Qt::Horizontal, this);
    outerSplitter->setObjectName(QStringLiteral("workspaceSplitter"));
    outerSplitter->addWidget(leftSplitter);
    outerSplitter->addWidget(innerSplitter);
    outerSplitter->setStretchFactor(0, 0);
    outerSplitter->setStretchFactor(1, 1);
    outerSplitter->setSizes({200, 1080});

    // 主竖向 splitter：上（内容区）| 下（终端）
    auto* mainSplitter = new QSplitter(Qt::Vertical, this);
    mainSplitter->setObjectName(QStringLiteral("terminalSplitter"));
    mainSplitter->addWidget(outerSplitter);
    mainSplitter->addWidget(_terminalWidget);
    mainSplitter->setStretchFactor(0, 5);
    mainSplitter->setStretchFactor(1, 1);
    mainSplitter->setSizes({560, 160});

    _centralStack = new QStackedWidget(this);
    _centralStack->addWidget(_welcomeWidget);
    _centralStack->addWidget(mainSplitter);
    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(_notification);
    centralLayout->addWidget(_centralStack, 1);
    setCentralWidget(central);
    connect(_welcomeWidget, &WelcomeWidget::sigOpenRequested,
            this, &MainWindow::slotOpenRepo);
    connect(_welcomeWidget, &WelcomeWidget::sigInitRequested,
            this, &MainWindow::slotInitRepo);
    connect(_welcomeWidget, &WelcomeWidget::sigCloneRequested,
            this, &MainWindow::slotCloneRepo);
}

void MainWindow::connectSignals()
{
    connect(_git, &Git::GitManager::sigError,
            this, &MainWindow::slotGitError);
    connect(_git, &Git::GitManager::sigInfo,
            this, &MainWindow::showStatus);
    connect(_git, &Git::GitManager::sigCommandStarted,
            _terminalWidget, &TerminalWidget::beginCommand);
    connect(_git, &Git::GitManager::sigCommandOutput,
            _terminalWidget, &TerminalWidget::appendOutput);
    connect(_git, &Git::GitManager::sigRepositoryOpened,
            this, &MainWindow::slotRepositoryOpened);
    connect(_git, &Git::GitManager::sigStateReady,
            this, &MainWindow::slotStateReady);
    connect(_git, &Git::GitManager::sigLfsStateReady, this,
            [this](const Git::LfsState& state, const QString& error) {
        LfsDialog dialog(state, error, this);
        connect(&dialog, &LfsDialog::sigTrackRequested, _git,
                &Git::GitManager::trackLfsPattern);
        connect(&dialog, &LfsDialog::sigUntrackRequested, _git,
                &Git::GitManager::untrackLfsPattern);
        connect(&dialog, &LfsDialog::sigLockRequested, _git,
                &Git::GitManager::lockLfsPath);
        connect(&dialog, &LfsDialog::sigUnlockRequested, _git,
                &Git::GitManager::unlockLfsPath);
        dialog.exec();
    });
    connect(_git, &Git::GitManager::sigHostingRemotesReady, this,
            [this](const QVector<Git::HostingRemoteInfo>& remotes,
                   const QString& error) {
        QHash<int, QString> savedTokens;
        for (const auto& remote : remotes) {
            const int provider = static_cast<int>(remote.provider);
            if (savedTokens.contains(provider)) continue;
            QString token;
            QString credentialError;
            if (Git::CredentialStore::read(remote.provider, &token, &credentialError)
                && !token.isEmpty()) {
                savedTokens.insert(provider, token);
                _git->setHostingToken(remote.provider, token);
            } else if (!credentialError.isEmpty()) {
                showStatus(credentialError);
            }
        }
        HostingDialog dialog(remotes, error, savedTokens, this);
        connect(&dialog, &HostingDialog::sigOpenUrlRequested, this,
                [](const QString& url) {
            QDesktopServices::openUrl(QUrl(url));
        });
        connect(&dialog, &HostingDialog::sigLoadChangesRequested, this,
                [this](const Git::HostingRemoteInfo& remote, const QString& token) {
            _git->setHostingToken(remote.provider, token);
            _git->requestHostingChanges(remote);
        });
        connect(&dialog, &HostingDialog::sigLoadIssuesRequested, this,
                [this](const Git::HostingRemoteInfo& remote, const QString& token) {
            _git->setHostingToken(remote.provider, token);
            _git->requestHostingIssues(remote);
        });
        connect(&dialog, &HostingDialog::sigStoreTokenRequested, this,
                [this](Git::HostingProvider provider, const QString& token) {
            QString credentialError;
            if (!Git::CredentialStore::write(provider, token, &credentialError))
                QMessageBox::warning(this, QStringLiteral("Credential Store"), credentialError);
        });
        connect(&dialog, &HostingDialog::sigForgetTokenRequested, this,
                [this](Git::HostingProvider provider) {
            _git->setHostingToken(provider, {});
            QString credentialError;
            if (!Git::CredentialStore::remove(provider, &credentialError))
                QMessageBox::warning(this, QStringLiteral("Credential Store"), credentialError);
        });
        dialog.exec();
    });
    connect(_git, &Git::GitManager::sigHostingChangesReady, this,
            [this](const Git::HostingRemoteInfo& remote,
                   const QVector<Git::HostingChangeInfo>& changes,
                   const QString& error) {
        HostingChangesDialog dialog(remote, changes, error, this);
        connect(&dialog, &HostingChangesDialog::sigOpenRequested, this,
                [](const QString& url) { QDesktopServices::openUrl(QUrl(url)); });
        connect(&dialog, &HostingChangesDialog::sigReviewFilesRequested, this,
                [this](const Git::HostingRemoteInfo& remote,
                       const Git::HostingChangeInfo& change) {
            _git->requestHostingReviewFiles(remote, change);
        });
        dialog.exec();
    });
    connect(_git, &Git::GitManager::sigHostingIssuesReady, this,
            [this](const Git::HostingRemoteInfo& remote,
                   const QVector<Git::HostingIssueInfo>& issues,
                   const QString& error) {
        HostingIssuesDialog dialog(remote, issues, error, this);
        connect(&dialog, &HostingIssuesDialog::sigOpenRequested, this,
                [](const QString& url) { QDesktopServices::openUrl(QUrl(url)); });
        dialog.exec();
    });
    connect(_git, &Git::GitManager::sigHostingReviewFilesReady, this,
            [this](const Git::HostingRemoteInfo& remote,
                   const Git::HostingChangeInfo& change,
                   const QVector<Git::HostingReviewFile>& files,
                   const QString& error) {
        HostingReviewDialog dialog(remote, change, files, error, this);
        connect(&dialog, &HostingReviewDialog::sigOpenFileRequested, this,
                [](const QString& url) { QDesktopServices::openUrl(QUrl(url)); });
        connect(&dialog, &HostingReviewDialog::sigCommentRequested, this,
                [this](const Git::HostingRemoteInfo& remote,
                       const Git::HostingChangeInfo& change,
                       const Git::HostingReviewFile& file,
                       int line, const QString& body) {
            _git->postHostingReviewComment(remote, change, file, line, body);
        });
        dialog.exec();
    });
    connect(_git, &Git::GitManager::sigHostingReviewCommentFinished, this,
            [this](bool success, const QString& message) {
        if (success) QMessageBox::information(this, QStringLiteral("Code Review"), message);
        else QMessageBox::warning(this, QStringLiteral("Code Review"), message);
    });
    connect(_git, &Git::GitManager::sigGitDiagnosticsReady, this,
            [this](const Git::GitDiagnosticReport& report, const QString& error) {
        GitDiagnosticsDialog dialog(report, error, this);
        connect(&dialog, &GitDiagnosticsDialog::sigTestRemoteRequested,
                _git, &Git::GitManager::testRemoteConnection);
        dialog.exec();
    });
    connect(_git, &Git::GitManager::sigRemoteConnectionTestFinished, this,
            [this](bool success, const QString& message) {
        if (success) QMessageBox::information(this, QStringLiteral("Remote Test"), message);
        else QMessageBox::warning(this, QStringLiteral("Remote Test"), message);
    });
    connect(_git, &Git::GitManager::sigHooksReady, this,
            [this](const QVector<Git::HookInfo>& hooks, const QString& error) {
        HooksDialog(hooks, error, this).exec();
    });
    connect(_git, &Git::GitManager::sigHookFinished, this,
            [this](const Git::HookResult& result) {
        const QString text = QStringLiteral("%1 (exit %2)%3%4")
            .arg(result.name).arg(result.exitCode)
            .arg(result.timedOut ? QStringLiteral(" — timed out") : QString())
            .arg(result.output.isEmpty() ? QString() : QStringLiteral("\n\n") + result.output);
        if (result.success) QMessageBox::information(this, QStringLiteral("Git Hook"), text);
        else QMessageBox::warning(this, QStringLiteral("Git Hook Failed"), text);
    });
    connect(_git, &Git::GitManager::sigCommitHistoryReady, this,
            [this](const Git::CommitHistoryPage& page) {
        if (page.offset == 0 || page.resetRequired)
            _commitGraph->resetHistory(page);
        else
            _commitGraph->appendHistory(page);
    });
    connect(_git, &Git::GitManager::sigCommitHistoryLoading,
            _commitGraph, &CommitGraphWidget::setHistoryLoading);
    connect(_git, &Git::GitManager::sigResetPreviewReady,
            this, &MainWindow::slotResetPreviewReady);
    connect(_git, &Git::GitManager::sigRebasePlanReady,
            this, &MainWindow::slotRebasePlanReady);
    connect(_git, &Git::GitManager::sigHistoryOperationFinished,
            this, &MainWindow::slotHistoryOperationFinished);
    connect(_git, &Git::GitManager::sigDiffReady, this,
            [this](const QString& diff, const QString&, bool staged,
                   bool hunkActionsEnabled) {
        const DiffSource source = hunkActionsEnabled
            ? DiffSource::File : DiffSource::Commit;
        _requestedDiffSource = source;
        _displayedDiffSource = source;
        const int actions = !hunkActionsEnabled ? DiffWidget::NoAction
            : staged ? DiffWidget::UnstageAction
                     : DiffWidget::StageAction | DiffWidget::DiscardAction;
        _diffWidget->setDiff(diff, actions);
        _rightTabs->setCurrentIndex(1);
    });
    connect(_git, &Git::GitManager::sigOperationFinished,
            this, &MainWindow::slotOperationFinished);
    connect(_git, &Git::GitManager::sigBusyChanged,
            this, &MainWindow::slotBusyChanged);
    connect(_watcher, &Git::RepositoryWatcher::sigRefreshRequested, this,
            [this] {
        if (_git->isValid() && !_git->isBusy())
            _git->refreshFromWatcher();
    });
    connect(_git, &Git::GitManager::sigStashesReady, this, [this](const QStringList& entries) {
        _stashes = entries;
        if (entries.isEmpty()) QMessageBox::information(this, QStringLiteral("Stashes"), QStringLiteral("No stashes."));
        else {
            bool ok=false;
            const QString item=QInputDialog::getItem(this,QStringLiteral("Stashes"),QStringLiteral("Select stash:"),entries,0,false,&ok);
            if (!ok) return;
            const QString ref=item.section('\t',0,0);
            const QStringList actions={QStringLiteral("Apply"),QStringLiteral("Pop"),QStringLiteral("Drop")};
            const QString action=QInputDialog::getItem(this,QStringLiteral("Stash Action"),QStringLiteral("Action:"),actions,0,false,&ok);
            if (!ok) return;
            if(action==actions[0])_git->stashApply(ref,false);
            else if(action==actions[1])_git->stashApply(ref,true);
            else if(QMessageBox::question(this,QStringLiteral("Drop Stash"),QStringLiteral("Delete %1 permanently?").arg(ref))==QMessageBox::Yes)_git->stashDrop(ref);
        }
    });
    connect(_diffWidget, &DiffWidget::sigHunkActionRequested, this,
            [this](const QString& patch, int action) {
        if (_git->isBusy() || !_git->isValid())
            return;
        if (action == 2 && QMessageBox::warning(this, QStringLiteral("Discard Hunk"),
            QStringLiteral("Discard the selected hunk permanently?"), QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel) != QMessageBox::Yes) return;
        if (_git->isBusy() || !_git->isValid())
            return;
        _git->applyPatch(patch, action == 0 || action == 1, action == 1 || action == 2);
    });

    connect(_repoList, &RepoListWidget::sigRepoSwitchRequested,
            this, &MainWindow::slotRepoSelected);

    connect(_commitGraph, &CommitGraphWidget::sigCommitSelected,
            this, &MainWindow::slotCommitSelected);
    connect(_commitGraph, &CommitGraphWidget::sigCommitDetailsRequested, this,
            [this](const QString& hash) {
        _requestedDiffSource = DiffSource::Commit;
        _git->fetchCommitDiff(hash);
    });
    connect(_commitGraph, &CommitGraphWidget::sigCompareBaseSelected, this,
            [this](const QString& revision) {
        _compareWidget->setBaseRevision(revision);
        if (_compareWidget->targetRevision().isEmpty())
            _compareWidget->setTargetRevision(_state.headName);
        _rightTabs->setCurrentIndex(1);
    });
    connect(_commitGraph, &CommitGraphWidget::sigCompareRequested,
            this, &MainWindow::slotCompareRevisions);
    connect(_commitGraph, &CommitGraphWidget::sigCommitSelectionCleared, this,
            [this] {
        if (_requestedDiffSource == DiffSource::Commit) {
            _git->cancelDiffRequest();
            _requestedDiffSource = DiffSource::None;
        }
        if (_displayedDiffSource == DiffSource::Commit) {
            _diffWidget->clearDiff();
            _displayedDiffSource = DiffSource::None;
        }
    });
    connect(_commitGraph, &CommitGraphWidget::sigHistoryQueryChanged,
            _git, &Git::GitManager::setCommitHistoryQuery);
    connect(_commitGraph, &CommitGraphWidget::sigLoadMoreRequested,
            _git, &Git::GitManager::loadMoreCommits);
    connect(_commitGraph, &CommitGraphWidget::sigCreateBranchRequested,
            this, &MainWindow::slotCreateBranch);
    connect(_commitGraph, &CommitGraphWidget::sigMergeRequested,
            this, &MainWindow::slotMergeRevision);
    connect(_commitGraph, &CommitGraphWidget::sigRebaseRequested,
            this, &MainWindow::slotRebaseRevision);
    connect(_commitGraph, &CommitGraphWidget::sigCherryPickRequested,
            this, &MainWindow::slotCherryPickCommit);
    connect(_commitGraph, &CommitGraphWidget::sigRevertRequested,
            this, &MainWindow::slotRevertCommit);
    connect(_commitGraph, &CommitGraphWidget::sigResetRequested,
            this, &MainWindow::slotResetCommit);

    connect(_statusWidget, &StatusWidget::sigFileSelected,
            this, &MainWindow::slotFileSelected);
    connect(_statusWidget, &StatusWidget::sigStageRequested,
            this, &MainWindow::slotStageFile);
    connect(_statusWidget, &StatusWidget::sigUnstageRequested,
            this, &MainWindow::slotUnstageFile);
    connect(_statusWidget, &StatusWidget::sigStageAllRequested,
            this, &MainWindow::slotStageAll);
    connect(_statusWidget, &StatusWidget::sigUnstageAllRequested,
            this, &MainWindow::slotUnstageAll);
    connect(_statusWidget, &StatusWidget::sigIgnoreRequested,
            this, &MainWindow::slotIgnoreFile);
    connect(_statusWidget, &StatusWidget::sigDiscardRequested,
            this, &MainWindow::slotDiscardFile);
    connect(_statusWidget, &StatusWidget::sigResolveRequested,
            this, &MainWindow::slotResolveFile);

    connect(_branchList, &BranchListWidget::sigCheckoutRequested,
            this, &MainWindow::slotCheckoutBranch);
    connect(_branchList, &BranchListWidget::sigDeleteRequested,
            this, &MainWindow::slotDeleteBranch);
    connect(_branchList, &BranchListWidget::sigCreateFromRequested,
            this, &MainWindow::slotCreateBranch);
    connect(_branchList, &BranchListWidget::sigRenameRequested,
            this, &MainWindow::slotRenameBranch);
    connect(_branchList, &BranchListWidget::sigPublishRequested,
            this, &MainWindow::slotPublishBranch);
    connect(_branchList, &BranchListWidget::sigUntrackRequested,
            this, &MainWindow::slotUntrackBranch);
    connect(_branchList, &BranchListWidget::sigMergeRequested,
            this, &MainWindow::slotMergeRevision);
    connect(_branchList, &BranchListWidget::sigRebaseRequested,
            this, &MainWindow::slotRebaseRevision);
    connect(_compareWidget, &CompareWidget::sigCompareRequested,
            this, &MainWindow::slotCompareRevisions);

    connect(_commitGraph, &CommitGraphWidget::sigCreateTagRequested,
            this, &MainWindow::slotCreateTag);
    connect(_commitGraph, &CommitGraphWidget::sigDeleteTagRequested,
            this, &MainWindow::slotDeleteTag);

    _git->setCommitHistoryQuery(_commitGraph->historyQuery());
}

// ============================================================
// Toolbar slots
// ============================================================

void MainWindow::slotOpenRepo()
{
    const QString path = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("Add Git Repository"),
        QDir::homePath());

    if (!path.isEmpty())
        slotRepoSelected(path);
}

void MainWindow::slotRefresh()
{
    if (!_git->isValid())
        return;
    refresh();
}

void MainWindow::slotPull()
{
    if (!_git->isValid()) return;
    showStatus(QStringLiteral("Pulling..."));
    _git->pull();
}

void MainWindow::slotPush()
{
    if (!_git->isValid()) return;
    showStatus(QStringLiteral("Pushing..."));
    if (!_state.unborn && _state.upstream.isEmpty()) {
        const QString remote = QInputDialog::getText(this, QStringLiteral("Publish Branch"),
            QStringLiteral("Remote name:"), QLineEdit::Normal, QStringLiteral("origin"));
        if (!remote.trimmed().isEmpty()) _git->pushSetUpstream(remote.trimmed(), _state.headName);
    } else _git->push();
}

void MainWindow::slotPushWithLease()
{
    if (!_git->isValid()) return;
    if (_state.unborn || _state.detached || _state.upstream.isEmpty()) {
        QMessageBox::information(
            this, QStringLiteral("Force Push with Lease"),
            QStringLiteral("The current branch must have an upstream before it can be force-pushed."));
        return;
    }
    const QString message = QStringLiteral(
        "Rewrite %1 from local branch %2?\n\n"
        "The push will proceed only if the remote branch still matches the last fetched state.")
        .arg(_state.upstream, _state.headName);
    if (QMessageBox::warning(this, QStringLiteral("Force Push with Lease"), message,
                             QMessageBox::Yes | QMessageBox::Cancel,
                             QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }
    showStatus(QStringLiteral("Force pushing with lease..."));
    _git->pushWithLease();
}

void MainWindow::slotCommit()
{
    if (!_git->isValid()) return;

    QStringList staged;
    for (const Git::File& f : _state.files) {
        if (f.isStaged())
            staged << f.path;
    }

    if (staged.isEmpty()) {
        QMessageBox::information(this,
            QStringLiteral("Nothing to Commit"),
            QStringLiteral("No staged files. Stage files before committing."));
        return;
    }

    CommitDialog dlg(staged, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const QString msg = dlg.commitMessage();
    if (msg.isEmpty())
        return;

    _git->commit(msg, dlg.amend(), dlg.signoff());
}

void MainWindow::slotInitRepo()
{
    const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("Initialize Repository"), QDir::homePath());
    if (!path.isEmpty()) {
        _git->initRepository(path);
        beginRepositoryTransition(path);
    }
}

void MainWindow::slotCloneRepo()
{
    const QString url = QInputDialog::getText(this, QStringLiteral("Clone Repository"), QStringLiteral("Repository URL:"));
    if (url.trimmed().isEmpty()) return;
    const QString parent = QFileDialog::getExistingDirectory(this, QStringLiteral("Clone Into"), QDir::homePath());
    if (parent.isEmpty()) return;
    QString name = url.section('/', -1);
    name.remove(QRegularExpression(QStringLiteral("\\.git$")));
    const QString destination = QDir(parent).filePath(name);
    _git->cloneRepository(url.trimmed(), destination);
    beginRepositoryTransition(destination);
}

void MainWindow::slotFetch() { if (_git->isValid()) _git->fetch(true); }

void MainWindow::slotRemote()
{
    const QStringList actions = {
        QStringLiteral("Add remote"),
        QStringLiteral("Remove remote"),
        QStringLiteral("Prune remote-tracking refs"),
        QStringLiteral("Push tag"),
        QStringLiteral("Delete remote tag"),
        QStringLiteral("Set session credentials"),
        QStringLiteral("Clear session credentials")
    };
    bool selected = false;
    const QString action = QInputDialog::getItem(this, QStringLiteral("Remote"), QStringLiteral("Action:"), actions, 0, false, &selected);
    if (!selected) return;
    if (action == actions[5]) {
        const QString username = QInputDialog::getText(
            this, QStringLiteral("Remote Credentials"), QStringLiteral("Username:"));
        bool accepted = false;
        const QString secret = QInputDialog::getText(
            this, QStringLiteral("Remote Credentials"),
            QStringLiteral("Password or access token:"), QLineEdit::Password,
            {}, &accepted);
        if (accepted) {
            _git->setRemoteCredentials(username, secret);
            showStatus(QStringLiteral("Remote credentials set for this session."));
        }
        return;
    }
    if (action == actions[6]) {
        _git->clearRemoteCredentials();
        showStatus(QStringLiteral("Session credentials cleared."));
        return;
    }
    if (action == actions[2]) {
        slotPruneRemote();
        return;
    }
    if (action == actions[3]) {
        slotPushTag();
        return;
    }
    if (action == actions[4]) {
        slotDeleteRemoteTag();
        return;
    }
    const QString name = QInputDialog::getText(this, QStringLiteral("Add Remote"), QStringLiteral("Remote name:"), QLineEdit::Normal, QStringLiteral("origin"));
    if (name.trimmed().isEmpty()) return;
    if (action == actions[1]) {
        if (QMessageBox::question(this, QStringLiteral("Remove Remote"), QStringLiteral("Remove remote '%1'?").arg(name)) == QMessageBox::Yes)
            _git->removeRemote(name.trimmed());
        return;
    }
    const QString url = QInputDialog::getText(this, QStringLiteral("Add Remote"), QStringLiteral("Remote URL:"));
    if (!url.trimmed().isEmpty()) _git->addRemote(name.trimmed(), url.trimmed());
}

void MainWindow::slotStash()
{
    const QStringList actions = {QStringLiteral("Create stash"), QStringLiteral("Manage stashes")};
    bool ok = false;
    const QString action = QInputDialog::getItem(this, QStringLiteral("Stash"), QStringLiteral("Action:"), actions, 0, false, &ok);
    if (!ok) return;
    if (action == actions[0]) {
        const QString message = QInputDialog::getText(this, QStringLiteral("Create Stash"), QStringLiteral("Message:"));
        _git->stashPush(message, true);
    } else _git->listStashes();
}

void MainWindow::slotWorktree()
{
    if (!_git->isValid())
        return;

    WorktreeDialog dialog(_state.worktrees, this);
    connect(&dialog, &WorktreeDialog::sigOpenRequested, this,
            [this, &dialog](const QString& path) {
        dialog.accept();
        slotRepoSelected(path);
    });
    connect(&dialog, &WorktreeDialog::sigCreateRequested, this,
            [this](const QString& name, const QString& path, const QString& branchName) {
        _git->addWorktree(name, path, branchName);
    });
    connect(&dialog, &WorktreeDialog::sigMoveRequested, this,
            [this](const QString& name, const QString& path) {
        _git->moveWorktree(name, path);
    });
    connect(&dialog, &WorktreeDialog::sigLockRequested, this,
            [this](const QString& name, bool locked, const QString& reason) {
        if (locked)
            _git->lockWorktree(name, reason);
        else
            _git->unlockWorktree(name);
    });
    connect(&dialog, &WorktreeDialog::sigRemoveRequested, this,
            [this](const QString& name, bool force) {
        _git->removeWorktree(name, force);
    });
    dialog.exec();
}

void MainWindow::slotSubmodule()
{
    if (!_git->isValid())
        return;

    SubmoduleDialog dialog(_state.submodules, this);
    connect(&dialog, &SubmoduleDialog::sigOpenRequested, this,
            [this, &dialog](const QString& path) {
        dialog.accept();
        slotRepoSelected(QDir(_state.rootPath).filePath(path));
    });
    connect(&dialog, &SubmoduleDialog::sigAddRequested, this,
            [this](const QString& url, const QString& path) {
        _git->addSubmodule(url, path);
    });
    connect(&dialog, &SubmoduleDialog::sigUpdateRequested, this,
            [this](const QString& name) { _git->updateSubmodule(name); });
    connect(&dialog, &SubmoduleDialog::sigSyncRequested, this,
            [this](const QString& name) { _git->syncSubmodule(name); });
    connect(&dialog, &SubmoduleDialog::sigBranchRequested, this,
            [this](const QString& name, const QString& branch) {
        _git->setSubmoduleBranch(name, branch);
    });
    connect(&dialog, &SubmoduleDialog::sigRemoveRequested, this,
            [this](const QString& name, bool force) {
        _git->removeSubmodule(name, force);
    });
    dialog.exec();
}

void MainWindow::slotLfs()
{
    if (_git->isValid())
        _git->requestLfsState();
}

void MainWindow::slotHosting()
{
    if (_git->isValid())
        _git->requestHostingRemotes();
}

void MainWindow::slotGitDiagnostics()
{
    if (_git->isValid())
        _git->requestGitDiagnostics();
}

void MainWindow::slotHooks()
{
    if (_git->isValid())
        _git->requestHooks();
}

void MainWindow::slotRepositoryOperation()
{
    QString operation;
    switch (_state.activeOperation) {
    case Git::RepositoryOperation::Merge:
        operation = QStringLiteral("merge");
        break;
    case Git::RepositoryOperation::Rebase:
        operation = QStringLiteral("rebase");
        break;
    case Git::RepositoryOperation::CherryPick:
        operation = QStringLiteral("cherry-pick");
        break;
    case Git::RepositoryOperation::Revert:
        operation = QStringLiteral("revert");
        break;
    case Git::RepositoryOperation::None:
        QMessageBox::information(this, QStringLiteral("Repository Operation"),
                                 QStringLiteral("No repository operation is in progress."));
        return;
    case Git::RepositoryOperation::Unknown:
        QMessageBox::warning(this, QStringLiteral("Repository Operation"),
                             QStringLiteral("The active repository operation is not supported."));
        return;
    }

    const QStringList actions = {
        QStringLiteral("Continue %1").arg(operation),
        QStringLiteral("Abort %1").arg(operation)
    };
    bool accepted = false;
    const QString action = QInputDialog::getItem(
        this, QStringLiteral("Repository Operation"), QStringLiteral("Action:"),
        actions, 0, false, &accepted);
    if (!accepted)
        return;
    if (action == actions.first()) {
        _git->continueOperation(operation);
        return;
    }

    if (QMessageBox::warning(
            this, QStringLiteral("Abort Repository Operation"),
            QStringLiteral("Abort the active %1 operation and restore its starting state? "
                           "Changes created while resolving the operation will be discarded; "
                           "untracked files are preserved unless they obstruct checkout.")
                .arg(operation),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel) == QMessageBox::Yes) {
        _git->abortOperation(operation);
    }
}

void MainWindow::slotCancelOperation() { _git->cancelAll(); }

// ============================================================
// 仓库切换
// ============================================================

void MainWindow::slotRepoSelected(const QString& path)
{
    openRepo(path);
}

// ============================================================
// Widget slots
// ============================================================

void MainWindow::slotCommitSelected(const Git::Commit& commit)
{
    showStatus(QStringLiteral("Loading diff for %1…").arg(commit.shortHash));
    _requestedDiffSource = DiffSource::Commit;
    _git->fetchCommitDiff(commit.hash);
}

void MainWindow::slotFileSelected(const QString& filePath, bool staged, bool untracked)
{
    if (_git->isBusy() || !_git->isValid())
        return;
    _requestedDiffSource = DiffSource::File;
    _currentDiffPath = filePath;
    _git->fetchFileDiff(filePath, staged, untracked);
}

void MainWindow::slotExternalTools()
{
    ExternalToolsDialog(this).exec();
}

void MainWindow::slotExternalDiff()
{
    if (_currentDiffPath.isEmpty() || !_git->isValid()) {
        QMessageBox::information(this, QStringLiteral("External Diff"),
                                 QStringLiteral("Select a file diff first."));
        return;
    }
    QString error;
    const QString file = QDir(_git->repositoryPath()).filePath(_currentDiffPath);
    if (!Git::ExternalToolService::launchDiff(_git->repositoryPath(), file, &error))
        QMessageBox::warning(this, QStringLiteral("External Diff"), error);
}

void MainWindow::slotStageFile(const QString& filePath)
{
    if (_git->isBusy() || !_git->isValid())
        return;
    _git->stageFile(filePath);
}

void MainWindow::slotUnstageFile(const QString& filePath)
{
    if (_git->isBusy() || !_git->isValid())
        return;
    _git->unstageFile(filePath);
}

void MainWindow::slotStageAll()
{
    if (_git->isBusy() || !_git->isValid())
        return;
    _git->stageAll();
}

void MainWindow::slotUnstageAll()
{
    if (_git->isBusy() || !_git->isValid())
        return;
    _git->unstageAll();
}

void MainWindow::slotIgnoreFile(const QString& filePath)
{
    if (_git->isBusy() || !_git->isValid())
        return;
    if (_git->addToGitIgnore(filePath)) {
        refresh();
        showStatus(QStringLiteral("Added to .gitignore: %1").arg(filePath));
    }
}

void MainWindow::slotCheckoutBranch(const QString& branchName)
{
    _git->checkoutBranch(branchName);
}

void MainWindow::slotDeleteBranch(const QString& branchName, bool force)
{
    _git->deleteBranch(branchName, force);
}

void MainWindow::slotCreateBranch(const QString& fromBranch)
{
    BranchDialog dialog(BranchDialog::Mode::Create, this);
    dialog.setSourceRevision(fromBranch);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (dialog.branchName().isEmpty())
        return;
    _git->createBranch(dialog.branchName(), dialog.sourceRevision());
}

void MainWindow::slotRenameBranch(const QString& branchName)
{
    BranchDialog dialog(BranchDialog::Mode::Rename, this);
    dialog.setBranchName(branchName);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QString name = dialog.branchName();
    if (!name.isEmpty() && name != branchName)
        _git->renameBranch(branchName, name);
}

void MainWindow::slotPublishBranch(const QString& branchName)
{
    BranchDialog dialog(BranchDialog::Mode::Publish, this);
    dialog.setBranchName(branchName);
    dialog.setRemoteName(QStringLiteral("origin"));
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (!dialog.remoteName().isEmpty() && !dialog.branchName().isEmpty())
        _git->pushSetUpstream(dialog.remoteName(), dialog.branchName());
}

void MainWindow::slotUntrackBranch(const QString& branchName)
{
    if (QMessageBox::question(
            this, QStringLiteral("Untrack Branch"),
            QStringLiteral("Stop '%1' from tracking its upstream branch?\n\n"
                           "The remote branch will not be deleted.").arg(branchName),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel) == QMessageBox::Yes) {
        _git->unsetBranchUpstream(branchName);
    }
}

void MainWindow::slotMergeRevision(const QString& revision)
{
    if (!_git->isValid() || revision.isEmpty())
        return;
    const QString target = revision.left(12);
    if (QMessageBox::question(
            this, QStringLiteral("Merge into Current Branch"),
            QStringLiteral("Merge '%1' into the current branch?\n\n"
                           "If conflicts occur, the repository will remain in merge "
                           "state so they can be resolved or the merge can be aborted.")
                .arg(target),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }
    _git->mergeRevision(revision);
}

void MainWindow::slotRebaseRevision(const QString& revision)
{
    if (!_git->isValid() || revision.isEmpty())
        return;
    showStatus(QStringLiteral("Preparing rebase preview..."));
    _git->requestRebasePlan(revision);
}

void MainWindow::slotCherryPickCommit(const QString& hash, int mainline)
{
    if (!_git->isValid() || hash.isEmpty())
        return;
    QString text = QStringLiteral("Apply commit %1 on top of the current branch?")
                       .arg(hash.left(12));
    if (mainline > 0)
        text += QStringLiteral("\n\nParent %1 will be used as the mainline.").arg(mainline);
    if (QMessageBox::question(this, QStringLiteral("Cherry-pick Commit"), text,
                              QMessageBox::Yes | QMessageBox::Cancel,
                              QMessageBox::Cancel) == QMessageBox::Yes) {
        _git->cherryPickCommit(hash, mainline);
    }
}

void MainWindow::slotRevertCommit(const QString& hash, int mainline)
{
    if (!_git->isValid() || hash.isEmpty())
        return;
    QString text = QStringLiteral(
        "Create a new commit that reverses %1? Existing history will not be rewritten.")
                       .arg(hash.left(12));
    if (mainline > 0)
        text += QStringLiteral("\n\nParent %1 will be used as the mainline.").arg(mainline);
    if (QMessageBox::question(this, QStringLiteral("Revert Commit"), text,
                              QMessageBox::Yes | QMessageBox::Cancel,
                              QMessageBox::Cancel) == QMessageBox::Yes) {
        _git->revertCommit(hash, mainline);
    }
}

void MainWindow::slotResetCommit(const QString& hash)
{
    if (!_git->isValid() || hash.isEmpty())
        return;
    showStatus(QStringLiteral("Preparing reset preview..."));
    _git->requestResetPreview(hash);
}

void MainWindow::slotCompareRevisions(const QString& baseRevision,
                                      const QString& targetRevision)
{
    if (!_git->isValid())
        return;

    const QString base = baseRevision.trimmed();
    const QString target = targetRevision.trimmed();
    if (base.isEmpty() || target.isEmpty() || base == target)
        return;

    showStatus(QStringLiteral("Comparing %1..%2…").arg(base, target));
    _compareWidget->setBaseRevision(base);
    _compareWidget->setTargetRevision(target);
    _requestedDiffSource = DiffSource::Commit;
    _git->fetchRevisionDiff(base, target);
}

void MainWindow::slotCreateTag(const QString& commitHash)
{
    if (!_git->isValid()) return;

    const QString name = QInputDialog::getText(
        this,
        QStringLiteral("New Tag"),
        QStringLiteral("Tag name:"));

    if (name.trimmed().isEmpty())
        return;

    const QString msg = QInputDialog::getText(
        this,
        QStringLiteral("Tag Message"),
        QStringLiteral("Annotation message (leave empty for lightweight tag):"));

    _git->createTag(name.trimmed(), commitHash, msg.trimmed());
}

void MainWindow::slotDeleteTag(const QString& tagName)
{
    if (!_git->isValid()) return;

    const auto btn = QMessageBox::question(
        this,
        QStringLiteral("Delete Tag"),
        QStringLiteral("Delete tag '%1'?").arg(tagName));

    if (btn != QMessageBox::Yes)
        return;

    _git->deleteTag(tagName);
}

void MainWindow::slotPushTag()
{
    if (!_git->isValid())
        return;
    bool accepted = false;
    const QString remote = QInputDialog::getText(
        this, QStringLiteral("Push Tag"), QStringLiteral("Remote name:"),
        QLineEdit::Normal, QStringLiteral("origin"), &accepted).trimmed();
    if (!accepted || remote.isEmpty())
        return;
    const QString tag = QInputDialog::getText(
        this, QStringLiteral("Push Tag"), QStringLiteral("Tag name:"),
        QLineEdit::Normal, {}, &accepted).trimmed();
    if (!accepted || tag.isEmpty())
        return;
    _git->pushTag(remote, tag);
}

void MainWindow::slotDeleteRemoteTag()
{
    if (!_git->isValid())
        return;
    bool accepted = false;
    const QString remote = QInputDialog::getText(
        this, QStringLiteral("Delete Remote Tag"), QStringLiteral("Remote name:"),
        QLineEdit::Normal, QStringLiteral("origin"), &accepted).trimmed();
    if (!accepted || remote.isEmpty())
        return;
    const QString tag = QInputDialog::getText(
        this, QStringLiteral("Delete Remote Tag"), QStringLiteral("Tag name:"),
        QLineEdit::Normal, {}, &accepted).trimmed();
    if (!accepted || tag.isEmpty())
        return;
    if (QMessageBox::question(
            this, QStringLiteral("Delete Remote Tag"),
            QStringLiteral("Delete tag '%1' from remote '%2'?").arg(tag, remote),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }
    _git->deleteRemoteTag(remote, tag);
}

void MainWindow::slotPruneRemote()
{
    if (!_git->isValid())
        return;
    bool accepted = false;
    const QString remote = QInputDialog::getText(
        this, QStringLiteral("Prune Remote"), QStringLiteral("Remote name:"),
        QLineEdit::Normal, QStringLiteral("origin"), &accepted).trimmed();
    if (!accepted || remote.isEmpty())
        return;
    _git->pruneRemote(remote);
}

// ============================================================
// Helpers
// ============================================================

void MainWindow::openRepo(const QString& path)
{
    showStatus(QStringLiteral("Opening repository…"));
    _git->openRepository(path);
    beginRepositoryTransition(path);
}

void MainWindow::beginRepositoryTransition(const QString& path)
{
    if (_centralStack)
        _centralStack->setCurrentIndex(1);
    _state = {};
    _commitGraph->clear();
    _commitGraph->setBranches({});
    _compareWidget->setBranches({});
    _compareWidget->setBaseRevision({});
    _compareWidget->setTargetRevision({});
    _commitGraph->setHistoryLoading(true);
    _branchList->setBranches({});
    _branchList->setEnabled(false);
    if (_operationAction)
        _operationAction->setEnabled(false);
    _statusWidget->setFiles({});
    _statusWidget->setEnabled(false);
    _compareWidget->setEnabled(false);
    _diffWidget->clearDiff();
    _diffWidget->setEnabled(false);
    _requestedDiffSource = DiffSource::None;
    _displayedDiffSource = DiffSource::None;
    _watcher->setRepositoryPath({});
    _rightTabs->setCurrentWidget(_statusWidget);
    _repoList->setCurrentRepo({});
    _repoLabel->setText(QStringLiteral("  Opening: %1").arg(path));
    setWindowTitle(QStringLiteral("Git Manager — Opening"));
}

void MainWindow::refresh()
{
    showStatus(QStringLiteral("Refreshing…"));
    _git->refresh();
}

void MainWindow::slotDiscardFile(const QString& filePath, bool untracked)
{
    if (_git->isBusy() || !_git->isValid())
        return;
    const QString text = untracked ? QStringLiteral("Delete untracked file '%1' permanently?").arg(filePath)
                                   : QStringLiteral("Discard all unstaged changes in '%1'?").arg(filePath);
    if (QMessageBox::warning(this, QStringLiteral("Confirm Destructive Operation"), text,
                             QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    if (_git->isBusy() || !_git->isValid())
        return;
    if (untracked) _git->removeUntracked(filePath); else _git->discardFile(filePath);
}

void MainWindow::slotResolveFile(const QString& filePath, bool ours)
{
    if (_git->isBusy() || !_git->isValid())
        return;
    if (QMessageBox::question(this, QStringLiteral("Resolve Conflict"),
        QStringLiteral("Replace '%1' with the %2 version?").arg(filePath, ours ? QStringLiteral("current") : QStringLiteral("incoming"))) == QMessageBox::Yes
        && !_git->isBusy() && _git->isValid())
        _git->resolveConflict(filePath, ours);
}

void MainWindow::slotRepositoryOpened(const QString& path, bool success, const QString& error)
{
    if (!success) {
        _commitGraph->setHistoryLoading(false);
        _watcher->setRepositoryPath({});
        _repoLabel->setText(QStringLiteral("  No repository"));
        setWindowTitle(QStringLiteral("Git Manager"));
        if (_centralStack)
            _centralStack->setCurrentIndex(0);
        if (_notification)
            _notification->showMessage(error, NotificationWidget::Level::Error, 0);
        return;
    }
    if (_centralStack)
        _centralStack->setCurrentIndex(1);
    RepoListWidget::recordRepo(path);
    _repoList->setCurrentRepo(path);
    QSettings settings;
    settings.setValue(QStringLiteral("lastRepo"), path);
    settings.sync();
    _watcher->setRepositoryPath(path);
    _repoLabel->setText(QStringLiteral("  ") + path);
    setWindowTitle(QStringLiteral("Git Manager — %1").arg(path));
}

void MainWindow::slotStateReady(const Git::RepositoryState& state)
{
    _state = state;
    _branchList->setEnabled(true);
    _statusWidget->setEnabled(!_git->isBusy());
    _compareWidget->setEnabled(!_git->isBusy());
    _diffWidget->setEnabled(!_git->isBusy());
    _commitGraph->setBranches(state.branches);
    _compareWidget->setBranches(state.branches);
    if (_compareWidget->targetRevision().isEmpty())
        _compareWidget->setTargetRevision(state.headName);
    _branchList->setBranches(state.branches);
    _statusWidget->setFiles(state.files);
    _diffWidget->clearDiff();
    _git->cancelDiffRequest();
    _requestedDiffSource = DiffSource::None;
    _displayedDiffSource = DiffSource::None;
    const QString operation = state.activeOperationText();
    const QString operationSuffix = operation.isEmpty()
        ? QString() : QStringLiteral(" — %1 in progress").arg(operation);
    showStatus(QStringLiteral("Ready — %1 [%2] %3%4")
        .arg(state.rootPath, state.displayHead(), state.syncText(), operationSuffix));
    if (_operationAction)
        _operationAction->setEnabled(!_git->isBusy()
                                     && supportsContinueAbort(
                                         state.activeOperation));
    const bool historyOperationsEnabled = !_git->isBusy()
        && state.activeOperation == Git::RepositoryOperation::None;
    _commitGraph->setOperationsEnabled(historyOperationsEnabled);
    _branchList->setOperationsEnabled(historyOperationsEnabled);
}

void MainWindow::slotOperationFinished(const QString& operation, bool success, const QString& message)
{
    showStatus(success ? QStringLiteral("%1 completed. %2").arg(operation, message)
                       : QStringLiteral("%1 failed.").arg(operation));
}

void MainWindow::slotResetPreviewReady(const Git::HistoryRewritePreview& preview)
{
    ResetDialog dialog(preview, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    _git->resetToCommit(preview, dialog.resetMode());
}

void MainWindow::slotRebasePlanReady(const Git::RebasePlan& plan)
{
    RebaseDialog dialog(plan, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    _git->rebaseOnto(dialog.rebasePlan(), dialog.interactiveMode());
}

void MainWindow::slotHistoryOperationFinished(
    const QString& operation, Git::HistoryOperationStatus status,
    const QString& message)
{
    const QString detail = message.isEmpty()
        ? QStringLiteral("History operation finished.") : message;
    switch (status) {
    case Git::HistoryOperationStatus::Completed:
    case Git::HistoryOperationStatus::UpToDate:
        showStatus(QStringLiteral("%1: %2").arg(operation, detail));
        break;
    case Git::HistoryOperationStatus::Conflicts:
        _rightTabs->setCurrentWidget(_statusWidget);
        showStatus(detail);
        QMessageBox::warning(this, QStringLiteral("Conflicts Require Attention"), detail);
        break;
    case Git::HistoryOperationStatus::PausedForEdit:
        _rightTabs->setCurrentWidget(_statusWidget);
        showStatus(detail);
        QMessageBox::information(this, QStringLiteral("Interactive Rebase Paused"), detail);
        break;
    }
}

void MainWindow::slotBusyChanged(bool busy)
{
    _cancelAction->setEnabled(busy);
    for (QAction* action : _repositoryActions)
        action->setEnabled(!busy && _git->isValid());
    const bool repositoryReady = !busy && _git->isValid();
    const bool operationsEnabled = repositoryReady
        && _state.activeOperation == Git::RepositoryOperation::None;
    for (QAction* action : _inactiveDuringOperationActions)
        action->setEnabled(operationsEnabled);
    _statusWidget->setEnabled(repositoryReady);
    _compareWidget->setEnabled(repositoryReady);
    _diffWidget->setEnabled(repositoryReady);
    _commitGraph->setOperationsEnabled(operationsEnabled);
    _branchList->setOperationsEnabled(operationsEnabled);
    if (_operationAction) {
        _operationAction->setEnabled(
            repositoryReady
            && supportsContinueAbort(_state.activeOperation));
    }
}

void MainWindow::showStatus(const QString& msg)
{
    statusBar()->showMessage(msg);
}

void MainWindow::slotGitError(const QString& message)
{
    statusBar()->showMessage(QStringLiteral("Error: %1").arg(message));
    if (_notification)
        _notification->showMessage(message, NotificationWidget::Level::Error);
}
