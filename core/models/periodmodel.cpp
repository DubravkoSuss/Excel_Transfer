#include "periodmodel.h"

const QStringList PeriodModel::MONTHS_LIST = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

const QMap<int, QList<int>> PeriodModel::QUARTER_INDICES = {
    {1, {0, 1,  2}},   // Q1: Jan, Feb, Mar
    {2, {3, 4,  5}},   // Q2: Apr, May, Jun
    {3, {6, 7,  8}},   // Q3: Jul, Aug, Sep
    {4, {9, 10, 11}}   // Q4: Oct, Nov, Dec
};

PeriodModel::PeriodModel(QObject* parent)
    : QObject(parent)
{
}

// ── Selection state ───────────────────────────────────────────────────────

void PeriodModel::setYearSelected(int year, bool selected)
{
    if (selected) {
        m_selectedYears.insert(year);
    } else {
        m_selectedYears.remove(year);
        // Also clear all month selections for this year
        for (const QString& month : MONTHS_LIST) {
            m_monthSelections.remove(qMakePair(year, month));
        }
    }
    emit selectionChanged();
}

void PeriodModel::setMonthSelected(int year, const QString& month, bool selected)
{
    QPair<int, QString> key(year, month);
    if (selected) {
        m_monthSelections[key] = true;
    } else {
        m_monthSelections.remove(key);
    }
    emit selectionChanged();
}

bool PeriodModel::isYearSelected(int year) const
{
    return m_selectedYears.contains(year);
}

bool PeriodModel::isMonthSelected(int year, const QString& month) const
{
    return m_monthSelections.value(qMakePair(year, month), false);
}

QStringList PeriodModel::selectedMonthsForYear(int year) const
{
    QStringList months;
    for (const QString& month : MONTHS_LIST) {
        if (isMonthSelected(year, month)) {
            months.append(month);
        }
    }
    return months;
}

// ── Generation ────────────────────────────────────────────────────────────

QList<YearEntry> PeriodModel::generateSelectedPeriods() const
{
    QList<YearEntry> result;

    // Sort years for consistent output
    QList<int> years = m_selectedYears.values();
    std::sort(years.begin(), years.end());

    for (int year : years) {
        YearEntry entry;
        entry.year = year;
        entry.selectedMonths = selectedMonthsForYear(year);

        if (!entry.isEmpty()) {
            result.append(entry);
        }
    }

    return result;
}

// ── Batch operations ──────────────────────────────────────────────────────

void PeriodModel::clearAllSelections()
{
    m_selectedYears.clear();
    m_monthSelections.clear();
    emit selectionChanged();
}

void PeriodModel::applyQuarterToYear(int year, int quarter, bool replace)
{
    if (!QUARTER_INDICES.contains(quarter)) return;

    const QList<int>& indices = QUARTER_INDICES[quarter];

    // If replace mode, clear all months first
    if (replace) {
        for (const QString& month : MONTHS_LIST) {
            m_monthSelections.remove(qMakePair(year, month));
        }
    }

    // Set the quarter months
    for (int idx : indices) {
        if (idx >= 0 && idx < MONTHS_LIST.size()) {
            setMonthSelected(year, MONTHS_LIST[idx], true);
        }
    }
}

void PeriodModel::removeQuarterFromYear(int year, int quarter)
{
    if (!QUARTER_INDICES.contains(quarter)) return;

    const QList<int>& indices = QUARTER_INDICES[quarter];

    for (int idx : indices) {
        if (idx >= 0 && idx < MONTHS_LIST.size()) {
            m_monthSelections.remove(qMakePair(year, MONTHS_LIST[idx]));
        }
    }

    emit selectionChanged();
}
