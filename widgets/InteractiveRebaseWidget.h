#ifndef INTERACTIVEREBASEWIDGET_H
#define INTERACTIVEREBASEWIDGET_H

#include "../core/GitTypes.h"

#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QTableWidget;

class InteractiveRebaseWidget : public QWidget {
    Q_OBJECT

public:
    explicit InteractiveRebaseWidget(QWidget* parent = nullptr);
    explicit InteractiveRebaseWidget(const Git::RebasePlan& plan,
                                     QWidget* parent = nullptr);

    void setRebasePlan(const Git::RebasePlan& plan);
    Git::RebasePlan rebasePlan() const;
    bool isPlanValid() const { return _planValid; }
    QString validationMessage() const { return _validationMessage; }

signals:
    void sigPlanChanged();
    void sigValidationChanged(bool valid, const QString& message);

private:
    void rebuildTable();
    void loadMessageEditor(int row);
    void updateRow(int row);
    void updateValidation();
    QString effectiveMessage(const Git::RebasePlanItem& item) const;

    Git::RebasePlan _plan;
    QTableWidget* _table {nullptr};
    QLabel* _messageLabel {nullptr};
    QPlainTextEdit* _messageEdit {nullptr};
    QLabel* _validationLabel {nullptr};
    int _currentRow {-1};
    bool _updating {false};
    bool _planValid {true};
    QString _validationMessage;
};

#endif // INTERACTIVEREBASEWIDGET_H
