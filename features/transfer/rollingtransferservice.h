#pragma once

#include <QObject>
#include <QVector>
#include <QPair>
#include <QStringList>
#include "../../core/mappingsmanager.h"
#include "../../services/mappingservice.h"

class ExcelHandler;
class TransferService;

struct RollingStep {
    QString month;
    int     year = 0;
    QString monthNum;
    QString inputPath;   // previous month's xlsm to copy from
    QString outputPath;  // new month's xlsm path in test/ folder
    QVector<MappingEntry> selectedEntries; // mapping entries to transfer for this step
};

struct RollingResult {
    int totalMonths = 0;
    int successfulMonths = 0;
    int totalCells = 0;
    QStringList errors;
};

class RollingTransferService : public QObject
{
    Q_OBJECT
public:
    explicit RollingTransferService(ExcelHandler* handler,
                                    TransferService* transferService,
                                    MappingsManager* mappingsManager,
                                    MappingService* mappingService,
                                    QObject* parent = nullptr);

    QVector<RollingStep> buildChain(const QString& outputDir,
                                    const QVector<QPair<QString, int>>& periods);

    // allEntries: all mapping entries per month (from MappingService) — if empty, loads from JSON
    RollingResult executeChain(const QVector<RollingStep>& steps,
                               const QString& destFolder,
                               const QString& jsonBase,
                               const QMap<QString, QVector<MappingEntry>>& allEntriesByMonth = {});

signals:
    void stepCompleted(int stepIndex, bool success, int cellsTransferred);
    void chainProgress(int current, int total, const QString& message);
    void chainFinished(const RollingResult& result);

private:
    ExcelHandler*     m_handler;
    TransferService*  m_transferService;
    MappingsManager*  m_mappingsManager;
    MappingService*   m_mappingService;
};
