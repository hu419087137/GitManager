#ifndef RESETDIALOG_H
#define RESETDIALOG_H

#include "../core/GitTypes.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QPushButton;
class QRadioButton;

class ResetDialog : public QDialog {
    Q_OBJECT

public:
    explicit ResetDialog(const Git::HistoryRewritePreview& preview,
                         QWidget* parent = nullptr);

    Git::ResetMode resetMode() const;

private:
    void updateModeDescription();
    void updateAcceptState();

    Git::HistoryRewritePreview _preview;
    QRadioButton* _softRadio {nullptr};
    QRadioButton* _mixedRadio {nullptr};
    QRadioButton* _hardRadio {nullptr};
    QLabel* _modeDescription {nullptr};
    QLabel* _hardWarning {nullptr};
    QLabel* _publishedWarning {nullptr};
    QLabel* _blockingReason {nullptr};
    QCheckBox* _hardConfirm {nullptr};
    QCheckBox* _publishedConfirm {nullptr};
    QPushButton* _resetButton {nullptr};
};

#endif // RESETDIALOG_H
