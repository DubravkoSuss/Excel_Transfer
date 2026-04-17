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
    void runCumulativePassExecuteAll(const QSet<int>& allRows,
                                     const QString& destSheet,
                                     int year,
                                     const QString& destKey,
                                     const QString& targetMonth);

    // Cumulative pass for Fill All only — recomputes all months from scratch.
    void runCumulativePassAllMonths(const QSet<int>& allRows,
                                    const QString& destSheet,
                                    int year,
                                    const QString& destKey,
                                    const QString& targetMonth);

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