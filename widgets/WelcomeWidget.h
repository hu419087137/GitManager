#ifndef WELCOMEWIDGET_H
#define WELCOMEWIDGET_H

#include <QWidget>
#include <QStringList>

class QVBoxLayout;

class WelcomeWidget : public QWidget {
    Q_OBJECT
public:
    explicit WelcomeWidget(QWidget* parent = nullptr);
    void setRecentRepositories(const QStringList& paths);
signals:
    void sigOpenRequested();
    void sigInitRequested();
    void sigCloneRequested();
    void sigRecentRepositoryRequested(const QString& path);

private:
    QVBoxLayout* _recentLayout {nullptr};
};

#endif // WELCOMEWIDGET_H
