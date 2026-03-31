#include "rollingtransferservice.h"
#include <QtConcurrent>
#include <QThreadPool>
#include <QFuture>
#include "../../services/excelhandler.h"
#include "../../services/transferservice.h"
#include "../../services/mappingservice.h"
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
            step.inputPath = m_handler->findCostControlFile(outputDir, prevMonth, prevYear);
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

    // Pre-load JSON mappings
    const QString mappingsOldPath = QString("%1/mappings_old.json").arg(jsonBase);
    const QString mappingsNewPath = QString("%1/mappings.json").arg(jsonBase);
    const QString sapYtdPath      = QString("%1/mappings_sap_ytd.json").arg(jsonBase);
    const QString paxPath         = QString("%1/pax.json").arg(jsonBase);
    const QString staffPath       = QString("%1/staff.json").arg(jsonBase);
    const QString budgetRefiPath  = QString("%1/mappings_budget_refi_prev_year.json").arg(jsonBase);

    m_mappingsManager->loadMappings(mappingsOldPath);
    m_mappingsManager->loadMappings(mappingsNewPath);
    m_mappingsManager->loadSapYtdMappings(sapYtdPath);
    m_mappingsManager->loadPaxMappings(paxPath);
    m_mappingsManager->loadStaffMappings(staffPath);
    m_mappingsManager->loadBudgetRefiMappings(budgetRefiPath);

    for (int i = 0; i < steps.size(); ++i) {
        const RollingStep& step = steps[i];

        emit chainProgress(i, steps.size(),
                           QString("Rolling %1 %2...").arg(step.month).arg(step.year));

        qDebug() << "Rolling step" << (i+1) << "/" << steps.size()
                 << step.month << step.year
                 << "\n  src:" << step.inputPath
                 << "\n  dst:" << step.outputPath;

        // ── Step 1: Copy previous month's file to new month's test/ folder ──
        QDir().mkpath(QFileInfo(step.outputPath).absolutePath());
        if (QFile::exists(step.outputPath)) {
            qDebug() << "Rolling: destination exists, overwriting";
            QFile::remove(step.outputPath);
        }
        if (!QFile::copy(step.inputPath, step.outputPath)) {
            const QString err = QString("%1 %2: Failed to copy file").arg(step.month).arg(step.year);
            qWarning() << err << step.inputPath << "→" << step.outputPath;
            result.errors.append(err);
            emit stepCompleted(i, false, 0);
            continue;
        }
        qDebug() << "Rolling: file copied OK";

        // ── Step 2: Load dest workbook + source files in parallel ──
        QString destKey = QString("%1_%2_cost_control").arg(step.month).arg(step.year);
        QString srcKey  = QString("%1_%2_cost").arg(step.month).arg(step.year);
        m_handler->unloadWorkbook(destKey);

        QThreadPool pool;
        pool.setMaxThreadCount(2);

        QString sapPath2   = m_handler->findSAPFile(destFolder, step.month, step.year);
        QString paxPath2   = m_handler->findPaxFile(destFolder, step.month, step.year);
        QString staffPath2 = m_handler->findStaffFile(destFolder, step.year);

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

        // ── Step 3: Also load source workbook (_cost key) for same-file SAP transfers ──
        if (!m_handler->isLoaded(srcKey)) {
            m_handler->loadWorkbook(step.outputPath, srcKey, {});
        }

        // ── Step 4: Transfer selected mapping entries (or all if none specified) ──
        int monthCells = 0;
        QVector<MappingEntry> entries;
        const QString monthKey = QString("%1_%2").arg(step.month).arg(step.year);
        if (!step.selectedEntries.isEmpty()) {
            // User selected specific cards
            entries = step.selectedEntries;
        } else if (allEntriesByMonth.contains(monthKey)) {
            // Use pre-collected entries from mainwindow
            entries = allEntriesByMonth[monthKey];
        } else if (m_mappingService && m_mappingsManager) {
            // Fallback: load correct JSON for year and collect all mapping types
            const bool useOldMappings = step.year <= 2025;
            m_mappingsManager->loadMappings(useOldMappings ? mappingsOldPath : mappingsNewPath);

            entries = m_mappingsManager->getMappingsForMonthYear(step.month, step.year);
            for (MappingEntry entry : m_mappingsManager->getDynamicMappingsForMonthYear(step.month, step.year))
                entries.append(entry);
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

            // Resolve source path
            QString srcPath;
            if (entry.sourceFileType == "sap") {
                srcPath = m_handler->findSAPFile(destFolder, step.month, step.year);
            } else if (entry.sourceFileType == "pax") {
                srcPath = m_handler->findPaxFile(destFolder, step.month, step.year);
            } else if (entry.sourceFileType == "staff") {
                srcPath = m_handler->findStaffFile(destFolder, step.year);
            } else {
                srcPath = step.outputPath; // same file (cost control = dest)
            }

            TransferService::Result res = m_transferService->transferEntry(
                entry, step.year, destKey, step.outputPath, destFolder);
            monthCells += res.cellsTransferred;
        }

        // SAP YTD entries
        for (const MappingEntry& entry : m_mappingsManager->getSapYtdMappingsForMonthYear(step.month, step.year)) {
            TransferService::Result res = m_transferService->transferEntry(
                entry, step.year, destKey, step.outputPath, destFolder);
            monthCells += res.cellsTransferred;
        }

        qDebug() << "Rolling: transferred" << monthCells << "cells";

        // ── Step 5: Save the workbook ──
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
}
