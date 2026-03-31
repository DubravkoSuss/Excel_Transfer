#ifndef PERIODMODEL_H
#define PERIODMODEL_H

#include <QObject>
#include <QList>
#include <QString>

struct MonthEntry {
    QString name;
    bool selected = false;
};

struct YearEntry {
    int year = 0;
    bool selected = false;
    QList<MonthEntry> months;
};

class PeriodModel : public QObject
{
    Q_OBJECT

public:
    explicit PeriodModel(QObject* parent = nullptr);

    const QList<YearEntry>& years() const { return m_years; }

    void addYear(int year, const QStringList& months);
    void setYearSelected(int year, bool selected);
    void setMonthSelected(int year, const QString& month, bool selected);
    QList<YearEntry> generateSelectedPeriods() const;
    QList<YearEntry> selectedYears() const;
    void clear();
    void clearSelections();

signals:
    void dataChanged();

private:
    QList<YearEntry> m_years;
};

#endif // PERIODMODEL_H
