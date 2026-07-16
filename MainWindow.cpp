#include "MainWindow.h"
#include "widgets/CommitGraphWidget.h"
#include "widgets/BranchListWidget.h"
#include "widgets/RepoListWidget.h"
#include "widgets/StatusWidget.h"
#include "widgets/DiffWidget.h"
#include "widgets/TerminalWidget.h"
#include "dialogs/CommitDialog.h"

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
    auto* refreshAction = tb->addAction(QStringLiteral("Refresh"));
    tb->addSeparator();
    auto* pullAction    = tb->addAction(QStringLiteral("Pull"));
    auto* pushAction    = tb->addAction(QStringLiteral("Push"));
    tb->addSeparator();
    auto* commitAction  = tb->addAction(QStringLiteral("Commit"));
    _cancelAction       = tb->addAction(QStringLiteral("Cancel"));
    _cancelAction->setEnabled(false);
    _repositoryActions = {refreshAction, pullAction, pushAction, commitAction};

    _repoLabel = new QLabel(QStringLiteral("  No repository"), this);
    statusBar()->addPermanentWidget(_repoLabel);

    connect(openAction,    &QAction::triggered, this, &MainWindow::slotOpenRepo);
    connect(refreshAction, &QAction::triggered, this, &MainWindow::slotRefresh);
    connect(pullAction,    &QAction::triggered, this, &MainWindow::slotPull);
    connect(pushAction,    &QAction::triggered, this, &MainWindow::slotPush);
    connect(commitAction,  &QAction::triggered, this, &MainWindow::slotCommit);
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
    connect(_git, &Git::GitManager::sigDiffReady, this,
            [this](const QString& diff, const QString&) {
        _diffWidget->setDiff(diff);
        _rightTabs->setCurrentWidget(_diffWidget);
    });
    connect(_git, &Git::GitManager::sigOperationFinished,
            this, &MainWindow::slotOperationFinished);
    connect(_git, &Git::GitManager::sigBusyChanged,
            this, &MainWindow::slotBusyChanged);

    connect(_repoList, &RepoListWidget::sigRepoSwitchRequested,
            this, &MainWindow::slotRepoSelected);

    connect(_commitGraph, &CommitGraphWidget::sigCommitSelected,
            this, &MainWindow::slotCommitSelected);

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

    connect(_branchList, &BranchListWidget::sigCheckoutRequested,
            this, &MainWindow::slotCheckoutBranch);
    connect(_branchList, &BranchListWidget::sigDeleteRequested,
            this, &MainWindow::slotDeleteBranch);
    connect(_branchList, &BranchListWidget::sigCreateFromRequested,
            this, &MainWindow::slotCreateBranch);

    connect(_commitGraph, &CommitGraphWidget::sigCreateTagRequested,
            this, &MainWindow::slotCreateTag);
    connect(_commitGraph, &CommitGraphWidget::sigDeleteTagRequested,
            this, &MainWindow::slotDeleteTag);
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
    _git->push();
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

    _git->commit(msg);
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
    _git->fetchCommitDiff(commit.hash);
}

void MainWindow::slotFileSelected(const QString& filePath, bool staged, bool untracked)
{
    _git->fetchFileDiff(filePath, staged, untracked);
}

void MainWindow::slotStageFile(const QString& filePath)
{
    _git->stageFile(filePath);
}

void MainWindow::slotUnstageFile(const QString& filePath)
{
    _git->unstageFile(filePath);
}

void MainWindow::slotStageAll()
{
    _git->stageAll();
}

void MainWindow::slotUnstageAll()
{
    _git->unstageAll();
}

void MainWindow::slotIgnoreFile(const QString& filePath)
{
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
}

void MainWindow::refresh()
{
    showStatus(QStringLiteral("Refreshing…"));
    _git->refresh();
}

void MainWindow::slotRepositoryOpened(const QString& path, bool success, const QString& error)
{
    if (!success) {
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
    _commitGraph->setCommits(state.commits);
    _branchList->setBranches(state.branches);
    _statusWidget->setFiles(state.files);
    _diffWidget->clearDiff();
    showStatus(QStringLiteral("Ready — %1 [%2] %3")
        .arg(state.rootPath, state.displayHead(), state.syncText()));
}

void MainWindow::slotOperationFinished(const QString& operation, bool success, const QString& message)
{
    showStatus(success ? QStringLiteral("%1 completed. %2").arg(operation, message)
                       : QStringLiteral("%1 failed.").arg(operation));
}

void MainWindow::slotBusyChanged(bool busy)
{
    _cancelAction->setEnabled(busy);
    for (QAction* action : _repositoryActions)
        action->setEnabled(!busy && _git->isValid());
}

void MainWindow::showStatus(const QString& msg)
{
    statusBar()->showMessage(msg);
}

void MainWindow::slotGitError(const QString& message)
{
    statusBar()->showMessage(QStringLiteral("Error: %1").arg(message));
}
