#include "periodmodel.h"
#include <QDebug>

PeriodModel::PeriodModel(QObject* parent)
    : QObject(parent)
{
}

void PeriodModel::addYear(int year, const QStringList& months)
{
    YearEntry entry;
    entry.year = year;
    for (const QString& month : months) {
        entry.months.append({month, false});
    }
    m_years.append(entry);
    emit dataChanged();
}

void PeriodModel::setYearSelected(int year, bool selected)
{
    for (YearEntry& entry : m_years) {
        if (entry.year == year) {
            entry.selected = selected;
            emit dataChanged();
            return;
        }
    }
    qWarning() << "Year not found in PeriodModel:" << year << "available" << m_years.size();
}

void PeriodModel::setMonthSelected(int year, const QString& month, bool selected)
{
    for (YearEntry& entry : m_years) {
        if (entry.year == year) {
            for (MonthEntry& m : entry.months) {
                if (m.name == month) {
                    m.selected = selected;
                    emit dataChanged();
                    return;
                }
            }
            qWarning() << "Month not found in PeriodModel:" << year << month;
            return;
        }
    }
    qWarning() << "Year not found in PeriodModel:" << year;
}

QList<YearEntry> PeriodModel::generateSelectedPeriods() const
{
    QList<YearEntry> result;
    for (const YearEntry& entry : m_years) {
        if (!entry.selected) {
            continue;
        }
        YearEntry filtered;
        filtered.year = entry.year;
        filtered.selected = true;
        for (const MonthEntry& month : entry.months) {
            if (month.selected) {
                filtered.months.append(month);
            }
        }
        if (!filtered.months.isEmpty()) {
            result.append(filtered);
        }
    }
    return result;
}

QList<YearEntry> PeriodModel::selectedYears() const
{
    QList<YearEntry> result;
    for (const YearEntry& entry : m_years) {
        if (entry.selected) {
            result.append(entry);
        }
    }
    return result;
}

void PeriodModel::clear()
{
    m_years.clear();
    emit dataChanged();
}

void PeriodModel::clearSelections()
{
    for (YearEntry& entry : m_years) {
        entry.selected = false;
        for (MonthEntry& month : entry.months) {
            month.selected = false;
        }
    }
    emit dataChanged();
}
