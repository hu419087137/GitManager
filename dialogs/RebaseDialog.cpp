#include "RebaseDialog.h"

#include "../widgets/InteractiveRebaseWidget.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QHBoxLayout>
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

RebaseDialog::RebaseDialog(const Git::RebasePlan& plan, QWidget* parent)
    : QDialog(parent),
      _plan(plan)
{
    setWindowTitle(QStringLiteral("Rebase Current Branch"));
    setMinimumSize(620, 360);
    resize(780, 560);

    const Git::HistoryRewritePreview& preview = plan.preview;
    auto* summary = new QLabel(
        QStringLiteral("Rebase %1 onto %2\n%3 commit(s) will be replayed.")
            .arg(preview.currentBranch.isEmpty()
                     ? QStringLiteral("HEAD") : preview.currentBranch,
                 displayRevision(preview))
            .arg(preview.affectedCount),
        this);
    summary->setObjectName(QStringLiteral("rebaseSummaryLabel"));
    summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    summary->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    _normalRadio = new QRadioButton(QStringLiteral("Rebase"), this);
    _normalRadio->setObjectName(QStringLiteral("rebaseNormalRadio"));
    _interactiveRadio = new QRadioButton(QStringLiteral("Interactive"), this);
    _interactiveRadio->setObjectName(QStringLiteral("rebaseInteractiveRadio"));
    _normalRadio->setChecked(true);

    auto* modeLayout = new QHBoxLayout;
    modeLayout->addWidget(_normalRadio);
    modeLayout->addWidget(_interactiveRadio);
    modeLayout->addStretch();

    _interactiveWidget = new InteractiveRebaseWidget(plan, this);
    _interactiveWidget->setObjectName(QStringLiteral("interactiveRebaseWidget"));
    _interactiveWidget->hide();

    _publishedWarning = new QLabel(this);
    _publishedWarning->setObjectName(QStringLiteral("rebasePublishedWarningLabel"));
    _publishedWarning->setWordWrap(true);
    _publishedWarning->setStyleSheet(
        QStringLiteral("color: #b42318; font-weight: 600;"));
    _publishedConfirm = new QCheckBox(
        QStringLiteral("I understand that this rewrites published history."), this);
    _publishedConfirm->setObjectName(QStringLiteral("rebasePublishedConfirmCheck"));

    const bool rewritesPublished = preview.publishedCount > 0;
    if (rewritesPublished) {
        const QString upstream = preview.upstream.isEmpty()
            ? QStringLiteral("the configured upstream") : preview.upstream;
        _publishedWarning->setText(
            QStringLiteral("%1 commit(s) are already reachable from %2. "
                           "A later normal push will be rejected; updating the remote requires "
                           "force-with-lease.")
                .arg(preview.publishedCount).arg(upstream));
    }
    _publishedWarning->setVisible(rewritesPublished);
    _publishedConfirm->setVisible(rewritesPublished);

    _blockingReason = new QLabel(this);
    _blockingReason->setObjectName(QStringLiteral("rebaseBlockingReasonLabel"));
    _blockingReason->setWordWrap(true);
    _blockingReason->setStyleSheet(
        QStringLiteral("color: #b42318; font-weight: 600;"));

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    _startButton = buttonBox->addButton(QStringLiteral("Start Rebase"),
                                        QDialogButtonBox::AcceptRole);
    _startButton->setObjectName(QStringLiteral("startRebaseButton"));
    _startButton->setAutoDefault(false);
    if (auto* cancelButton = buttonBox->button(QDialogButtonBox::Cancel))
        cancelButton->setDefault(true);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(summary);
    layout->addLayout(modeLayout);
    layout->addWidget(_interactiveWidget, 1);
    layout->addWidget(_publishedWarning);
    layout->addWidget(_publishedConfirm);
    layout->addWidget(_blockingReason);
    layout->addWidget(buttonBox);

    connect(_normalRadio, &QRadioButton::toggled, this, [this] {
        _interactiveWidget->setVisible(interactiveMode());
        updateAcceptState();
    });
    connect(_interactiveRadio, &QRadioButton::toggled, this, [this] {
        _interactiveWidget->setVisible(interactiveMode());
        updateAcceptState();
    });
    connect(_interactiveWidget, &InteractiveRebaseWidget::sigValidationChanged,
            this, [this](bool, const QString&) { updateAcceptState(); });
    connect(_publishedConfirm, &QCheckBox::toggled,
            this, &RebaseDialog::updateAcceptState);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    updateAcceptState();
}

bool RebaseDialog::interactiveMode() const
{
    return _interactiveRadio->isChecked();
}

Git::RebasePlan RebaseDialog::rebasePlan() const
{
    Git::RebasePlan result = interactiveMode()
        ? _interactiveWidget->rebasePlan() : _plan;
    if (!interactiveMode()) {
        for (Git::RebasePlanItem& item : result.items) {
            item.action = Git::RebaseAction::Pick;
            item.rewrittenMessage.clear();
        }
    }
    return result;
}

void RebaseDialog::updateAcceptState()
{
    QString reason;
    if (_plan.preview.dirty) {
        reason = QStringLiteral(
            "Cannot start rebase while the index or working tree has changes.");
    } else if (_plan.preview.activeOperation != Git::RepositoryOperation::None) {
        reason = QStringLiteral(
            "Cannot start rebase while another repository operation is active.");
    } else if (_plan.items.isEmpty()) {
        reason = QStringLiteral("There are no commits to rebase.");
    } else if (interactiveMode() && !_interactiveWidget->isPlanValid()) {
        reason = _interactiveWidget->validationMessage();
    }

    const bool publishedAccepted = _plan.preview.publishedCount <= 0
        || _publishedConfirm->isChecked();
    _blockingReason->setText(reason);
    _blockingReason->setVisible(!reason.isEmpty());
    _startButton->setEnabled(reason.isEmpty() && publishedAccepted);
}
