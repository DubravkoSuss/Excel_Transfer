#pragma once
#include <QObject>
#include <QThread>
#include <QString>
#include <QVector>
#include "fillallmodels.h"
#include "../../services/transferservice.h"
#include "../../services/excelhandler.h"
#include "../../core/mappingsmanager.h"

class FillAllWorker : public QObject
{
    Q_OBJECT

public:
    explicit FillAllWorker(ExcelHandler*    handler,
                           MappingsManager* mappings,
                           TransferService* transferService,
                           const FillAllScanResult& scanResult,
                           QObject* parent = nullptr)
        : QObject(parent)
        , m_handler(handler)
        , m_mappings(mappings)
        , m_transfer(transferService)
        , m_scan(scanResult)
    {}

signals:
    void progress(int current, int total, const QString& message);
    void finished(FillAllResult result);

public slots:
    void run();

private:
    ExcelHandler*     m_handler;
    MappingsManager*  m_mappings;
    TransferService*  m_transfer;
    FillAllScanResult m_scan;

    // Helpers
    QString buildSourceKey(const QString& monthName, int year,
                           const QString& transferType) const;

    QVector<MappingEntry> getMappingsForType(const QString& monthName, int year,
                                             const QString& transferType) const;

    void computeTrafficQ(const QString& destKey, int targetMonth);

    // Execution order weight: lower = runs first
    static int typeOrder(const QString& type) {
        if (type == "traffic_mott")  return 0;
        if (type == "pax_transfer")  return 1;
        if (type == "staff")         return 2;
        if (type == "sap")           return 3;
        if (type == "budget_refi")   return 4;
        if (type == "sap_ytd")       return 5;
        return 3;
    }
};
