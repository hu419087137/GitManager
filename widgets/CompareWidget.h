#ifndef COMPAREWIDGET_H
#define COMPAREWIDGET_H

#include "../core/GitTypes.h"
#include <QWidget>

class QComboBox;
class QPushButton;

class CompareWidget : public QWidget {
    Q_OBJECT

public:
    explicit CompareWidget(QWidget* parent = nullptr);

    void setBranches(const QVector<Git::Branch>& branches);
    void setBaseRevision(const QString& revision);
    void setTargetRevision(const QString& revision);
    QString baseRevision() const;
    QString targetRevision() const;

signals:
    void sigCompareRequested(const QString& baseRevision,
                             const QString& targetRevision);

private:
    void updateButtonState();
    void addRevisionItem(QComboBox* combo, const QString& label,
                         const QString& revision, const QString& toolTip);
    void setRevision(QComboBox* combo, const QString& revision);

    QComboBox* _baseCombo {nullptr};
    QComboBox* _targetCombo {nullptr};
    QPushButton* _compareButton {nullptr};
};

#endif // COMPAREWIDGET_H
