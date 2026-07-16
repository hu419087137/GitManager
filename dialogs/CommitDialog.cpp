#include "CommitDialog.h"
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>

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
    layout->addWidget(buttonBox);

    connect(_messageEdit, &QTextEdit::textChanged,
            this, &CommitDialog::slotTextChanged);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QString CommitDialog::commitMessage() const
{
    return _messageEdit->toPlainText().trimmed();
}

void CommitDialog::slotTextChanged()
{
    _commitBtn->setEnabled(!_messageEdit->toPlainText().trimmed().isEmpty());
}
