#include "SettingsDialog.h"
#include "../core/AppSettings.h"
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Settings"));
    auto* label = new QLabel(QStringLiteral(
        "Window size, toolbar state, splitter positions, and active panel are saved automatically."), this);
    label->setWordWrap(true);
    auto* reset = new QPushButton(QStringLiteral("Reset Window Layout"), this);
    reset->setObjectName(QStringLiteral("resetWindowLayoutButton"));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(label);
    layout->addWidget(reset);
    layout->addWidget(buttons);
    connect(reset, &QPushButton::clicked, this, [] {
        Git::AppSettings::resetWindowLayout();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
