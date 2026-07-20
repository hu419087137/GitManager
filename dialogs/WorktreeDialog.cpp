#include "WorktreeDialog.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
QString worktreeLabel(const Git::WorktreeInfo& worktree)
{
    QString label = QStringLiteral("%1  [%2]").arg(worktree.name, worktree.path);
    if (worktree.current)
        label.prepend(QStringLiteral("* "));
    if (worktree.locked)
        label += QStringLiteral("  (locked)");
    if (!worktree.valid)
        label += QStringLiteral("  (missing)");
    return label;
}
}

WorktreeDialog::WorktreeDialog(const QVector<Git::WorktreeInfo>& worktrees,
                               QWidget* parent)
    : QDialog(parent)
    , _worktrees(worktrees)
{
    setWindowTitle(QStringLiteral("Worktrees"));
    resize(760, 420);

    _list = new QListWidget(this);
    _list->setObjectName(QStringLiteral("worktreeList"));
    for (int index = 0; index < _worktrees.size(); ++index) {
        const Git::WorktreeInfo& worktree = _worktrees.at(index);
        auto* item = new QListWidgetItem(worktreeLabel(worktree), _list);
        item->setData(Qt::UserRole, index);
        item->setToolTip(worktree.path);
    }

    _nameEdit = new QLineEdit(this);
    _nameEdit->setObjectName(QStringLiteral("worktreeNameEdit"));
    _nameEdit->setPlaceholderText(QStringLiteral("Worktree name"));

    _pathEdit = new QLineEdit(this);
    _pathEdit->setObjectName(QStringLiteral("worktreePathEdit"));
    _pathEdit->setPlaceholderText(QStringLiteral("Target path"));
    auto* browseButton = new QToolButton(this);
    browseButton->setText(QStringLiteral("..."));
    browseButton->setToolTip(QStringLiteral("Choose worktree path"));

    _branchEdit = new QLineEdit(this);
    _branchEdit->setObjectName(QStringLiteral("worktreeBranchEdit"));
    _branchEdit->setPlaceholderText(QStringLiteral("Branch name (optional)"));

    _movePathEdit = new QLineEdit(this);
    _movePathEdit->setObjectName(QStringLiteral("worktreeMovePathEdit"));
    _movePathEdit->setPlaceholderText(QStringLiteral("Move selected worktree to..."));

    _lockReasonEdit = new QLineEdit(this);
    _lockReasonEdit->setObjectName(QStringLiteral("worktreeLockReasonEdit"));
    _lockReasonEdit->setPlaceholderText(QStringLiteral("Lock reason (optional)"));

    _openButton = new QPushButton(QStringLiteral("Open"), this);
    _openButton->setObjectName(QStringLiteral("worktreeOpenButton"));
    _moveButton = new QPushButton(QStringLiteral("Move"), this);
    _moveButton->setObjectName(QStringLiteral("worktreeMoveButton"));
    _lockButton = new QPushButton(QStringLiteral("Lock"), this);
    _lockButton->setObjectName(QStringLiteral("worktreeLockButton"));
    _removeButton = new QPushButton(QStringLiteral("Remove"), this);
    _removeButton->setObjectName(QStringLiteral("worktreeRemoveButton"));
    _createButton = new QPushButton(QStringLiteral("Create"), this);
    _createButton->setObjectName(QStringLiteral("worktreeCreateButton"));

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);

    auto* form = new QVBoxLayout;
    form->addWidget(new QLabel(QStringLiteral("Worktree name:"), this));
    form->addWidget(_nameEdit);
    form->addWidget(new QLabel(QStringLiteral("Path:"), this));
    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(_pathEdit, 1);
    pathRow->addWidget(browseButton);
    form->addLayout(pathRow);
    form->addWidget(new QLabel(QStringLiteral("Branch name:"), this));
    form->addWidget(_branchEdit);
    form->addWidget(new QLabel(QStringLiteral("Move target path:"), this));
    form->addWidget(_movePathEdit);
    form->addWidget(new QLabel(QStringLiteral("Lock reason:"), this));
    form->addWidget(_lockReasonEdit);
    form->addWidget(_createButton);
    form->addStretch();

    auto* actions = new QHBoxLayout;
    actions->addWidget(_openButton);
    actions->addWidget(_moveButton);
    actions->addWidget(_lockButton);
    actions->addWidget(_removeButton);
    actions->addStretch();

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(
        QStringLiteral("Create, open, or remove linked worktrees for this repository."),
        this));
    auto* content = new QHBoxLayout;
    content->addWidget(_list, 2);
    content->addLayout(form, 1);
    layout->addLayout(content, 1);
    layout->addLayout(actions);
    layout->addWidget(buttonBox);

    connect(_list, &QListWidget::currentRowChanged, this,
            [this](int) { updateSelectionState(); });
    connect(_nameEdit, &QLineEdit::textChanged, this,
            [this](const QString&) { updateSelectionState(); });
    connect(_pathEdit, &QLineEdit::textChanged, this,
            [this](const QString&) { updateSelectionState(); });
    connect(_movePathEdit, &QLineEdit::textChanged, this,
            [this](const QString&) { updateSelectionState(); });
    connect(browseButton, &QToolButton::clicked, this, [this] {
        const QString path = QFileDialog::getExistingDirectory(
            this, QStringLiteral("Create Worktree"), _pathEdit->text());
        if (!path.isEmpty())
            _pathEdit->setText(path);
    });
    connect(_openButton, &QPushButton::clicked, this, &WorktreeDialog::slotOpen);
    connect(_moveButton, &QPushButton::clicked, this, &WorktreeDialog::slotMove);
    connect(_lockButton, &QPushButton::clicked, this, &WorktreeDialog::slotToggleLock);
    connect(_removeButton, &QPushButton::clicked, this, &WorktreeDialog::slotRemove);
    connect(_createButton, &QPushButton::clicked, this, &WorktreeDialog::slotCreate);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    updateSelectionState();
}

void WorktreeDialog::updateSelectionState()
{
    const QListWidgetItem* current = _list->currentItem();
    bool canOpen = false;
    bool canMove = false;
    bool canLock = false;
    bool locked = false;
    bool canRemove = false;
    if (current) {
        const int index = current->data(Qt::UserRole).toInt();
        if (index >= 0 && index < _worktrees.size()) {
            const Git::WorktreeInfo& worktree = _worktrees.at(index);
            canOpen = !worktree.path.isEmpty();
            canMove = !worktree.current
                && !worktree.path.isEmpty()
                && !_movePathEdit->text().trimmed().isEmpty()
                && QDir::cleanPath(_movePathEdit->text().trimmed())
                    != QDir::cleanPath(worktree.path);
            canLock = !worktree.current;
            locked = worktree.locked;
            canRemove = !worktree.current;
            _lockReasonEdit->setText(worktree.lockReason);
        }
    } else {
        _lockReasonEdit->clear();
    }
    _openButton->setEnabled(canOpen);
    _moveButton->setEnabled(canMove);
    _lockButton->setEnabled(canLock);
    _lockButton->setText(locked ? QStringLiteral("Unlock")
                                : QStringLiteral("Lock"));
    _removeButton->setEnabled(canRemove);
    _createButton->setEnabled(!_nameEdit->text().trimmed().isEmpty()
                              && !_pathEdit->text().trimmed().isEmpty());
}

void WorktreeDialog::slotCreate()
{
    emit sigCreateRequested(_nameEdit->text().trimmed(),
                            _pathEdit->text().trimmed(),
                            _branchEdit->text().trimmed());
}

void WorktreeDialog::slotOpen()
{
    const QListWidgetItem* current = _list->currentItem();
    if (!current)
        return;
    const int index = current->data(Qt::UserRole).toInt();
    if (index < 0 || index >= _worktrees.size())
        return;
    emit sigOpenRequested(_worktrees.at(index).path);
}

void WorktreeDialog::slotMove()
{
    const QListWidgetItem* current = _list->currentItem();
    if (!current)
        return;
    const int index = current->data(Qt::UserRole).toInt();
    if (index < 0 || index >= _worktrees.size())
        return;
    const Git::WorktreeInfo& worktree = _worktrees.at(index);
    const QString path = _movePathEdit->text().trimmed();
    if (worktree.current || path.isEmpty()
        || QDir::cleanPath(path) == QDir::cleanPath(worktree.path)) {
        return;
    }
    emit sigMoveRequested(worktree.name, path);
}

void WorktreeDialog::slotToggleLock()
{
    const QListWidgetItem* current = _list->currentItem();
    if (!current)
        return;
    const int index = current->data(Qt::UserRole).toInt();
    if (index < 0 || index >= _worktrees.size())
        return;
    const Git::WorktreeInfo& worktree = _worktrees.at(index);
    if (worktree.current)
        return;
    emit sigLockRequested(worktree.name, !worktree.locked,
                          _lockReasonEdit->text().trimmed());
}

void WorktreeDialog::slotRemove()
{
    const QListWidgetItem* current = _list->currentItem();
    if (!current)
        return;
    const int index = current->data(Qt::UserRole).toInt();
    if (index < 0 || index >= _worktrees.size())
        return;
    const Git::WorktreeInfo& worktree = _worktrees.at(index);
    if (QMessageBox::question(
            this, QStringLiteral("Remove Worktree"),
            QStringLiteral("Remove worktree '%1'?\n\n"
                           "If the linked directory is already gone, this will prune its metadata.")
                .arg(worktree.name),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }
    emit sigRemoveRequested(worktree.name, true);
}
