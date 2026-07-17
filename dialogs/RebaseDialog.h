#ifndef REBASEDIALOG_H
#define REBASEDIALOG_H

#include "../core/GitTypes.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QPushButton;
class QRadioButton;
class InteractiveRebaseWidget;

class RebaseDialog : public QDialog {
    Q_OBJECT

public:
    explicit RebaseDialog(const Git::RebasePlan& plan,
                          QWidget* parent = nullptr);

    bool interactiveMode() const;
    Git::RebasePlan rebasePlan() const;

private:
    void updateAcceptState();

    Git::RebasePlan _plan;
    QRadioButton* _normalRadio {nullptr};
    QRadioButton* _interactiveRadio {nullptr};
    InteractiveRebaseWidget* _interactiveWidget {nullptr};
    QLabel* _publishedWarning {nullptr};
    QCheckBox* _publishedConfirm {nullptr};
    QLabel* _blockingReason {nullptr};
    QPushButton* _startButton {nullptr};
};

#endif // REBASEDIALOG_H
