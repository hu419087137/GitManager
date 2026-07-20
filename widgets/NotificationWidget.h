#ifndef NOTIFICATIONWIDGET_H
#define NOTIFICATIONWIDGET_H

#include <QWidget>
class QLabel;
class QTimer;

class NotificationWidget : public QWidget {
    Q_OBJECT
public:
    enum class Level { Info, Success, Warning, Error };
    explicit NotificationWidget(QWidget* parent = nullptr);
    QString text() const;
public slots:
    void showMessage(const QString& message, Level level = Level::Info,
                     int timeoutMs = 5000);
    void dismiss();
private:
    QLabel* _label {nullptr};
    QTimer* _timer {nullptr};
};

#endif // NOTIFICATIONWIDGET_H
