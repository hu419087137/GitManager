#include "SubmoduleDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
QString labelFor(const Git::SubmoduleInfo& submodule)
{
    QString state = submodule.initialized
        ? QStringLiteral("initialized") : QStringLiteral("not initialized");
    if (submodule.dirty)
        state += QStringLiteral(", modified");
    return QStringLiteral("%1  [%2]  (%3)")
        .arg(submodule.name, submodule.path, state);
}
}

SubmoduleDialog::SubmoduleDialog(
    const QVector<Git::SubmoduleInfo>& submodules, QWidget* parent)
    : QDialog(parent)
    , _submodules(submodules)
{
    setWindowTitle(QStringLiteral("Submodules"));
    resize(760, 420);

    _list = new QListWidget(this);
    _list->setObjectName(QStringLiteral("submoduleList"));
    for (int index = 0; index < _submodules.size(); ++index) {
        const auto& submodule = _submodules.at(index);
        auto* item = new QListWidgetItem(labelFor(submodule), _list);
        item->setData(Qt::UserRole, index);
        item->setToolTip(QStringLiteral("URL: %1\nBranch: %2\nIndex: %3\nWorktree: %4")
                             .arg(submodule.url, submodule.branch,
                                  submodule.indexHash, submodule.workdirHash));
    }

    _urlEdit = new QLineEdit(this);
    _urlEdit->setObjectName(QStringLiteral("submoduleUrlEdit"));
    _urlEdit->setPlaceholderText(QStringLiteral("Repository URL or local path"));
    _pathEdit = new QLineEdit(this);
    _pathEdit->setObjectName(QStringLiteral("submodulePathEdit"));
    _pathEdit->setPlaceholderText(QStringLiteral("Relative path, e.g. deps/library"));
    _branchEdit = new QLineEdit(this);
    _branchEdit->setObjectName(QStringLiteral("submoduleBranchEdit"));
    _branchEdit->setPlaceholderText(QStringLiteral("Tracked branch; empty uses remote HEAD"));

    _openButton = new QPushButton(QStringLiteral("Open"), this);
    _openButton->setObjectName(QStringLiteral("submoduleOpenButton"));
    _updateButton = new QPushButton(QStringLiteral("Initialize / Update"), this);
    _updateButton->setObjectName(QStringLiteral("submoduleUpdateButton"));
    _syncButton = new QPushButton(QStringLiteral("Sync URL"), this);
    _syncButton->setObjectName(QStringLiteral("submoduleSyncButton"));
    _addButton = new QPushButton(QStringLiteral("Add Submodule"), this);
    _addButton->setObjectName(QStringLiteral("submoduleAddButton"));
    _branchButton = new QPushButton(QStringLiteral("Set Branch"), this);
    _branchButton->setObjectName(QStringLiteral("submoduleBranchButton"));
    _removeButton = new QPushButton(QStringLiteral("Remove"), this);
    _removeButton->setObjectName(QStringLiteral("submoduleRemoveButton"));

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("URL:"), _urlEdit);
    form->addRow(QStringLiteral("Path:"), _pathEdit);
    form->addRow(QString(), _addButton);
    form->addRow(QStringLiteral("Tracked branch:"), _branchEdit);
    form->addRow(QString(), _branchButton);

    auto* content = new QHBoxLayout;
    content->addWidget(_list, 2);
    content->addLayout(form, 1);

    auto* actions = new QHBoxLayout;
    actions->addWidget(_openButton);
    actions->addWidget(_updateButton);
    actions->addWidget(_syncButton);
    actions->addWidget(_removeButton);
    actions->addStretch();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(
        QStringLiteral("Add, initialize, update, or synchronize repository submodules."),
        this));
    layout->addLayout(content, 1);
    layout->addLayout(actions);
    layout->addWidget(buttons);

    connect(_list, &QListWidget::currentRowChanged,
            this, &SubmoduleDialog::updateActions);
    connect(_urlEdit, &QLineEdit::textChanged,
            this, &SubmoduleDialog::updateActions);
    connect(_pathEdit, &QLineEdit::textChanged,
            this, &SubmoduleDialog::updateActions);
    connect(_openButton, &QPushButton::clicked, this, &SubmoduleDialog::slotOpen);
    connect(_addButton, &QPushButton::clicked, this, &SubmoduleDialog::slotAdd);
    connect(_updateButton, &QPushButton::clicked, this, &SubmoduleDialog::slotUpdate);
    connect(_syncButton, &QPushButton::clicked, this, &SubmoduleDialog::slotSync);
    connect(_branchButton, &QPushButton::clicked,
            this, &SubmoduleDialog::slotSetBranch);
    connect(_removeButton, &QPushButton::clicked,
            this, &SubmoduleDialog::slotRemove);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    updateActions();
}

const Git::SubmoduleInfo* SubmoduleDialog::selectedSubmodule() const
{
    const auto* item = _list->currentItem();
    if (!item)
        return nullptr;
    const int index = item->data(Qt::UserRole).toInt();
    return index >= 0 && index < _submodules.size()
        ? &_submodules.at(index) : nullptr;
}

void SubmoduleDialog::updateActions()
{
    const auto* submodule = selectedSubmodule();
    _openButton->setEnabled(submodule && submodule->initialized);
    _updateButton->setEnabled(submodule);
    _syncButton->setEnabled(submodule);
    _branchButton->setEnabled(submodule);
    _removeButton->setEnabled(submodule);
    if (submodule && !_branchEdit->hasFocus())
        _branchEdit->setText(submodule->branch);
    _addButton->setEnabled(!_urlEdit->text().trimmed().isEmpty()
                           && !_pathEdit->text().trimmed().isEmpty());
}

void SubmoduleDialog::slotOpen()
{
    if (const auto* submodule = selectedSubmodule())
        emit sigOpenRequested(submodule->path);
}

void SubmoduleDialog::slotAdd()
{
    emit sigAddRequested(_urlEdit->text().trimmed(),
                         _pathEdit->text().trimmed());
}

void SubmoduleDialog::slotUpdate()
{
    if (const auto* submodule = selectedSubmodule())
        emit sigUpdateRequested(submodule->name);
}

void SubmoduleDialog::slotSync()
{
    if (const auto* submodule = selectedSubmodule())
        emit sigSyncRequested(submodule->name);
}

void SubmoduleDialog::slotSetBranch()
{
    if (const auto* submodule = selectedSubmodule())
        emit sigBranchRequested(submodule->name, _branchEdit->text().trimmed());
}

void SubmoduleDialog::slotRemove()
{
    const auto* submodule = selectedSubmodule();
    if (!submodule)
        return;
    const QString warning = submodule->dirty
        ? QStringLiteral("\n\nThis submodule has local changes. They will be deleted.")
        : QString();
    if (QMessageBox::question(
            this, QStringLiteral("Remove Submodule"),
            QStringLiteral("Remove submodule '%1' and its working directory?%2")
                .arg(submodule->name, warning),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel) != QMessageBox::Yes) {
        return;
    }
    emit sigRemoveRequested(submodule->name, submodule->dirty);
}
