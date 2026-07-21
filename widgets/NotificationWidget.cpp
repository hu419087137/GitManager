#include "NotificationWidget.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

NotificationWidget::NotificationWidget(QWidget* parent)
    : QWidget(parent), _label(new QLabel(this)), _timer(new QTimer(this))
{
    setObjectName(QStringLiteral("notificationWidget"));
    setAccessibleName(QStringLiteral("Application notification"));
    setVisible(false);
    _label->setWordWrap(true);
    auto* close = new QPushButton(QStringLiteral("x"), this);
    close->setObjectName(QStringLiteral("notificationCloseButton"));
    close->setAccessibleName(QStringLiteral("Dismiss notification"));
    close->setFixedSize(28, 28);
    close->setToolTip(QStringLiteral("Dismiss notification"));
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 6, 6, 6);
    layout->addWidget(_label, 1);
    layout->addWidget(close);
    _timer->setSingleShot(true);
    connect(_timer, &QTimer::timeout, this, &NotificationWidget::dismiss);
    connect(close, &QPushButton::clicked, this, &NotificationWidget::dismiss);
}

QString NotificationWidget::text() const { return _label->text(); }

void NotificationWidget::showMessage(const QString& message, Level level,
                                     int timeoutMs)
{
    static const char* colors[] = {"#eaf2ff", "#e8f7ee", "#fff5d6", "#fdebec"};
    static const char* borders[] = {"#6b9ee8", "#4ca86b", "#d49b20", "#cf4b56"};
    const int index = static_cast<int>(level);
    _label->setText(message);
    setStyleSheet(QStringLiteral("NotificationWidget { background: %1; border-bottom: 1px solid %2; }")
                      .arg(QString::fromLatin1(colors[index]),
                           QString::fromLatin1(borders[index])));
    setVisible(!message.isEmpty());
    _timer->stop();
    if (timeoutMs > 0) _timer->start(timeoutMs);
}

void NotificationWidget::dismiss()
{
    _timer->stop();
    setVisible(false);
}
