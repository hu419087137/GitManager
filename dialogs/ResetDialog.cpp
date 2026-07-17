#include "ResetDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace {

QString displayRevision(const Git::HistoryRewritePreview& preview)
{
    if (!preview.revision.trimmed().isEmpty())
        return preview.revision.trimmed();
    return preview.targetHash.left(8);
}

} // namespace

ResetDialog::ResetDialog(const Git::HistoryRewritePreview& preview,
                         QWidget* parent)
    : QDialog(parent),
      _preview(preview)
{
    setWindowTitle(QStringLiteral("Reset Current Branch"));
    setMinimumWidth(560);

    auto* summary = new QLabel(
        QStringLiteral("Reset %1 from %2 to %3")
            .arg(preview.currentBranch.isEmpty()
                     ? QStringLiteral("HEAD") : preview.currentBranch,
                 preview.expectedHead.left(8), displayRevision(preview)),
        this);
    summary->setObjectName(QStringLiteral("resetSummaryLabel"));
    summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    summary->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    auto* modeBox = new QGroupBox(QStringLiteral("Reset mode"), this);
    _softRadio = new QRadioButton(QStringLiteral("Soft"), modeBox);
    _softRadio->setObjectName(QStringLiteral("resetSoftRadio"));
    _mixedRadio = new QRadioButton(QStringLiteral("Mixed"), modeBox);
    _mixedRadio->setObjectName(QStringLiteral("resetMixedRadio"));
    _hardRadio = new QRadioButton(QStringLiteral("Hard"), modeBox);
    _hardRadio->setObjectName(QStringLiteral("resetHardRadio"));
    _mixedRadio->setChecked(true);

    auto* modeLayout = new QVBoxLayout(modeBox);
    modeLayout->addWidget(_softRadio);
    modeLayout->addWidget(_mixedRadio);
    modeLayout->addWidget(_hardRadio);

    _modeDescription = new QLabel(this);
    _modeDescription->setObjectName(QStringLiteral("resetModeDescriptionLabel"));
    _modeDescription->setWordWrap(true);

    _hardWarning = new QLabel(
        QStringLiteral("Hard reset permanently discards staged and tracked working-tree changes. "
                       "Untracked files normally remain, but an untracked path that obstructs a "
                       "tracked file may be removed."),
        this);
    _hardWarning->setObjectName(QStringLiteral("resetHardWarningLabel"));
    _hardWarning->setWordWrap(true);
    _hardWarning->setStyleSheet(
        QStringLiteral("color: #b42318; font-weight: 600;"));

    _hardConfirm = new QCheckBox(
        QStringLiteral("I understand that tracked changes may be lost permanently."), this);
    _hardConfirm->setObjectName(QStringLiteral("resetHardConfirmCheck"));

    _publishedWarning = new QLabel(this);
    _publishedWarning->setObjectName(QStringLiteral("resetPublishedWarningLabel"));
    _publishedWarning->setWordWrap(true);
    _publishedWarning->setStyleSheet(
        QStringLiteral("color: #b42318; font-weight: 600;"));

    _publishedConfirm = new QCheckBox(
        QStringLiteral("I understand that this rewrites published history."), this);
    _publishedConfirm->setObjectName(QStringLiteral("resetPublishedConfirmCheck"));

    const bool rewritesPublished = preview.publishedCount > 0;
    if (rewritesPublished) {
        const QString upstream = preview.upstream.isEmpty()
            ? QStringLiteral("the configured upstream") : preview.upstream;
        _publishedWarning->setText(
            QStringLiteral("This reset removes %1 commit(s) already reachable from %2. "
                           "A later normal push will be rejected; updating the remote requires "
                           "force-with-lease.")
                .arg(preview.publishedCount).arg(upstream));
    }
    _publishedWarning->setVisible(rewritesPublished);
    _publishedConfirm->setVisible(rewritesPublished);

    _blockingReason = new QLabel(this);
    _blockingReason->setObjectName(QStringLiteral("resetBlockingReasonLabel"));
    _blockingReason->setWordWrap(true);
    _blockingReason->setStyleSheet(
        QStringLiteral("color: #b42318; font-weight: 600;"));
    const bool operationActive =
        preview.activeOperation != Git::RepositoryOperation::None;
    if (operationActive) {
        _blockingReason->setText(QStringLiteral(
            "Cannot reset while another repository operation is active. "
            "Continue or abort it first."));
    }
    _blockingReason->setVisible(operationActive);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    _resetButton = buttonBox->addButton(QStringLiteral("Reset"),
                                        QDialogButtonBox::AcceptRole);
    _resetButton->setObjectName(QStringLiteral("resetButton"));
    _resetButton->setAutoDefault(false);
    if (auto* cancelButton = buttonBox->button(QDialogButtonBox::Cancel))
        cancelButton->setDefault(true);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(summary);
    layout->addSpacing(6);
    layout->addWidget(modeBox);
    layout->addWidget(_modeDescription);
    layout->addWidget(_hardWarning);
    layout->addWidget(_hardConfirm);
    layout->addWidget(_publishedWarning);
    layout->addWidget(_publishedConfirm);
    layout->addWidget(_blockingReason);
    layout->addSpacing(6);
    layout->addWidget(buttonBox);

    const auto modeChanged = [this] {
        updateModeDescription();
        updateAcceptState();
    };
    connect(_softRadio, &QRadioButton::toggled, this, modeChanged);
    connect(_mixedRadio, &QRadioButton::toggled, this, modeChanged);
    connect(_hardRadio, &QRadioButton::toggled, this, modeChanged);
    connect(_hardConfirm, &QCheckBox::toggled,
            this, &ResetDialog::updateAcceptState);
    connect(_publishedConfirm, &QCheckBox::toggled,
            this, &ResetDialog::updateAcceptState);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    updateModeDescription();
    updateAcceptState();
}

Git::ResetMode ResetDialog::resetMode() const
{
    if (_softRadio->isChecked())
        return Git::ResetMode::Soft;
    if (_hardRadio->isChecked())
        return Git::ResetMode::Hard;
    return Git::ResetMode::Mixed;
}

void ResetDialog::updateModeDescription()
{
    const bool hard = _hardRadio->isChecked();
    _hardWarning->setVisible(hard);
    _hardConfirm->setVisible(hard);

    switch (resetMode()) {
    case Git::ResetMode::Soft:
        _modeDescription->setText(
            QStringLiteral("Moves the current branch and HEAD. The index and working tree are unchanged."));
        break;
    case Git::ResetMode::Mixed:
        _modeDescription->setText(
            QStringLiteral("Moves the current branch and HEAD, and resets the index. Working-tree files are kept."));
        break;
    case Git::ResetMode::Hard:
        _modeDescription->setText(
            QStringLiteral("Moves the current branch and HEAD, then resets both the index and working tree."));
        break;
    }
}

void ResetDialog::updateAcceptState()
{
    const bool hardAccepted = !_hardRadio->isChecked()
        || _hardConfirm->isChecked();
    const bool publishedAccepted = _preview.publishedCount <= 0
        || _publishedConfirm->isChecked();
    const bool operationAccepted =
        _preview.activeOperation == Git::RepositoryOperation::None;
    _resetButton->setEnabled(hardAccepted && publishedAccepted
                             && operationAccepted);
}
