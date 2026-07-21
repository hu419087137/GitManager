#include "ExternalToolsDialog.h"
#include "../core/ExternalToolService.h"
#include <QAbstractButton>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

ExternalToolsDialog::ExternalToolsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("External Diff / Merge Tools"));
    resize(680, 210);
    _diffEdit = new QLineEdit(Git::ExternalToolService::diffCommand(), this);
    _diffEdit->setObjectName(QStringLiteral("externalDiffCommandEdit"));
    _mergeEdit = new QLineEdit(Git::ExternalToolService::mergeCommand(), this);
    _mergeEdit->setObjectName(QStringLiteral("externalMergeCommandEdit"));
    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("Diff command:"), _diffEdit);
    form->addRow(QStringLiteral("Merge command:"), _mergeEdit);
    auto* hint = new QLabel(QStringLiteral(
        "Placeholders are quoted automatically. Diff: {left}, {right}, {file}, {repo}. "
        "Merge: {base}, {local}, {remote}, {merged}, {repo}."), this);
    hint->setWordWrap(true);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(hint);
    layout->addWidget(buttons);
    const auto save = [this] {
        Git::ExternalToolService::setDiffCommand(_diffEdit->text());
        Git::ExternalToolService::setMergeCommand(_mergeEdit->text());
        accept();
    };
    connect(buttons, &QDialogButtonBox::accepted, this, save);
    connect(buttons, &QDialogButtonBox::clicked, this,
            [buttons, save](QAbstractButton* button) {
        if (buttons->standardButton(button) == QDialogButtonBox::Ok)
            save();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
