#ifndef PERIODCONTROLLER_H
#define PERIODCONTROLLER_H

#include <QObject>
#include <QList>
#include <QMap>
#include <QStringList>
#include "periodmodel.h"

class PeriodController : public QObject
{
    Q_OBJECT

public:
    explicit PeriodController(PeriodModel* model, QObject* parent = nullptr);

public:
    void setLoadingGuards(bool* rtGuard, bool* periodsGuard) {
        m_loadingRTGuard = rtGuard;
        m_loadingPeriodsGuard = periodsGuard;
    }

public slots:
    // ── Core slots ────────────────────────────────────────────────────────
    void generatePeriodRows();
    void onYearToggled(int year, bool checked);
    void onMonthToggled(int year, const QString& month, bool checked);

    // ── Quarter helpers ───────────────────────────────────────────────────
    // replace=true  → clear all months first, then set quarter months
    // replace=false → additive; only sets the quarter months
    void applyQuarterToAll(int quarter, bool replace = true);
    void removeQuarterFromAll(int quarter);

signals:
    void clearUI();
    void periodsReady(const QList<YearEntry>& periods);

private:
    void refreshUI();

    PeriodModel* m_model = nullptr;
    bool* m_loadingRTGuard = nullptr;
    bool* m_loadingPeriodsGuard = nullptr;

    static const QMap<int, QList<int>> QUARTER_INDICES;
    static const QStringList           MONTHS_LIST;
};

#endif // PERIODCONTROLLER_H
