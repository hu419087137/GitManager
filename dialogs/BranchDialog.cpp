#include "BranchDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

BranchDialog::BranchDialog(Mode mode, QWidget* parent)
    : QDialog(parent)
    , _mode(mode)
{
    _summaryLabel = new QLabel(this);
    _summaryLabel->setWordWrap(true);
    _summaryLabel->setStyleSheet(QStringLiteral("color: #8b949e; font-size: 11px;"));

    _branchEdit = new QLineEdit(this);
    _branchEdit->setObjectName(QStringLiteral("branchDialogBranchEdit"));

    _sourceEdit = new QLineEdit(this);
    _sourceEdit->setObjectName(QStringLiteral("branchDialogSourceEdit"));

    _remoteEdit = new QLineEdit(this);
    _remoteEdit->setObjectName(QStringLiteral("branchDialogRemoteEdit"));

    auto* buttonBox = new QDialogButtonBox(this);
    _acceptButton = buttonBox->addButton(QDialogButtonBox::Ok);
    _acceptButton->setObjectName(QStringLiteral("branchDialogAcceptButton"));
    buttonBox->addButton(QDialogButtonBox::Cancel);

    auto* layout = new QVBoxLayout(this);

    switch (_mode) {
    case Mode::Create:
        setWindowTitle(QStringLiteral("New Branch"));
        _summaryLabel->setText(QStringLiteral(
            "Create a new local branch. Leave the source revision empty to branch from HEAD."));
        _branchEdit->setPlaceholderText(QStringLiteral("Branch name"));
        _sourceEdit->setPlaceholderText(QStringLiteral("Source revision (optional)"));
        layout->addWidget(new QLabel(QStringLiteral("Branch name:"), this));
        layout->addWidget(_branchEdit);
        layout->addWidget(new QLabel(QStringLiteral("Source revision:"), this));
        layout->addWidget(_sourceEdit);
        break;
    case Mode::Rename:
        setWindowTitle(QStringLiteral("Rename Branch"));
        _summaryLabel->setText(QStringLiteral(
            "Rename the selected local branch. Existing upstream configuration is kept when possible."));
        _branchEdit->setPlaceholderText(QStringLiteral("New branch name"));
        layout->addWidget(new QLabel(QStringLiteral("New branch name:"), this));
        layout->addWidget(_branchEdit);
        break;
    case Mode::Publish:
        setWindowTitle(QStringLiteral("Publish Branch"));
        _summaryLabel->setText(QStringLiteral(
            "Push the local branch to a remote and set its upstream tracking branch."));
        _branchEdit->setPlaceholderText(QStringLiteral("Branch name"));
        _remoteEdit->setPlaceholderText(QStringLiteral("Remote name"));
        layout->addWidget(new QLabel(QStringLiteral("Branch name:"), this));
        layout->addWidget(_branchEdit);
        layout->addWidget(new QLabel(QStringLiteral("Remote name:"), this));
        layout->addWidget(_remoteEdit);
        break;
    }

    layout->insertWidget(0, _summaryLabel);
    layout->addWidget(buttonBox);

    connect(_branchEdit, &QLineEdit::textChanged, this, &BranchDialog::updateState);
    connect(_sourceEdit, &QLineEdit::textChanged, this, &BranchDialog::updateState);
    connect(_remoteEdit, &QLineEdit::textChanged, this, &BranchDialog::updateState);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    updateState();
}

void BranchDialog::setBranchName(const QString& name)
{
    _branchEdit->setText(name);
}

void BranchDialog::setSourceRevision(const QString& revision)
{
    _sourceEdit->setText(revision);
}

void BranchDialog::setRemoteName(const QString& remote)
{
    _remoteEdit->setText(remote);
}

QString BranchDialog::branchName() const
{
    return _branchEdit->text().trimmed();
}

QString BranchDialog::sourceRevision() const
{
    return _sourceEdit->text().trimmed();
}

QString BranchDialog::remoteName() const
{
    return _remoteEdit->text().trimmed();
}

void BranchDialog::updateState()
{
    bool enabled = !branchName().isEmpty();
    if (_mode == Mode::Publish)
        enabled = enabled && !remoteName().isEmpty();
    _acceptButton->setEnabled(enabled);
}
