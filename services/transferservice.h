#ifndef TRANSFERSERVICE_H
#define TRANSFERSERVICE_H

#include <QObject>
#include <QMap>
#include <QSet>
#include <QString>
#include <QVector>
#include "../core/mappingsmanager.h"

class ExcelHandler;

class TransferService : public QObject {
    Q_OBJECT
public:
    struct Result {
        int cellsTransferred = 0;
        bool usedComCopy = false;
        QString error;
    };

    explicit TransferService(ExcelHandler* handler, QObject* parent = nullptr);

    ExcelHandler* handler() const { return m_handler; }

    Result transferEntry(const MappingEntry& entry,
                         int year,
                         const QString& destKey,
                         const QString& destFilePath,
                         const QString& baseFolder,
                         bool skipCumulative = false);

    // Cumulative pass for Execute All — computes IP through (targetMonth - 1).
    // Does NOT touch the target month's cumulative column.
    // monthDestKeys: maps month name → destKey for all months so prevCum can be
    // read from the correct prior-month file (not from the current month's file
    // which may have been copied from a different month and therefore has 0 in
    // any cumulative column that was never written by a prior Execute All run).
    // clearFirst=true: zero rows 5-228 of the cumulative column before running
    // the two passes (used for the second "clean" run after the first save).
    void runCumulativePassExecuteAll(const QSet<int>& allRows,
                                     const QString& destSheet,
                                     int year,
                                     const QString& destKey,
                                     const QString& targetMonth,
                                     const QMap<QString, QString>& monthDestKeys = {},
                                     bool clearFirst = false);

    // Cumulative pass for Fill All only — recomputes all months from scratch.
    void runCumulativePassAllMonths(const QSet<int>& allRows,
                                    const QString& destSheet,
                                    int year,
                                    const QString& destKey,
                                    const QString& targetMonth);

    // Cumulative pass for Execute RT — exact same logic as runCumulativePassExecuteAll
    // but takes int month (1=Jan … 12=Dec). Completely decoupled symbol so RT-specific
    // tweaks can never break Execute All.
    void runCumulativePassRT(const QSet<int>& destRows,
                             const QString& sheetName,
                             int year,
                             const QString& destKey,
                             int month);

private:
    void loadSubtotals();
    
    Result handleSapYtd(const MappingEntry& entry,
                        int year,
                        const QString& destKey,
                        const QString& destFilePath,
                        const QString& baseFolder);

    ExcelHandler* m_handler;
    QMap<int, QString> m_subtotalsMap;
};

#endif // TRANSFERSERVICE_H