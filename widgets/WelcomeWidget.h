#ifndef WELCOMEWIDGET_H
#define WELCOMEWIDGET_H

#include <QWidget>

class WelcomeWidget : public QWidget {
    Q_OBJECT
public:
    explicit WelcomeWidget(QWidget* parent = nullptr);
signals:
    void sigOpenRequested();
    void sigInitRequested();
    void sigCloneRequested();
};

#endif // WELCOMEWIDGET_H
