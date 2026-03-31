#include "periodcontroller.h"
#include <QDebug>

// ── Static definitions ─────────────────────────────────────────────────────

const QMap<int, QList<int>> PeriodController::QUARTER_INDICES = {
    {1, {0, 1,  2}},
    {2, {3, 4,  5}},
    {3, {6, 7,  8}},
    {4, {9, 10, 11}}
};

const QStringList PeriodController::MONTHS_LIST = {
    "January", "February", "March", "April",
    "May",     "June",     "July",  "August",
    "September","October", "November","December"
};

// ── Constructor ────────────────────────────────────────────────────────────

PeriodController::PeriodController(PeriodModel* model, QObject* parent)
    : QObject(parent)
    , m_model(model)
{
}

// ── Core slots ─────────────────────────────────────────────────────────────

void PeriodController::generatePeriodRows()
{
    if (!m_model) {
        qWarning() << "generatePeriodRows: model is null";
        return;
    }

    const QList<YearEntry> selected = m_model->selectedYears();
    emit clearUI();
    if (!selected.isEmpty()) {
        emit periodsReady(selected);
    } else {
        qWarning() << "No selected years in model";
    }
}

void PeriodController::onYearToggled(int year, bool checked)
{
    if (m_model) {
        m_model->setYearSelected(year, checked);
    }
}

void PeriodController::onMonthToggled(int year, const QString& month, bool checked)
{
    if ((m_loadingRTGuard && *m_loadingRTGuard) ||
        (m_loadingPeriodsGuard && *m_loadingPeriodsGuard)) {
        qDebug() << "onMonthToggled BLOCKED" << year << month;
        return;
    }
    qDebug() << "onMonthToggled" << year << month << checked;
    if (m_model) {
        m_model->setMonthSelected(year, month, checked);
    }
}

// ── Quarter helpers ───────────────────────────────────────────────────────

void PeriodController::applyQuarterToAll(int quarter, bool replace)
{
    if (!QUARTER_INDICES.contains(quarter) || !m_model) return;
    const QList<int>& indices = QUARTER_INDICES[quarter];

    // Snapshot to avoid aliasing while mutating the model
    const QList<YearEntry> snapshot = m_model->years();
    for (const YearEntry& entry : snapshot) {
        if (!entry.selected) continue;

        if (replace) {
            for (const MonthEntry& m : entry.months) {
                m_model->setMonthSelected(entry.year, m.name, false);
            }
        }
        for (int idx : indices) {
            if (idx < entry.months.size()) {
                m_model->setMonthSelected(entry.year, entry.months[idx].name, true);
            }
        }
    }

    refreshUI();
}

void PeriodController::removeQuarterFromAll(int quarter)
{
    if (!QUARTER_INDICES.contains(quarter) || !m_model) return;
    const QList<int>& indices = QUARTER_INDICES[quarter];

    const QList<YearEntry> snapshot = m_model->years();
    for (const YearEntry& entry : snapshot) {
        if (!entry.selected) continue;

        for (int idx : indices) {
            if (idx < entry.months.size()) {
                m_model->setMonthSelected(entry.year, entry.months[idx].name, false);
            }
        }
    }

    refreshUI();
}

// ── Private helpers ────────────────────────────────────────────────────────

void PeriodController::refreshUI()
{
    const QList<YearEntry> selected = m_model->selectedYears();
    emit clearUI();
    if (!selected.isEmpty()) {
        emit periodsReady(selected);
    }
}
