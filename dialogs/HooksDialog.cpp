#include "HooksDialog.h"
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

HooksDialog::HooksDialog(const QVector<Git::HookInfo>& hooks,
                         const QString& error, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Repository Hooks"));
    resize(720, 400);
    auto* message = new QLabel(error.isEmpty()
        ? QStringLiteral("Hooks are executed through 'git hook run'. Commit integrates pre-commit and post-commit.")
        : error, this);
    message->setWordWrap(true);
    auto* table = new QTableWidget(hooks.size(), 3, this);
    table->setObjectName(QStringLiteral("hooksTable"));
    table->setHorizontalHeaderLabels({QStringLiteral("Hook"), QStringLiteral("Runnable"),
                                      QStringLiteral("Path")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    for (int row = 0; row < hooks.size(); ++row) {
        const auto& hook = hooks.at(row);
        table->setItem(row, 0, new QTableWidgetItem(hook.name));
        table->setItem(row, 1, new QTableWidgetItem(
            hook.executable ? QStringLiteral("Yes") : QStringLiteral("No")));
        table->setItem(row, 2, new QTableWidgetItem(hook.path));
    }
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(message);
    layout->addWidget(table, 1);
    layout->addWidget(close);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
