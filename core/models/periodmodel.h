#ifndef PERIODMODEL_H
#define PERIODMODEL_H

#include <QObject>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>

// ── Data structures ───────────────────────────────────────────────────────

struct YearEntry {
    int year;
    QStringList selectedMonths;

    bool isEmpty() const { return selectedMonths.isEmpty(); }
};

// ──────────────────────────────────────────────────────────────────────────

class PeriodModel : public QObject
{
    Q_OBJECT

public:
    explicit PeriodModel(QObject* parent = nullptr);

    // ── Selection state ───────────────────────────────────────────────────
    void setYearSelected(int year, bool selected);
    void setMonthSelected(int year, const QString& month, bool selected);
    bool isYearSelected(int year) const;
    bool isMonthSelected(int year, const QString& month) const;

    QSet<int> selectedYears() const { return m_selectedYears; }
    QStringList selectedMonthsForYear(int year) const;

    // ── Generation ────────────────────────────────────────────────────────
    QList<YearEntry> generateSelectedPeriods() const;

    // ── Batch operations ──────────────────────────────────────────────────
    void clearAllSelections();
    void applyQuarterToYear(int year, int quarter, bool replace = true);
    void removeQuarterFromYear(int year, int quarter);

signals:
    void selectionChanged();

private:
    QSet<int> m_selectedYears;
    QMap<QPair<int, QString>, bool> m_monthSelections;  // (year, month) -> selected

    static const QMap<int, QList<int>> QUARTER_INDICES;
    static const QStringList MONTHS_LIST;
};

#endif // PERIODMODEL_H
