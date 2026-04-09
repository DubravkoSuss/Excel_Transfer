#include "rollingtransferservice.h"
#include <QtConcurrent>
#include <QThreadPool>
#include <QFuture>
#include "../../services/excelhandler.h"
#include "../../services/transferservice.h"
#include "../../services/mappingservice.h"
#include "../../core/crashguard.h"
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <algorithm>

static const QStringList MONTHS = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

static const QMap<QString, QString> MONTH_TO_NUM = {
    {"January","01"}, {"February","02"}, {"March","03"}, {"April","04"},
    {"May","05"}, {"June","06"}, {"July","07"}, {"August","08"},
    {"September","09"}, {"October","10"}, {"November","11"}, {"December","12"}
};

RollingTransferService::RollingTransferService(ExcelHandler* handler,
                                               TransferService* transferService,
                                               MappingsManager* mappingsManager,
                                               MappingService* mappingService,
                                               QObject* parent)
    : QObject(parent)
    , m_handler(handler)
    , m_transferService(transferService)
    , m_mappingsManager(mappingsManager)
    , m_mappingService(mappingService)
{
}

QVector<RollingStep> RollingTransferService::buildChain(const QString& outputDir,
                                                        const QVector<QPair<QString, int>>& periods)
{
    QVector<QPair<QString, int>> sorted = periods;
    std::sort(sorted.begin(), sorted.end(), [](const QPair<QString,int>& a, const QPair<QString,int>& b) {
        if (a.second != b.second) return a.second < b.second;
        return MONTHS.indexOf(a.first) < MONTHS.indexOf(b.first);
    });

    QVector<RollingStep> chain;
    chain.reserve(sorted.size());

    for (int i = 0; i < sorted.size(); ++i) {
        const QString& month = sorted[i].first;
        int year = sorted[i].second;
        QString monthNum = MONTH_TO_NUM.value(month, "01");

        RollingStep step;
        step.month = month;
        step.year = year;
        step.monthNum = monthNum;

        // Output always goes to test/ subfolder with standard name
        step.outputPath = QString("%1/%2/%3/test/Cost Control ZAG %3_%2_working.xlsm")
                              .arg(outputDir).arg(year).arg(monthNum);

        if (i == 0) {
            // First month: find the previous month's actual file on disk
            int prevIndex = MONTHS.indexOf(month) - 1;
            int prevYear = year;
            if (prevIndex < 0) {
                prevIndex = 11; // December of previous year
                prevYear = year - 1;
            }
            QString prevMonth = MONTHS[prevIndex];
            step.inputPath = m_handler ? m_handler->findCostControlFile(outputDir, prevMonth, prevYear) : QString();
        } else {
            // Chain: use the output of the previous step as input
            step.inputPath = chain[i - 1].outputPath;
        }

        chain.append(step);
    }

    return chain;
}

RollingResult RollingTransferService::executeChain(const QVector<RollingStep>& steps,
                                                   const QString& destFolder,
                                                   const QString& jsonBase,
                                                   const QMap<QString, QVector<MappingEntry>>& allEntriesByMonth)
{
    RollingResult result;
    result.totalMonths = steps.size();

    try {
        if (!m_handler || !m_transferService || !m_mappingsManager) {
            result.errors.append("Rolling transfer dependencies are not initialized.");
            emit chainFinished(result);
            return result;
        }
        if (steps.isEmpty()) {
            emit chainFinished(result);
            return result;
        }

        // Pre-load JSON mappings
        const QString mappingsOldPath = QString("%1/mappings_old.json").arg(jsonBase);
        const QString mappingsNewPath = QString("%1/mappings.json").arg(jsonBase);
        const QString sapYtdPath      = QString("%1/mappings_sap_ytd.json").arg(jsonBase);
        const QString paxPath         = QString("%1/pax.json").arg(jsonBase);
        const QString staffPath       = QString("%1/staff.json").arg(jsonBase);
        const QString budgetRefiPath  = QString("%1/mappings_budget_refi_prev_year.json").arg(jsonBase);
        const QString paxTransferPath = QString("%1/mappings_pax_transfer.json").arg(jsonBase);
        const QString trafficMottPath = QString("%1/Traffic_mott.json").arg(jsonBase);

        m_mappingsManager->loadMappings(mappingsOldPath);
        m_mappingsManager->loadMappings(mappingsNewPath);
        m_mappingsManager->loadSapYtdMappings(sapYtdPath);
        m_mappingsManager->loadPaxMappings(paxPath);
        m_mappingsManager->loadStaffMappings(staffPath);
        m_mappingsManager->loadBudgetRefiMappings(budgetRefiPath);
        m_mappingsManager->loadPaxTransferMappings(paxTransferPath);
        m_mappingsManager->loadTrafficMottMappings(trafficMottPath);

        for (int i = 0; i < steps.size(); ++i) {
            const RollingStep& step = steps[i];

            emit chainProgress(i, steps.size(),
                               QString("Rolling %1 %2...").arg(step.month).arg(step.year));

            qDebug() << "Rolling step" << (i + 1) << "/" << steps.size()
                     << step.month << step.year
                     << "\n  src:" << step.inputPath
                     << "\n  dst:" << step.outputPath;

            // Step 1: Copy previous month's file to new month's test/ folder
            QDir().mkpath(QFileInfo(step.outputPath).absolutePath());
            if (QFile::exists(step.outputPath)) {
                qDebug() << "Rolling: destination exists, overwriting";
                QFile::remove(step.outputPath);
            }
            if (step.inputPath.isEmpty() || !QFile::exists(step.inputPath)) {
                const QString err = QString("%1 %2: Source workbook missing").arg(step.month).arg(step.year);
                qWarning() << err << step.inputPath;
                result.errors.append(err);
                emit stepCompleted(i, false, 0);
                continue;
            }
            if (!QFile::copy(step.inputPath, step.outputPath)) {
                const QString err = QString("%1 %2: Failed to copy file").arg(step.month).arg(step.year);
                qWarning() << err << step.inputPath << "->" << step.outputPath;
                result.errors.append(err);
                emit stepCompleted(i, false, 0);
                continue;
            }
            qDebug() << "Rolling: file copied OK";

            // Step 2: Load dest workbook + source files in parallel
            QString destKey = QString("%1_%2_cost_control").arg(step.month).arg(step.year);
            QString srcKey  = QString("%1_%2_cost").arg(step.month).arg(step.year);
            m_handler->unloadWorkbook(destKey);

            QThreadPool pool;
            pool.setMaxThreadCount(2);

            QString sapPath2, paxPath2, staffPath2;
            const QString monthKey = QString("%1_%2").arg(step.month).arg(step.year);
            if (allEntriesByMonth.contains(monthKey)) {
                for (const auto& entry : allEntriesByMonth[monthKey]) {
                    if (entry.sourceFileType == "sap" && !entry.sourcePath.isEmpty()) sapPath2 = entry.sourcePath;
                    else if (entry.sourceFileType == "pax" && !entry.sourcePath.isEmpty()) paxPath2 = entry.sourcePath;
                    else if (entry.sourceFileType == "staff" && !entry.sourcePath.isEmpty()) staffPath2 = entry.sourcePath;
                }
            }
            if (sapPath2.isEmpty()) sapPath2 = m_handler->findSAPFile(destFolder, step.month, step.year);
            if (paxPath2.isEmpty()) paxPath2 = m_handler->findPaxFile(destFolder, step.month, step.year);
            if (staffPath2.isEmpty()) staffPath2 = m_handler->findStaffFile(destFolder, step.year);

            QVector<QFuture<bool>> futures;

            // Always load dest workbook
            futures.append(QtConcurrent::run(&pool, [this, outputPath = step.outputPath, destKey]() {
                return m_handler->loadWorkbook(outputPath, destKey, {});
            }));

            // Load source files only if not already cached
            if (!sapPath2.isEmpty()) {
                QString sapKey = QString("%1_%2_sap").arg(step.month).arg(step.year);
                if (!m_handler->isLoaded(sapKey))
                    futures.append(QtConcurrent::run(&pool, [this, sapPath2, sapKey]() {
                        return m_handler->loadWorkbook(sapPath2, sapKey, {});
                    }));
            }
            if (!paxPath2.isEmpty()) {
                QString paxKey = QString("%1_%2_pax").arg(step.month).arg(step.year);
                if (!m_handler->isLoaded(paxKey))
                    futures.append(QtConcurrent::run(&pool, [this, paxPath2, paxKey]() {
                        return m_handler->loadWorkbook(paxPath2, paxKey, {});
                    }));
            }
            if (!staffPath2.isEmpty()) {
                QString staffKey = QString("%1_%2_staff").arg(step.month).arg(step.year);
                if (!m_handler->isLoaded(staffKey))
                    futures.append(QtConcurrent::run(&pool, [this, staffPath2, staffKey]() {
                        return m_handler->loadWorkbook(staffPath2, staffKey, {});
                    }));
            }

            if (futures.isEmpty()) {
                const QString err = QString("%1 %2: Internal load queue is empty").arg(step.month).arg(step.year);
                qWarning() << err;
                result.errors.append(err);
                emit stepCompleted(i, false, 0);
                continue;
            }

            bool destLoaded = futures[0].result();
            for (int fi = 1; fi < futures.size(); ++fi) futures[fi].result();

            if (!destLoaded) {
                const QString err = QString("%1 %2: Failed to load copied workbook").arg(step.month).arg(step.year);
                qWarning() << err;
                result.errors.append(err);
                emit stepCompleted(i, false, 0);
                continue;
            }
            qDebug() << "Rolling: workbook loaded as" << destKey;

            // Step 3: Also load source workbook (_cost key) for same-file SAP transfers
            if (!m_handler->isLoaded(srcKey)) {
                m_handler->loadWorkbook(step.outputPath, srcKey, {});
            }

            // Step 4: Transfer selected mapping entries (or all if none specified)
            int monthCells = 0;
            QVector<MappingEntry> entries;
            if (!step.selectedEntries.isEmpty()) {
                entries = step.selectedEntries;
            } else if (allEntriesByMonth.contains(monthKey)) {
                entries = allEntriesByMonth[monthKey];
            } else if (m_mappingService && m_mappingsManager) {
                const bool useOldMappings = step.year <= 2025;
                m_mappingsManager->loadMappings(useOldMappings ? mappingsOldPath : mappingsNewPath);

                entries = m_mappingsManager->getMappingsForMonthYear(step.month, step.year);
                for (MappingEntry entry : m_mappingsManager->getDynamicMappingsForMonthYear(step.month, step.year))
                    entries.append(entry);
                for (const MappingEntry& e : m_mappingsManager->getTrafficMottMappingsForMonthYear(step.month, step.year))
                    entries.append(e);
                for (const MappingEntry& e : m_mappingsManager->getPaxTransferMappingsForMonthYear(step.month, step.year))
                    entries.append(e);
                for (const auto& sel : m_mappingService->collectPaxMappings({{step.month, step.year}}))
                    entries.append(sel.entry);
                for (const auto& sel : m_mappingService->collectStaffMappings({{step.month, step.year}}))
                    entries.append(sel.entry);
            } else if (m_mappingsManager) {
                entries = m_mappingsManager->getMappingsForMonthYear(step.month, step.year);
                for (MappingEntry entry : m_mappingsManager->getDynamicMappingsForMonthYear(step.month, step.year))
                    entries.append(entry);
            }
            qDebug() << "Rolling: transferring" << entries.size() << "entries for" << step.month << step.year;

            for (const MappingEntry& entry : entries) {
                if (entry.sourceFileType == "sap_ytd") continue; // handled separately

                TransferService::Result res = m_transferService->transferEntry(
                    entry, step.year, destKey, step.outputPath, destFolder);
                monthCells += res.cellsTransferred;
            }

            // SAP YTD entries (executed last so the YTD math can safely sum previously transferred data)
            for (const MappingEntry& entry : entries) {
                if (entry.sourceFileType == "sap_ytd") {
                    TransferService::Result res = m_transferService->transferEntry(
                        entry, step.year, destKey, step.outputPath, destFolder);
                    monthCells += res.cellsTransferred;
                }
            }

            qDebug() << "Rolling: transferred" << monthCells << "cells";

            // Step 5: Save the workbook
            if (!m_handler->saveWorkbook(destKey, step.outputPath)) {
                const QString err = QString("%1 %2: Failed to save workbook").arg(step.month).arg(step.year);
                qWarning() << err;
                result.errors.append(err);
                emit stepCompleted(i, false, monthCells);
                m_handler->unloadWorkbook(destKey);
                m_handler->unloadWorkbook(srcKey);
                continue;
            }
            qDebug() << "Rolling: saved OK";

            result.successfulMonths++;
            result.totalCells += monthCells;
            emit stepCompleted(i, true, monthCells);
            emit chainProgress(i + 1, steps.size(),
                               QString("Done %1 %2 (%3 cells)").arg(step.month).arg(step.year).arg(monthCells));

            // Unload to free memory before next month
            m_handler->unloadWorkbook(destKey);
            m_handler->unloadWorkbook(srcKey);
        }

        emit chainFinished(result);
        return result;
    } catch (...) {
        result.errors.append(CrashGuard::format("RollingTransferService::executeChain"));
        qCritical() << "[CRASH_GUARD]" << result.errors.last();
        emit chainFinished(result);
        return result;
    }
}
