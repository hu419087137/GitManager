#include "HostingReviewDialog.h"
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVBoxLayout>

namespace {
class DiffHighlighter final : public QSyntaxHighlighter {
public:
    explicit DiffHighlighter(QTextDocument* document)
        : QSyntaxHighlighter(document) {}
protected:
    void highlightBlock(const QString& text) override
    {
        QTextCharFormat format;
        const bool dark = QGuiApplication::palette()
                              .color(QPalette::Base).lightness() < 128;
        if (text.startsWith(QStringLiteral("@@"))) {
            format.setForeground(QColor(dark ? QStringLiteral("#58a6ff")
                                              : QStringLiteral("#0969da")));
            format.setFontWeight(QFont::DemiBold);
        } else if (text.startsWith(QLatin1Char('+'))
                   && !text.startsWith(QStringLiteral("+++"))) {
            format.setForeground(QColor(dark ? QStringLiteral("#7ee787")
                                              : QStringLiteral("#116329")));
            format.setBackground(QColor(dark ? QStringLiteral("#1b4721")
                                              : QStringLiteral("#dafbe1")));
        } else if (text.startsWith(QLatin1Char('-'))
                   && !text.startsWith(QStringLiteral("---"))) {
            format.setForeground(QColor(dark ? QStringLiteral("#ff7b72")
                                              : QStringLiteral("#82071e")));
            format.setBackground(QColor(dark ? QStringLiteral("#4c1f24")
                                              : QStringLiteral("#ffebe9")));
        } else {
            return;
        }
        setFormat(0, text.size(), format);
    }
};
}

HostingReviewDialog::HostingReviewDialog(
    const Git::HostingRemoteInfo& remote,
    const Git::HostingChangeInfo& change,
    const QVector<Git::HostingReviewFile>& files,
    const QString& error, QWidget* parent)
    : QDialog(parent), _files(files), _remote(remote), _change(change)
{
    setWindowTitle(QStringLiteral("Review #%1 — %2").arg(change.id, change.title));
    resize(980, 620);
    _list = new QListWidget(this);
    _list->setObjectName(QStringLiteral("hostingReviewFileList"));
    for (int index = 0; index < _files.size(); ++index) {
        const auto& file = _files.at(index);
        auto* item = new QListWidgetItem(
            QStringLiteral("%1  [%2]").arg(file.path, file.status), _list);
        item->setData(Qt::UserRole, index);
        item->setToolTip(file.previousPath);
    }
    _patch = new QPlainTextEdit(this);
    _patch->setObjectName(QStringLiteral("hostingReviewPatch"));
    _patch->setReadOnly(true);
    _patch->setLineWrapMode(QPlainTextEdit::NoWrap);
    _patch->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    new DiffHighlighter(_patch->document());
    _openButton = new QPushButton(QStringLiteral("Open File in Provider"), this);
    _openButton->setObjectName(QStringLiteral("hostingReviewOpenFileButton"));
    _lineEdit = new QLineEdit(this);
    _lineEdit->setObjectName(QStringLiteral("hostingReviewLineEdit"));
    _lineEdit->setPlaceholderText(QStringLiteral("New-side line number"));
    _lineEdit->setValidator(new QIntValidator(1, 100000000, _lineEdit));
    _commentEdit = new QPlainTextEdit(this);
    _commentEdit->setObjectName(QStringLiteral("hostingReviewCommentEdit"));
    _commentEdit->setPlaceholderText(QStringLiteral("Review comment"));
    _commentEdit->setMaximumHeight(100);
    _commentButton = new QPushButton(QStringLiteral("Submit Line Comment"), this);
    _commentButton->setObjectName(QStringLiteral("hostingReviewCommentButton"));
    auto* splitter = new QSplitter(this);
    splitter->addWidget(_list);
    splitter->addWidget(_patch);
    splitter->setStretchFactor(1, 1);
    auto* message = new QLabel(error, this);
    message->setWordWrap(true);
    message->setVisible(!error.isEmpty());
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(message);
    layout->addWidget(splitter, 1);
    layout->addWidget(_openButton);
    auto* commentRow = new QHBoxLayout;
    commentRow->addWidget(_lineEdit);
    commentRow->addWidget(_commentButton);
    layout->addWidget(_commentEdit);
    layout->addLayout(commentRow);
    layout->addWidget(close);
    connect(_list, &QListWidget::currentRowChanged, this, &HostingReviewDialog::updateFile);
    connect(_openButton, &QPushButton::clicked, this, &HostingReviewDialog::slotOpenFile);
    connect(_commentButton, &QPushButton::clicked, this, &HostingReviewDialog::slotSubmitComment);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    if (!_files.isEmpty()) _list->setCurrentRow(0);
    else updateFile();
}
bool HostingReviewDialog::patchContainsNewLine(const QString& patch, int target)
{
    int newLine = 0;
    const QRegularExpression hunk(
        QStringLiteral(R"(^@@ -\d+(?:,\d+)? \+(\d+)(?:,\d+)? @@)"));
    for (const QString& line : patch.split(QLatin1Char('\n'))) {
        const auto match = hunk.match(line);
        if (match.hasMatch()) {
            newLine = match.captured(1).toInt();
            continue;
        }
        if (newLine <= 0 || line.startsWith(QStringLiteral("---"))
            || line.startsWith(QStringLiteral("+++"))) continue;
        if (line.startsWith(QLatin1Char('-'))) continue;
        if (newLine == target) return true;
        if (!line.startsWith(QLatin1Char('\\'))) ++newLine;
    }
    return false;
}
void HostingReviewDialog::updateFile()
{
    const auto* item = _list->currentItem();
    const int index = item ? item->data(Qt::UserRole).toInt() : -1;
    if (index < 0 || index >= _files.size()) {
        _patch->clear();
        _openButton->setEnabled(false);
        return;
    }
    const auto& file = _files.at(index);
    _patch->setPlainText(file.patch.isEmpty()
        ? QStringLiteral("Patch text is not supplied by this provider for this file.")
        : file.patch);
    _openButton->setEnabled(!file.webUrl.isEmpty());
}
void HostingReviewDialog::slotOpenFile()
{
    const auto* item = _list->currentItem();
    if (!item) return;
    const int index = item->data(Qt::UserRole).toInt();
    if (index >= 0 && index < _files.size() && !_files.at(index).webUrl.isEmpty())
        emit sigOpenFileRequested(_files.at(index).webUrl);
}
void HostingReviewDialog::slotSubmitComment()
{
    const auto* item = _list->currentItem();
    if (!item) return;
    const int index = item->data(Qt::UserRole).toInt();
    const int line = _lineEdit->text().toInt();
    const QString body = _commentEdit->toPlainText().trimmed();
    if (index < 0 || index >= _files.size() || line <= 0 || body.isEmpty())
        return;
    const auto& file = _files.at(index);
    if (!file.patch.isEmpty() && !patchContainsNewLine(file.patch, line)) {
        QMessageBox::warning(this, QStringLiteral("Line Comment"),
            QStringLiteral("Select a new-side added or context line present in this patch."));
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("Submit Review Comment"),
            QStringLiteral("Submit this comment to line %1 of %2?").arg(line).arg(file.path),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel) != QMessageBox::Yes) return;
    emit sigCommentRequested(_remote, _change, file, line, body);
}
