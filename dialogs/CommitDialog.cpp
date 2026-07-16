#include "CommitDialog.h"
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QCheckBox>

CommitDialog::CommitDialog(const QStringList& stagedFiles, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Commit"));
    setMinimumSize(480, 320);

    // 已暂存文件摘要
    const QString summary = stagedFiles.isEmpty()
        ? QStringLiteral("No staged files.")
        : QStringLiteral("%1 file(s) staged:\n  %2")
              .arg(stagedFiles.size())
              .arg(stagedFiles.join("\n  "));

    _filesLabel = new QLabel(summary, this);
    _filesLabel->setWordWrap(true);
    _filesLabel->setStyleSheet(QStringLiteral("color: #8b949e; font-size: 11px;"));

    _messageEdit = new QTextEdit(this);
    _messageEdit->setPlaceholderText(QStringLiteral("Commit message (required)"));
    _countLabel = new QLabel(QStringLiteral("0 characters"), this);
    _amendCheck = new QCheckBox(QStringLiteral("Amend previous commit"), this);
    _signoffCheck = new QCheckBox(QStringLiteral("Add Signed-off-by"), this);

    auto* buttonBox = new QDialogButtonBox(this);
    _commitBtn = buttonBox->addButton(QStringLiteral("Commit"),
                                      QDialogButtonBox::AcceptRole);
    _commitBtn->setEnabled(false);
    buttonBox->addButton(QDialogButtonBox::Cancel);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QStringLiteral("Staged files:"), this));
    layout->addWidget(_filesLabel);
    layout->addSpacing(8);
    layout->addWidget(new QLabel(QStringLiteral("Commit message:"), this));
    layout->addWidget(_messageEdit, 1);
    auto* options = new QHBoxLayout;
    options->addWidget(_amendCheck);
    options->addWidget(_signoffCheck);
    options->addStretch();
    options->addWidget(_countLabel);
    layout->addLayout(options);
    layout->addWidget(buttonBox);

    connect(_messageEdit, &QTextEdit::textChanged,
            this, &CommitDialog::slotTextChanged);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

bool CommitDialog::amend() const { return _amendCheck->isChecked(); }
bool CommitDialog::signoff() const { return _signoffCheck->isChecked(); }

QString CommitDialog::commitMessage() const
{
    return _messageEdit->toPlainText().trimmed();
}

void CommitDialog::slotTextChanged()
{
    _commitBtn->setEnabled(!_messageEdit->toPlainText().trimmed().isEmpty());
    _countLabel->setText(QStringLiteral("%1 characters").arg(_messageEdit->toPlainText().size()));
}
