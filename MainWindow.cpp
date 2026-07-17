#include "MainWindow.h"
#include "widgets/CommitGraphWidget.h"
#include "widgets/BranchListWidget.h"
#include "widgets/RepoListWidget.h"
#include "widgets/StatusWidget.h"
#include "widgets/DiffWidget.h"
#include "widgets/TerminalWidget.h"
#include "dialogs/CommitDialog.h"
#include "dialogs/RebaseDialog.h"
#include "dialogs/ResetDialog.h"

#include <QToolBar>
#include <QAction>
#include <QSplitter>
#include <QTabWidget>
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

    setupToolBar();
    setupCentralWidget();
    connectSignals();

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
    auto* initAction    = tb->addAction(QStringLiteral("Init"));
    auto* cloneAction   = tb->addAction(QStringLiteral("Clone"));
    auto* refreshAction = tb->addAction(QStringLiteral("Refresh"));
    tb->addSeparator();
    auto* pullAction    = tb->addAction(QStringLiteral("Pull"));
    auto* fetchAction   = tb->addAction(QStringLiteral("Fetch"));
    auto* pushAction    = tb->addAction(QStringLiteral("Push"));
    tb->addSeparator();
    auto* commitAction  = tb->addAction(QStringLiteral("Commit"));
    auto* stashAction   = tb->addAction(QStringLiteral("Stash"));
    auto* remoteAction  = tb->addAction(QStringLiteral("Remote"));
    _operationAction      = tb->addAction(QStringLiteral("Continue/Abort"));
    _cancelAction       = tb->addAction(QStringLiteral("Cancel"));
    _cancelAction->setEnabled(false);
    _operationAction->setEnabled(false);
    _repositoryActions = {refreshAction, pullAction, fetchAction, pushAction,
                          commitAction, stashAction, remoteAction, _operationAction};
    _inactiveDuringOperationActions = {pullAction, pushAction,
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
    connect(commitAction,  &QAction::triggered, this, &MainWindow::slotCommit);
    connect(stashAction, &QAction::triggered, this, &MainWindow::slotStash);
    connect(remoteAction, &QAction::triggered, this, &MainWindow::slotRemote);
    connect(_operationAction, &QAction::triggered,
            this, &MainWindow::slotRepositoryOperation);
    connect(_cancelAction, &QAction::triggered, this, &MainWindow::slotCancelOperation);
}

void MainWindow::setupCentralWidget()
{
    _repoList     = new RepoListWidget(this);
    _commitGraph  = new CommitGraphWidget(this);
    _branchList   = new BranchListWidget(this);
    _statusWidget = new StatusWidget(this);
    _diffWidget   = new DiffWidget(this);
    _terminalWidget = new TerminalWidget(this);

    // 右侧 Tab：Status | Diff
    _rightTabs = new QTabWidget(this);
    _rightTabs->addTab(_statusWidget, QStringLiteral("Status"));
    _rightTabs->addTab(_diffWidget,   QStringLiteral("Diff"));

    // 内部 splitter：中（提交图）| 右（status/diff）
    auto* innerSplitter = new QSplitter(Qt::Horizontal, this);
    innerSplitter->addWidget(_commitGraph);
    innerSplitter->addWidget(_rightTabs);
    innerSplitter->setStretchFactor(0, 3);
    innerSplitter->setStretchFactor(1, 1);

    // 左列 splitter：上（仓库列表）| 下（分支列表）
    auto* leftSplitter = new QSplitter(Qt::Vertical, this);
    leftSplitter->addWidget(_repoList);
    leftSplitter->addWidget(_branchList);
    leftSplitter->setStretchFactor(0, 1);
    leftSplitter->setStretchFactor(1, 1);
    leftSplitter->setSizes({220, 260});

    // 外部横向 splitter：左列 | 内容区
    auto* outerSplitter = new QSplitter(Qt::Horizontal, this);
    outerSplitter->addWidget(leftSplitter);
    outerSplitter->addWidget(innerSplitter);
    outerSplitter->setStretchFactor(0, 0);
    outerSplitter->setStretchFactor(1, 1);
    outerSplitter->setSizes({200, 1080});

    // 主竖向 splitter：上（内容区）| 下（终端）
    auto* mainSplitter = new QSplitter(Qt::Vertical, this);
    mainSplitter->addWidget(outerSplitter);
    mainSplitter->addWidget(_terminalWidget);
    mainSplitter->setStretchFactor(0, 5);
    mainSplitter->setStretchFactor(1, 1);
    mainSplitter->setSizes({560, 160});

    setCentralWidget(mainSplitter);
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
        _rightTabs->setCurrentWidget(_diffWidget);
    });
    connect(_git, &Git::GitManager::sigOperationFinished,
            this, &MainWindow::slotOperationFinished);
    connect(_git, &Git::GitManager::sigBusyChanged,
            this, &MainWindow::slotBusyChanged);
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
    connect(_branchList, &BranchListWidget::sigMergeRequested,
            this, &MainWindow::slotMergeRevision);
    connect(_branchList, &BranchListWidget::sigRebaseRequested,
            this, &MainWindow::slotRebaseRevision);

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
        QStringLiteral("Set session credentials"),
        QStringLiteral("Clear session credentials")
    };
    bool selected = false;
    const QString action = QInputDialog::getItem(this, QStringLiteral("Remote"), QStringLiteral("Action:"), actions, 0, false, &selected);
    if (!selected) return;
    if (action == actions[2]) {
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
    if (action == actions[3]) {
        _git->clearRemoteCredentials();
        showStatus(QStringLiteral("Session credentials cleared."));
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
    _git->fetchFileDiff(filePath, staged, untracked);
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
    const QString name = QInputDialog::getText(
        this,
        QStringLiteral("New Branch"),
        QStringLiteral("Branch name:"));

    if (name.trimmed().isEmpty())
        return;

    _git->createBranch(name.trimmed(), fromBranch);
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
    _state = {};
    _commitGraph->clear();
    _commitGraph->setBranches({});
    _commitGraph->setHistoryLoading(true);
    _branchList->setBranches({});
    _branchList->setEnabled(false);
    if (_operationAction)
        _operationAction->setEnabled(false);
    _statusWidget->setFiles({});
    _statusWidget->setEnabled(false);
    _diffWidget->clearDiff();
    _diffWidget->setEnabled(false);
    _requestedDiffSource = DiffSource::None;
    _displayedDiffSource = DiffSource::None;
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
        _repoLabel->setText(QStringLiteral("  No repository"));
        setWindowTitle(QStringLiteral("Git Manager"));
        QMessageBox::warning(this, QStringLiteral("Not a Git Repository"), error);
        return;
    }
    RepoListWidget::recordRepo(path);
    _repoList->setCurrentRepo(path);
    QSettings settings;
    settings.setValue(QStringLiteral("lastRepo"), path);
    settings.sync();
    _repoLabel->setText(QStringLiteral("  ") + path);
    setWindowTitle(QStringLiteral("Git Manager — %1").arg(path));
}

void MainWindow::slotStateReady(const Git::RepositoryState& state)
{
    _state = state;
    _branchList->setEnabled(true);
    _statusWidget->setEnabled(!_git->isBusy());
    _diffWidget->setEnabled(!_git->isBusy());
    _commitGraph->setBranches(state.branches);
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
}
