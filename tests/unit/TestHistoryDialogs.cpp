#include "dialogs/RebaseDialog.h"
#include "dialogs/ResetDialog.h"
#include "dialogs/BranchDialog.h"
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
#include "dialogs/SettingsDialog.h"
#include "core/ExternalToolService.h"
#include "widgets/NotificationWidget.h"
#include "widgets/WelcomeWidget.h"
#include "widgets/InteractiveRebaseWidget.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QTableWidget>
#include <QTreeWidget>
#include <QtTest>

#include <array>

class TestHistoryDialogs : public QObject {
    Q_OBJECT

    static Git::HistoryRewritePreview preview()
    {
        Git::HistoryRewritePreview value;
        value.revision = QStringLiteral("main~2");
        value.targetHash = QString(40, QLatin1Char('a'));
        value.expectedHead = QString(40, QLatin1Char('f'));
        value.currentBranch = QStringLiteral("main");
        value.upstream = QStringLiteral("origin/main");
        value.affectedCount = 2;
        value.activeOperation = Git::RepositoryOperation::None;
        return value;
    }

    static Git::RebasePlan plan(int itemCount = 2)
    {
        Git::RebasePlan value;
        value.preview = preview();
        for (int row = 0; row < itemCount; ++row) {
            Git::RebasePlanItem item;
            item.hash = QString(40, QChar::fromLatin1(
                static_cast<char>('b' + row)));
            item.subject = QStringLiteral("commit %1").arg(row + 1);
            item.message = item.subject + QStringLiteral("\n\nbody");
            item.action = Git::RebaseAction::Pick;
            item.parentCount = 1;
            value.items.append(item);
        }
        value.preview.affectedCount = itemCount;
        return value;
    }

    static QComboBox* actionCombo(InteractiveRebaseWidget& widget, int row)
    {
        return widget.findChild<QComboBox*>(
            QStringLiteral("rebaseActionCombo_%1").arg(row));
    }

    static void setAction(InteractiveRebaseWidget& widget, int row,
                          Git::RebaseAction action)
    {
        QComboBox* combo = actionCombo(widget, row);
        QVERIFY(combo);
        const int index = combo->findData(static_cast<int>(action));
        QVERIFY(index >= 0);
        combo->setCurrentIndex(index);
    }

private slots:
    void resetDefaultsToMixed()
    {
        ResetDialog dialog(preview());
        QCOMPARE(dialog.resetMode(), Git::ResetMode::Mixed);

        auto* resetButton = dialog.findChild<QPushButton*>(
            QStringLiteral("resetButton"));
        auto* mixedRadio = dialog.findChild<QRadioButton*>(
            QStringLiteral("resetMixedRadio"));
        QVERIFY(resetButton);
        QVERIFY(mixedRadio);
        QVERIFY(mixedRadio->isChecked());
        QVERIFY(resetButton->isEnabled());
    }

    void branchDialogValidatesRequiredFields()
    {
        BranchDialog createDialog(BranchDialog::Mode::Create);
        auto* createButton = createDialog.findChild<QPushButton*>(
            QStringLiteral("branchDialogAcceptButton"));
        auto* createBranch = createDialog.findChild<QLineEdit*>(
            QStringLiteral("branchDialogBranchEdit"));
        auto* createSource = createDialog.findChild<QLineEdit*>(
            QStringLiteral("branchDialogSourceEdit"));
        QVERIFY(createButton);
        QVERIFY(createBranch);
        QVERIFY(createSource);
        QVERIFY(!createButton->isEnabled());
        createBranch->setText(QStringLiteral("feature/new-ui"));
        QVERIFY(createButton->isEnabled());
        createSource->setText(QStringLiteral("main"));
        QCOMPARE(createDialog.branchName(), QStringLiteral("feature/new-ui"));
        QCOMPARE(createDialog.sourceRevision(), QStringLiteral("main"));

        BranchDialog renameDialog(BranchDialog::Mode::Rename);
        auto* renameButton = renameDialog.findChild<QPushButton*>(
            QStringLiteral("branchDialogAcceptButton"));
        auto* renameBranch = renameDialog.findChild<QLineEdit*>(
            QStringLiteral("branchDialogBranchEdit"));
        QVERIFY(renameButton);
        QVERIFY(renameBranch);
        QVERIFY(!renameButton->isEnabled());
        renameBranch->setText(QStringLiteral("release/2026"));
        QVERIFY(renameButton->isEnabled());

        BranchDialog publishDialog(BranchDialog::Mode::Publish);
        auto* publishButton = publishDialog.findChild<QPushButton*>(
            QStringLiteral("branchDialogAcceptButton"));
        auto* publishBranch = publishDialog.findChild<QLineEdit*>(
            QStringLiteral("branchDialogBranchEdit"));
        auto* publishRemote = publishDialog.findChild<QLineEdit*>(
            QStringLiteral("branchDialogRemoteEdit"));
        QVERIFY(publishButton);
        QVERIFY(publishBranch);
        QVERIFY(publishRemote);
        QVERIFY(!publishButton->isEnabled());
        publishBranch->setText(QStringLiteral("feature/login"));
        QVERIFY(!publishButton->isEnabled());
        publishRemote->setText(QStringLiteral("origin"));
        QVERIFY(publishButton->isEnabled());
        QCOMPARE(publishDialog.remoteName(), QStringLiteral("origin"));
    }

    void worktreeDialogEmitsCreateAndOpen()
    {
        QVector<Git::WorktreeInfo> worktrees;
        Git::WorktreeInfo current;
        current.name = QStringLiteral("main");
        current.path = QStringLiteral("D:/repo");
        current.current = true;
        worktrees.append(current);
        Git::WorktreeInfo linked;
        linked.name = QStringLiteral("feature");
        linked.path = QStringLiteral("D:/repo-feature");
        worktrees.append(linked);

        WorktreeDialog dialog(worktrees);
        auto* list = dialog.findChild<QListWidget*>(QStringLiteral("worktreeList"));
        auto* nameEdit = dialog.findChild<QLineEdit*>(QStringLiteral("worktreeNameEdit"));
        auto* pathEdit = dialog.findChild<QLineEdit*>(QStringLiteral("worktreePathEdit"));
        auto* branchEdit = dialog.findChild<QLineEdit*>(QStringLiteral("worktreeBranchEdit"));
        auto* movePathEdit = dialog.findChild<QLineEdit*>(QStringLiteral("worktreeMovePathEdit"));
        auto* lockReasonEdit = dialog.findChild<QLineEdit*>(QStringLiteral("worktreeLockReasonEdit"));
        auto* createButton = dialog.findChild<QPushButton*>(QStringLiteral("worktreeCreateButton"));
        auto* openButton = dialog.findChild<QPushButton*>(QStringLiteral("worktreeOpenButton"));
        auto* moveButton = dialog.findChild<QPushButton*>(QStringLiteral("worktreeMoveButton"));
        auto* lockButton = dialog.findChild<QPushButton*>(QStringLiteral("worktreeLockButton"));
        auto* removeButton = dialog.findChild<QPushButton*>(QStringLiteral("worktreeRemoveButton"));
        QVERIFY(list);
        QVERIFY(nameEdit);
        QVERIFY(pathEdit);
        QVERIFY(branchEdit);
        QVERIFY(movePathEdit);
        QVERIFY(lockReasonEdit);
        QVERIFY(createButton);
        QVERIFY(openButton);
        QVERIFY(moveButton);
        QVERIFY(lockButton);
        QVERIFY(removeButton);

        QSignalSpy createSpy(&dialog, &WorktreeDialog::sigCreateRequested);
        QSignalSpy openSpy(&dialog, &WorktreeDialog::sigOpenRequested);
        QSignalSpy moveSpy(&dialog, &WorktreeDialog::sigMoveRequested);
        QSignalSpy lockSpy(&dialog, &WorktreeDialog::sigLockRequested);

        QVERIFY(!createButton->isEnabled());
        nameEdit->setText(QStringLiteral("new-tree"));
        pathEdit->setText(QStringLiteral("D:/repo-new"));
        branchEdit->setText(QStringLiteral("feature/new"));
        QVERIFY(createButton->isEnabled());
        createButton->click();
        QCOMPARE(createSpy.count(), 1);
        QCOMPARE(createSpy.first().at(0).toString(), QStringLiteral("new-tree"));
        QCOMPARE(createSpy.first().at(1).toString(), QStringLiteral("D:/repo-new"));
        QCOMPARE(createSpy.first().at(2).toString(), QStringLiteral("feature/new"));

        list->setCurrentRow(1);
        QVERIFY(openButton->isEnabled());
        QVERIFY(!moveButton->isEnabled());
        movePathEdit->setText(QStringLiteral("D:/repo-feature-moved"));
        QVERIFY(moveButton->isEnabled());
        moveButton->click();
        QCOMPARE(moveSpy.count(), 1);
        QCOMPARE(moveSpy.first().at(0).toString(), QStringLiteral("feature"));
        QCOMPARE(moveSpy.first().at(1).toString(), QStringLiteral("D:/repo-feature-moved"));
        QVERIFY(lockButton->isEnabled());
        QVERIFY(removeButton->isEnabled());
        QCOMPARE(lockButton->text(), QStringLiteral("Lock"));
        lockReasonEdit->setText(QStringLiteral("portable drive"));
        lockButton->click();
        QCOMPARE(lockSpy.count(), 1);
        QCOMPARE(lockSpy.first().at(0).toString(), QStringLiteral("feature"));
        QCOMPARE(lockSpy.first().at(1).toBool(), true);
        QCOMPARE(lockSpy.first().at(2).toString(), QStringLiteral("portable drive"));
        openButton->click();
        QCOMPARE(openSpy.count(), 1);
        QCOMPARE(openSpy.first().at(0).toString(), QStringLiteral("D:/repo-feature"));

        list->setCurrentRow(0);
        QVERIFY(!removeButton->isEnabled());
    }

    void submoduleDialogEmitsManagementActions()
    {
        Git::SubmoduleInfo submodule;
        submodule.name = QStringLiteral("deps/library");
        submodule.path = QStringLiteral("deps/library");
        submodule.url = QStringLiteral("https://example.test/library.git");
        submodule.initialized = true;
        QVector<Git::SubmoduleInfo> submodules {submodule};

        SubmoduleDialog dialog(submodules);
        auto* list = dialog.findChild<QListWidget*>(QStringLiteral("submoduleList"));
        auto* urlEdit = dialog.findChild<QLineEdit*>(QStringLiteral("submoduleUrlEdit"));
        auto* pathEdit = dialog.findChild<QLineEdit*>(QStringLiteral("submodulePathEdit"));
        auto* openButton = dialog.findChild<QPushButton*>(QStringLiteral("submoduleOpenButton"));
        auto* updateButton = dialog.findChild<QPushButton*>(QStringLiteral("submoduleUpdateButton"));
        auto* syncButton = dialog.findChild<QPushButton*>(QStringLiteral("submoduleSyncButton"));
        auto* addButton = dialog.findChild<QPushButton*>(QStringLiteral("submoduleAddButton"));
        auto* branchEdit = dialog.findChild<QLineEdit*>(QStringLiteral("submoduleBranchEdit"));
        auto* branchButton = dialog.findChild<QPushButton*>(QStringLiteral("submoduleBranchButton"));
        auto* removeButton = dialog.findChild<QPushButton*>(QStringLiteral("submoduleRemoveButton"));
        QVERIFY(list);
        QVERIFY(urlEdit);
        QVERIFY(pathEdit);
        QVERIFY(openButton);
        QVERIFY(updateButton);
        QVERIFY(syncButton);
        QVERIFY(addButton);
        QVERIFY(branchEdit);
        QVERIFY(branchButton);
        QVERIFY(removeButton);

        QSignalSpy openSpy(&dialog, &SubmoduleDialog::sigOpenRequested);
        QSignalSpy addSpy(&dialog, &SubmoduleDialog::sigAddRequested);
        QSignalSpy updateSpy(&dialog, &SubmoduleDialog::sigUpdateRequested);
        QSignalSpy syncSpy(&dialog, &SubmoduleDialog::sigSyncRequested);
        QSignalSpy branchSpy(&dialog, &SubmoduleDialog::sigBranchRequested);

        QVERIFY(!addButton->isEnabled());
        urlEdit->setText(QStringLiteral("D:/sources/new-library"));
        pathEdit->setText(QStringLiteral("deps/new-library"));
        QVERIFY(addButton->isEnabled());
        addButton->click();
        QCOMPARE(addSpy.count(), 1);
        QCOMPARE(addSpy.first().at(0).toString(), QStringLiteral("D:/sources/new-library"));
        QCOMPARE(addSpy.first().at(1).toString(), QStringLiteral("deps/new-library"));

        list->setCurrentRow(0);
        QVERIFY(openButton->isEnabled());
        QVERIFY(updateButton->isEnabled());
        QVERIFY(syncButton->isEnabled());
        QVERIFY(branchButton->isEnabled());
        QVERIFY(removeButton->isEnabled());
        branchEdit->setText(QStringLiteral("release"));
        openButton->click();
        updateButton->click();
        syncButton->click();
        branchButton->click();
        QCOMPARE(openSpy.first().at(0).toString(), QStringLiteral("deps/library"));
        QCOMPARE(updateSpy.first().at(0).toString(), QStringLiteral("deps/library"));
        QCOMPARE(syncSpy.first().at(0).toString(), QStringLiteral("deps/library"));
        QCOMPARE(branchSpy.first().at(0).toString(), QStringLiteral("deps/library"));
        QCOMPARE(branchSpy.first().at(1).toString(), QStringLiteral("release"));
    }

    void lfsDialogEmitsManagementActions()
    {
        Git::LfsState state;
        state.installed = true;
        state.version = QStringLiteral("git-lfs/3.6.0");
        state.trackedPatterns = {QStringLiteral("*.psd")};
        Git::LfsLockInfo lock;
        lock.id = QStringLiteral("42");
        lock.path = QStringLiteral("art/hero.psd");
        lock.owner = QStringLiteral("Alice");
        state.locks.append(lock);

        LfsDialog dialog(state);
        auto* patterns = dialog.findChild<QListWidget*>(QStringLiteral("lfsPatternList"));
        auto* locks = dialog.findChild<QListWidget*>(QStringLiteral("lfsLockList"));
        auto* patternEdit = dialog.findChild<QLineEdit*>(QStringLiteral("lfsPatternEdit"));
        auto* pathEdit = dialog.findChild<QLineEdit*>(QStringLiteral("lfsLockPathEdit"));
        auto* force = dialog.findChild<QCheckBox*>(QStringLiteral("lfsForceUnlockCheck"));
        auto* trackButton = dialog.findChild<QPushButton*>(QStringLiteral("lfsTrackButton"));
        auto* untrackButton = dialog.findChild<QPushButton*>(QStringLiteral("lfsUntrackButton"));
        auto* lockButton = dialog.findChild<QPushButton*>(QStringLiteral("lfsLockButton"));
        auto* unlockButton = dialog.findChild<QPushButton*>(QStringLiteral("lfsUnlockButton"));
        QVERIFY(patterns && locks && patternEdit && pathEdit && force);
        QVERIFY(trackButton && untrackButton && lockButton && unlockButton);

        QSignalSpy trackSpy(&dialog, &LfsDialog::sigTrackRequested);
        QSignalSpy untrackSpy(&dialog, &LfsDialog::sigUntrackRequested);
        QSignalSpy lockSpy(&dialog, &LfsDialog::sigLockRequested);
        QSignalSpy unlockSpy(&dialog, &LfsDialog::sigUnlockRequested);
        patternEdit->setText(QStringLiteral("*.mov"));
        trackButton->click();
        patterns->setCurrentRow(0);
        untrackButton->click();
        pathEdit->setText(QStringLiteral("video/intro.mov"));
        lockButton->click();
        locks->setCurrentRow(0);
        force->setChecked(true);
        unlockButton->click();

        QCOMPARE(trackSpy.first().at(0).toString(), QStringLiteral("*.mov"));
        QCOMPARE(untrackSpy.first().at(0).toString(), QStringLiteral("*.psd"));
        QCOMPARE(lockSpy.first().at(0).toString(), QStringLiteral("video/intro.mov"));
        QCOMPARE(unlockSpy.first().at(0).toString(), QStringLiteral("art/hero.psd"));
        QVERIFY(unlockSpy.first().at(1).toBool());
    }

    void hostingDialogOpensGeneratedUrls()
    {
        Git::HostingRemoteInfo remote;
        remote.remoteName = QStringLiteral("origin");
        remote.provider = Git::HostingProvider::GitHub;
        remote.webUrl = QStringLiteral("https://github.com/org/repo");
        remote.commitUrl = remote.webUrl + QStringLiteral("/commit/1234");
        remote.createChangeUrl = remote.webUrl + QStringLiteral("/compare/main?expand=1");
        remote.changesUrl = remote.webUrl + QStringLiteral("/pulls");
        remote.issuesUrl = remote.webUrl + QStringLiteral("/issues");
        HostingDialog dialog({remote}, {},
            {{static_cast<int>(Git::HostingProvider::GitHub),
              QStringLiteral("saved-token")}});

        auto* list = dialog.findChild<QListWidget*>(QStringLiteral("hostingRemoteList"));
        auto* repository = dialog.findChild<QPushButton*>(QStringLiteral("hostingRepositoryButton"));
        auto* commit = dialog.findChild<QPushButton*>(QStringLiteral("hostingCommitButton"));
        auto* change = dialog.findChild<QPushButton*>(QStringLiteral("hostingChangeButton"));
        auto* changes = dialog.findChild<QPushButton*>(QStringLiteral("hostingChangesButton"));
        auto* issues = dialog.findChild<QPushButton*>(QStringLiteral("hostingIssuesButton"));
        auto* token = dialog.findChild<QLineEdit*>(QStringLiteral("hostingTokenEdit"));
        auto* load = dialog.findChild<QPushButton*>(QStringLiteral("hostingLoadChangesButton"));
        auto* loadIssues = dialog.findChild<QPushButton*>(QStringLiteral("hostingLoadIssuesButton"));
        auto* remember = dialog.findChild<QCheckBox*>(QStringLiteral("hostingRememberTokenCheck"));
        auto* forget = dialog.findChild<QPushButton*>(QStringLiteral("hostingForgetTokenButton"));
        QVERIFY(list && repository && commit && change && changes && issues
                && token && load && loadIssues && remember && forget);
        dialog.resize(640, 480);
        dialog.show();
        QApplication::processEvents();
        const QList<QPushButton*> actionButtons = {
            repository, commit, change, changes, issues, load, loadIssues};
        for (int left = 0; left < actionButtons.size(); ++left) {
            for (int right = left + 1; right < actionButtons.size(); ++right)
                QVERIFY(!actionButtons.at(left)->geometry().intersects(
                    actionButtons.at(right)->geometry()));
        }
        QSignalSpy spy(&dialog, &HostingDialog::sigOpenUrlRequested);
        QSignalSpy loadSpy(&dialog, &HostingDialog::sigLoadChangesRequested);
        QSignalSpy loadIssuesSpy(&dialog, &HostingDialog::sigLoadIssuesRequested);
        QSignalSpy storeSpy(&dialog, &HostingDialog::sigStoreTokenRequested);
        QSignalSpy forgetSpy(&dialog, &HostingDialog::sigForgetTokenRequested);
        list->setCurrentRow(0);
        QCOMPARE(token->text(), QStringLiteral("saved-token"));
        QVERIFY(remember->isChecked());
        token->setText(QStringLiteral("session-token"));
        repository->click();
        commit->click();
        changes->click();
        change->click();
        issues->click();
        load->click();
        loadIssues->click();
        QCOMPARE(spy.count(), 5);
        QCOMPARE(spy.at(0).at(0).toString(), remote.webUrl);
        QCOMPARE(spy.at(2).at(0).toString(), remote.changesUrl);
        QCOMPARE(spy.at(4).at(0).toString(), remote.issuesUrl);
        QCOMPARE(loadSpy.count(), 1);
        QCOMPARE(loadSpy.first().at(1).toString(), QStringLiteral("session-token"));
        QCOMPARE(loadIssuesSpy.count(), 1);
        QCOMPARE(storeSpy.count(), 2);
        forget->click();
        QCOMPARE(forgetSpy.count(), 1);
        QVERIFY(token->text().isEmpty());
    }

    void hostingChangesDialogOpensReview()
    {
        Git::HostingRemoteInfo remote;
        remote.provider = Git::HostingProvider::GitHub;
        Git::HostingChangeInfo change;
        change.id = QStringLiteral("9");
        change.title = QStringLiteral("Review this change");
        change.author = QStringLiteral("alice");
        change.state = QStringLiteral("open");
        change.webUrl = QStringLiteral("https://github.com/org/repo/pull/9");
        HostingChangesDialog dialog(remote, {change});
        auto* list = dialog.findChild<QListWidget*>(QStringLiteral("hostingChangesList"));
        auto* open = dialog.findChild<QPushButton*>(QStringLiteral("hostingOpenChangeButton"));
        auto* review = dialog.findChild<QPushButton*>(QStringLiteral("hostingReviewFilesButton"));
        QVERIFY(list && open && review);
        QSignalSpy spy(&dialog, &HostingChangesDialog::sigOpenRequested);
        QSignalSpy reviewSpy(&dialog, &HostingChangesDialog::sigReviewFilesRequested);
        list->setCurrentRow(0);
        open->click();
        review->click();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toString(), change.webUrl);
        QCOMPARE(reviewSpy.count(), 1);
    }

    void hostingIssuesDialogOpensIssue()
    {
        Git::HostingRemoteInfo remote;
        remote.provider = Git::HostingProvider::GitLab;
        Git::HostingIssueInfo issue;
        issue.id = QStringLiteral("6");
        issue.title = QStringLiteral("Fix regression");
        issue.author = QStringLiteral("bob");
        issue.state = QStringLiteral("opened");
        issue.webUrl = QStringLiteral("https://gitlab.com/o/r/-/issues/6");
        HostingIssuesDialog dialog(remote, {issue});
        auto* list = dialog.findChild<QListWidget*>(QStringLiteral("hostingIssuesList"));
        auto* open = dialog.findChild<QPushButton*>(QStringLiteral("hostingOpenIssueButton"));
        QVERIFY(list && open);
        QSignalSpy spy(&dialog, &HostingIssuesDialog::sigOpenRequested);
        list->setCurrentRow(0);
        open->click();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toString(), issue.webUrl);
    }

    void hostingReviewDialogDisplaysPatch()
    {
        Git::HostingChangeInfo change;
        change.id = QStringLiteral("10");
        change.title = QStringLiteral("Update code");
        Git::HostingReviewFile file;
        file.path = QStringLiteral("src/main.cpp");
        file.status = QStringLiteral("modified");
        file.patch = QStringLiteral("@@ -1 +1 @@\n-old\n+new");
        file.webUrl = QStringLiteral("https://github.com/o/r/blob/x/src/main.cpp");
        Git::HostingRemoteInfo remote;
        remote.provider = Git::HostingProvider::GitHub;
        HostingReviewDialog dialog(remote, change, {file});
        auto* list = dialog.findChild<QListWidget*>(QStringLiteral("hostingReviewFileList"));
        auto* patch = dialog.findChild<QPlainTextEdit*>(QStringLiteral("hostingReviewPatch"));
        auto* open = dialog.findChild<QPushButton*>(QStringLiteral("hostingReviewOpenFileButton"));
        QVERIFY(list && patch && open);
        QVERIFY(patch->toPlainText().contains(QStringLiteral("+new")));
        QSignalSpy spy(&dialog, &HostingReviewDialog::sigOpenFileRequested);
        open->click();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toString(), file.webUrl);
        QVERIFY(HostingReviewDialog::patchContainsNewLine(file.patch, 1));
        QVERIFY(!HostingReviewDialog::patchContainsNewLine(file.patch, 2));
    }

    void diagnosticsDialogRequestsSelectedRemote()
    {
        Git::GitDiagnosticReport report;
        report.items.append({QStringLiteral("Tools"), QStringLiteral("Git"),
                             QStringLiteral("git version 2.50"), false});
        report.remotes.append({QStringLiteral("origin"),
                               QStringLiteral("git@example.com:o/r.git"), {}});
        GitDiagnosticsDialog dialog(report);
        auto* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("gitDiagnosticsTree"));
        auto* button = dialog.findChild<QPushButton*>(QStringLiteral("diagnosticTestRemoteButton"));
        QVERIFY(tree && button);
        QCOMPARE(tree->topLevelItemCount(), 1);
        QSignalSpy spy(&dialog, &GitDiagnosticsDialog::sigTestRemoteRequested);
        button->click();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toString(), QStringLiteral("git@example.com:o/r.git"));
    }

    void hooksDialogDisplaysHooks()
    {
        Git::HookInfo hook;
        hook.name = QStringLiteral("pre-commit");
        hook.path = QStringLiteral("D:/repo/.git/hooks/pre-commit");
        hook.executable = true;
        HooksDialog dialog({hook});
        auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("hooksTable"));
        QVERIFY(table);
        QCOMPARE(table->rowCount(), 1);
        QCOMPARE(table->item(0, 0)->text(), QStringLiteral("pre-commit"));
    }

    void externalToolsDialogPersistsCommands()
    {
        QSettings(QStringLiteral("GitManager"), QStringLiteral("GitManager")).clear();
        Git::ExternalToolService::setDiffCommand(QStringLiteral("meld {file}"));
        Git::ExternalToolService::setMergeCommand(
            QStringLiteral("meld {base} {local} {remote} {merged}"));
        QCOMPARE(Git::ExternalToolService::diffCommand(), QStringLiteral("meld {file}"));
        ExternalToolsDialog dialog;
        auto* diff = dialog.findChild<QLineEdit*>(
            QStringLiteral("externalDiffCommandEdit"));
        auto* merge = dialog.findChild<QLineEdit*>(
            QStringLiteral("externalMergeCommandEdit"));
        QVERIFY(diff);
        QVERIFY(merge);
        QCOMPARE(diff->text(), QStringLiteral("meld {file}"));
        QCOMPARE(merge->text(), QStringLiteral("meld {base} {local} {remote} {merged}"));
    }

    void settingsDialogOffersLayoutReset()
    {
        SettingsDialog dialog;
        QVERIFY(dialog.findChild<QPushButton*>(
            QStringLiteral("resetWindowLayoutButton")));
    }

    void welcomeAndNotificationWidgetsExposeExpectedActions()
    {
        WelcomeWidget welcome;
        QVERIFY(welcome.findChild<QPushButton*>(QStringLiteral("welcomeOpenButton")));
        QVERIFY(welcome.findChild<QPushButton*>(QStringLiteral("welcomeInitButton")));
        QVERIFY(welcome.findChild<QPushButton*>(QStringLiteral("welcomeCloneButton")));
        QCOMPARE(welcome.findChild<QPushButton*>(QStringLiteral("welcomeOpenButton"))
                     ->accessibleName(),
                 QStringLiteral("Open existing repository"));
        welcome.setRecentRepositories(
            {QStringLiteral("C:/work/repo-a"), QStringLiteral("C:/work/repo-b")});
        auto* recent = welcome.findChild<QPushButton*>(
            QStringLiteral("welcomeRecentButton_0"));
        QVERIFY(recent);
        QCOMPARE(recent->text(), QStringLiteral("repo-a"));
        QSignalSpy recentSpy(&welcome,
                             &WelcomeWidget::sigRecentRepositoryRequested);
        recent->click();
        QCOMPARE(recentSpy.count(), 1);
        QCOMPARE(recentSpy.takeFirst().at(0).toString(),
                 QStringLiteral("C:/work/repo-a"));
        NotificationWidget notification;
        notification.showMessage(QStringLiteral("Saved"), NotificationWidget::Level::Success, 0);
        QCOMPARE(notification.text(), QStringLiteral("Saved"));
        QCOMPARE(notification.accessibleName(),
                 QStringLiteral("Application notification"));
        QVERIFY(notification.isVisible());
        notification.dismiss();
        QVERIFY(!notification.isVisible());
    }

    void hardResetRequiresConfirmation()
    {
        ResetDialog dialog(preview());
        auto* hardRadio = dialog.findChild<QRadioButton*>(
            QStringLiteral("resetHardRadio"));
        auto* hardConfirm = dialog.findChild<QCheckBox*>(
            QStringLiteral("resetHardConfirmCheck"));
        auto* resetButton = dialog.findChild<QPushButton*>(
            QStringLiteral("resetButton"));
        QVERIFY(hardRadio);
        QVERIFY(hardConfirm);
        QVERIFY(resetButton);

        hardRadio->setChecked(true);
        QCOMPARE(dialog.resetMode(), Git::ResetMode::Hard);
        QVERIFY(!resetButton->isEnabled());
        hardConfirm->setChecked(true);
        QVERIFY(resetButton->isEnabled());
    }

    void publishedResetRequiresIndependentConfirmation()
    {
        Git::HistoryRewritePreview value = preview();
        value.publishedCount = 2;
        ResetDialog dialog(value);
        auto* publishedConfirm = dialog.findChild<QCheckBox*>(
            QStringLiteral("resetPublishedConfirmCheck"));
        auto* hardRadio = dialog.findChild<QRadioButton*>(
            QStringLiteral("resetHardRadio"));
        auto* hardConfirm = dialog.findChild<QCheckBox*>(
            QStringLiteral("resetHardConfirmCheck"));
        auto* resetButton = dialog.findChild<QPushButton*>(
            QStringLiteral("resetButton"));
        QVERIFY(publishedConfirm);
        QVERIFY(hardRadio);
        QVERIFY(hardConfirm);
        QVERIFY(resetButton);

        QVERIFY(!resetButton->isEnabled());
        publishedConfirm->setChecked(true);
        QVERIFY(resetButton->isEnabled());
        hardRadio->setChecked(true);
        QVERIFY(!resetButton->isEnabled());
        hardConfirm->setChecked(true);
        QVERIFY(resetButton->isEnabled());
    }

    void resetBlocksActiveRepositoryOperation()
    {
        Git::HistoryRewritePreview value = preview();
        value.activeOperation = Git::RepositoryOperation::Rebase;
        ResetDialog dialog(value);
        auto* resetButton = dialog.findChild<QPushButton*>(
            QStringLiteral("resetButton"));
        auto* blockingReason = dialog.findChild<QLabel*>(
            QStringLiteral("resetBlockingReasonLabel"));
        QVERIFY(resetButton);
        QVERIFY(blockingReason);
        QVERIFY(!resetButton->isEnabled());
        QVERIFY(blockingReason->isVisibleTo(&dialog)
                || !blockingReason->text().isEmpty());
    }

    void interactiveWidgetSupportsAllActions()
    {
        InteractiveRebaseWidget widget(plan());
        const std::array<Git::RebaseAction, 6> actions {{
            Git::RebaseAction::Pick,
            Git::RebaseAction::Reword,
            Git::RebaseAction::Edit,
            Git::RebaseAction::Squash,
            Git::RebaseAction::Fixup,
            Git::RebaseAction::Drop,
        }};

        for (Git::RebaseAction action : actions) {
            setAction(widget, 1, action);
            QCOMPARE(widget.rebasePlan().items.at(1).action, action);
        }
    }

    void interactiveWidgetValidatesFirstNonDropAndMessages()
    {
        InteractiveRebaseWidget widget(plan());
        setAction(widget, 0, Git::RebaseAction::Drop);
        setAction(widget, 1, Git::RebaseAction::Squash);
        QVERIFY(!widget.isPlanValid());

        setAction(widget, 1, Git::RebaseAction::Fixup);
        QVERIFY(!widget.isPlanValid());

        setAction(widget, 0, Git::RebaseAction::Pick);
        setAction(widget, 1, Git::RebaseAction::Squash);
        QVERIFY(widget.isPlanValid());

        auto* table = widget.findChild<QTableWidget*>(
            QStringLiteral("interactiveRebaseTable"));
        auto* messageEdit = widget.findChild<QPlainTextEdit*>(
            QStringLiteral("rebaseMessageEdit"));
        QVERIFY(table);
        QVERIFY(messageEdit);
        table->setCurrentCell(1, 0);
        setAction(widget, 1, Git::RebaseAction::Reword);
        messageEdit->clear();
        QVERIFY(!widget.isPlanValid());
        messageEdit->setPlainText(QStringLiteral("rewritten message"));
        QVERIFY(widget.isPlanValid());
        QCOMPARE(widget.rebasePlan().items.at(1).rewrittenMessage,
                 QStringLiteral("rewritten message"));
    }

    void rebaseDialogDefaultsToNormalAndReturnsInteractivePlan()
    {
        RebaseDialog dialog(plan());
        auto* startButton = dialog.findChild<QPushButton*>(
            QStringLiteral("startRebaseButton"));
        auto* interactiveRadio = dialog.findChild<QRadioButton*>(
            QStringLiteral("rebaseInteractiveRadio"));
        auto* editor = dialog.findChild<InteractiveRebaseWidget*>(
            QStringLiteral("interactiveRebaseWidget"));
        QVERIFY(startButton);
        QVERIFY(interactiveRadio);
        QVERIFY(editor);
        QVERIFY(!dialog.interactiveMode());
        QVERIFY(startButton->isEnabled());

        interactiveRadio->setChecked(true);
        QVERIFY(dialog.interactiveMode());
        setAction(*editor, 1, Git::RebaseAction::Drop);
        QCOMPARE(dialog.rebasePlan().items.at(1).action,
                 Git::RebaseAction::Drop);
    }

    void rebaseDialogGatesPublishedDirtyActiveAndEmptyPlans()
    {
        Git::RebasePlan published = plan();
        published.preview.publishedCount = 1;
        published.items[0].published = true;
        RebaseDialog publishedDialog(published);
        auto* publishedStart = publishedDialog.findChild<QPushButton*>(
            QStringLiteral("startRebaseButton"));
        auto* publishedConfirm = publishedDialog.findChild<QCheckBox*>(
            QStringLiteral("rebasePublishedConfirmCheck"));
        QVERIFY(publishedStart);
        QVERIFY(publishedConfirm);
        QVERIFY(!publishedStart->isEnabled());
        publishedConfirm->setChecked(true);
        QVERIFY(publishedStart->isEnabled());

        Git::RebasePlan dirty = plan();
        dirty.preview.dirty = true;
        RebaseDialog dirtyDialog(dirty);
        QVERIFY(!dirtyDialog.findChild<QPushButton*>(
            QStringLiteral("startRebaseButton"))->isEnabled());

        Git::RebasePlan active = plan();
        active.preview.activeOperation = Git::RepositoryOperation::Merge;
        RebaseDialog activeDialog(active);
        QVERIFY(!activeDialog.findChild<QPushButton*>(
            QStringLiteral("startRebaseButton"))->isEnabled());

        RebaseDialog emptyDialog(plan(0));
        QVERIFY(!emptyDialog.findChild<QPushButton*>(
            QStringLiteral("startRebaseButton"))->isEnabled());
    }
};

int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QApplication application(argc, argv);
    TestHistoryDialogs test;
    return QTest::qExec(&test, argc, argv);
}

#include "TestHistoryDialogs.moc"
