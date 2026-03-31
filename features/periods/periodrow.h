#ifndef PERIODROW_H
#define PERIODROW_H

#include <QFrame>
#include <QMap>
#include <QCheckBox>
#include <QVBoxLayout>
#include <QVector>
#include <QToolButton>
#include "quarterquickselector.h"

class PeriodRow : public QFrame
{
    Q_OBJECT

public:
    explicit PeriodRow(int year, QWidget* parent = nullptr);
    ~PeriodRow();

    int getYear() const { return m_year; }
    QStringList getSelectedMonths() const;
    bool isEmpty() const;
    QMap<QString, QCheckBox*> getMonthCheckboxes() const { return m_monthCheckboxes; }
    void setMonthChecked(const QString& month, bool checked);
    QVector<QCheckBox*> monthCheckboxesInOrder() const;

signals:
    void removed(PeriodRow* row);
    void monthsChanged();
    void monthSelected(int year, const QString& month, bool checked);

private slots:
    void onMonthCheckChanged(const QString& month, int state);

private:
    void buildUI();
    void buildQuickSelector(QVBoxLayout* layout);

    int m_year;
    QuarterQuickSelector* m_quickSelector = nullptr;
    QMap<QString, QCheckBox*> m_monthCheckboxes;
    QVBoxLayout* m_layout;
};

#endif // PERIODROW_H
