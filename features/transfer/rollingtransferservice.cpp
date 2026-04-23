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

struct RtTransfer {
    MappingEntry entry;
    int year = 0;
    QString month;
    QString destPath;
};

struct RtLoadTask { QString key; QString path; };

static void addLoadTask(QVector<RtLoadTask>& tasks, QSet<QString>& seen,
                        ExcelHandler* handler, const QString& key, const QString& path)
{
    if (key.isEmpty() || path.isEmpty() || seen.contains(key))
        return;
    seen.insert(key);
    if (handler && !handler->isLoaded(key))
        tasks.append({key, path});
}

static int typeOrder(const QString& type, bool copyFull)
{
    if (type == "traffic_mott") return 0;
    if (type == "pax") return 1;
    if (type == "pax_transfer") return 2;
    if (type == "staff") return 3;
    if (type == "sap") return 4;
    if (type == "ytd") return 5;
    if (type == "sap_ytd" && !copyFull) return 6;
    if (type == "sap_ytd" && copyFull) return 7;
    return 5;
}

static int monthOrder(const QString& month)
{
    const QString mm = ExcelHandler::MONTH_TO_NUM.value(month, QString());
    bool ok = false;
    const int idx = mm.toInt(&ok);
    return ok ? idx : 99;
}

static QString resolveSourcePath(const MappingEntry& entry, ExcelHandler* handler,
                                 const QString& destFolder, const QString& month, int year)
{
    const QString explicitSourcePath = entry.sourcePath.trimmed();
    auto sourcePath = [&](const QString& fallback) -> QString {
        return explicitSourcePath.isEmpty() ? fallback : explicitSourcePath;
    };

    const QString type = entry.sourceFileType;
    if (type == "pax" || type == "pax_transfer" || type == "traffic_mott") {
        return sourcePath(handler->findPaxFile(destFolder, month, year));
    }
    if (type == "staff") {
        return sourcePath(handler->findStaffFile(destFolder, year));
    }
    if (type == "sap_ytd") {
        return sourcePath(handler->findSapYtdFile(destFolder, month, year));
    }
    if (type == "sap") {
        return sourcePath(handler->findSAPFile(destFolder, month, year));
    }
    return sourcePath(handler->findCostControlFile(destFolder, month, year));
}

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

            const QString monthKey = QString("%1_%2").arg(step.month).arg(step.year);

            // Step 2: Build RT transfer list (mirrors Execute All)
            QVector<RtTransfer> transfers;
            if (!step.selectedEntries.isEmpty()) {
                for (const auto& entry : step.selectedEntries) {
                    transfers.append({entry, step.year, step.month, step.outputPath});
                }
            } else if (allEntriesByMonth.contains(monthKey)) {
                for (const auto& entry : allEntriesByMonth[monthKey]) {
                    transfers.append({entry, step.year, step.month, step.outputPath});
                }
            } else if (m_mappingService && m_mappingsManager) {
                const bool useOldMappings = step.year <= 2025;
                m_mappingsManager->loadMappings(useOldMappings ? mappingsOldPath : mappingsNewPath);

                for (const MappingEntry& entry : m_mappingsManager->getMappingsForMonthYear(step.month, step.year))
                    transfers.append({entry, step.year, step.month, step.outputPath});
                for (const MappingEntry& entry : m_mappingsManager->getDynamicMappingsForMonthYear(step.month, step.year))
                    transfers.append({entry, step.year, step.month, step.outputPath});
                for (const MappingEntry& entry : m_mappingsManager->getTrafficMottMappingsForMonthYear(step.month, step.year))
                    transfers.append({entry, step.year, step.month, step.outputPath});
                for (const MappingEntry& entry : m_mappingsManager->getPaxTransferMappingsForMonthYear(step.month, step.year))
                    transfers.append({entry, step.year, step.month, step.outputPath});
                for (const auto& sel : m_mappingService->collectPaxMappings({{step.month, step.year}}))
                    transfers.append({sel.entry, step.year, step.month, step.outputPath});
                for (const auto& sel : m_mappingService->collectStaffMappings({{step.month, step.year}}))
                    transfers.append({sel.entry, step.year, step.month, step.outputPath});
            } else if (m_mappingsManager) {
                for (const MappingEntry& entry : m_mappingsManager->getMappingsForMonthYear(step.month, step.year))
                    transfers.append({entry, step.year, step.month, step.outputPath});
                for (const MappingEntry& entry : m_mappingsManager->getDynamicMappingsForMonthYear(step.month, step.year))
                    transfers.append({entry, step.year, step.month, step.outputPath});
            }

            if (transfers.isEmpty()) {
                qWarning() << "Rolling: no mappings for" << step.month << step.year;
                emit stepCompleted(i, false, 0);
                continue;
            }

            // Sort deterministically like Execute All
            std::stable_sort(transfers.begin(), transfers.end(),
                [](const RtTransfer& a, const RtTransfer& b) {
                    if (a.year != b.year)
                        return a.year < b.year;
                    const int aMonthIdx = monthOrder(a.month);
                    const int bMonthIdx = monthOrder(b.month);
                    if (aMonthIdx != bMonthIdx)
                        return aMonthIdx < bMonthIdx;
                    return typeOrder(a.entry.sourceFileType, a.entry.copyFullSheet)
                        < typeOrder(b.entry.sourceFileType, b.entry.copyFullSheet);
                });

            // Step 3: Preload workbooks like Execute All
            QVector<RtLoadTask> tasks;
            QSet<QString> seen;
            for (const RtTransfer& tm : transfers) {
                const QString month = tm.month.isEmpty() ? tm.entry.month : tm.month;
                if (month.trimmed().isEmpty() || tm.year <= 0)
                    continue;

                const QString type = tm.entry.sourceFileType;
                if (type == "pax" || type == "pax_transfer" || type == "traffic_mott") {
                    addLoadTask(tasks, seen, m_handler,
                                QString("%1_%2_pax").arg(month).arg(tm.year),
                                resolveSourcePath(tm.entry, m_handler, destFolder, month, tm.year));
                } else if (type == "staff") {
                    addLoadTask(tasks, seen, m_handler,
                                QString("%1_%2_staff").arg(month).arg(tm.year),
                                resolveSourcePath(tm.entry, m_handler, destFolder, month, tm.year));
                } else if (type == "sap_ytd") {
                    addLoadTask(tasks, seen, m_handler,
                                QString("%1_%2_sap_ytd").arg(month).arg(tm.year),
                                resolveSourcePath(tm.entry, m_handler, destFolder, month, tm.year));
                    if (!tm.entry.trafficSourceRows.isEmpty()) {
                        addLoadTask(tasks, seen, m_handler,
                                    QString("%1_%2_pax").arg(month).arg(tm.year),
                                    m_handler->findPaxFile(destFolder, month, tm.year));
                    }
                } else if (type == "sap") {
                    addLoadTask(tasks, seen, m_handler,
                                QString("%1_%2_sap").arg(month).arg(tm.year),
                                resolveSourcePath(tm.entry, m_handler, destFolder, month, tm.year));
                } else {
                    addLoadTask(tasks, seen, m_handler,
                                QString("%1_%2_cost").arg(month).arg(tm.year),
                                resolveSourcePath(tm.entry, m_handler, destFolder, month, tm.year));
                }

                addLoadTask(tasks, seen, m_handler,
                            QString("%1_%2_cost_control").arg(month).arg(tm.year),
                            tm.destPath.isEmpty() ? m_handler->findCostControlFile(destFolder, month, tm.year) : tm.destPath);
            }

            for (const RtLoadTask& task : tasks) {
                QString loadError;
                if (!m_handler->loadWorkbook(task.path, task.key, {}, &loadError)) {
                    qWarning() << "Rolling: preload failed for" << task.key << task.path
                               << ":" << loadError;
                }
            }

            // Clear overrides for this destKey (Execute All behavior)
            const QString destKey = QString("%1_%2_cost_control").arg(step.month).arg(step.year);
            m_handler->resetOverrides(destKey);

            // Step 4: Transfer sequentially (same TransferService entry logic)
            int monthCells = 0;
            for (const RtTransfer& tm : transfers) {
                TransferService::Result res = m_transferService->transferEntry(
                    tm.entry, tm.year, destKey, step.outputPath, destFolder);
                monthCells += res.cellsTransferred;
            }

            // Step 4b: Compute cumulative columns for this month.
            // Collect all dest rows that target MZLZ Consolidated base columns.
            // runCumulativePassRT writes only the target month's cumulative column
            // (e.g. IQ for February) using prevCum already in the file + the
            // newly-written base column. Completely decoupled from Execute All.
            {
                static const QSet<QString> baseColSet = {
                    "G","W","AM","BD","BW","CP","DJ","EF","FB","FY","GX","HW"
                };
                QSet<int> mzlzRows;
                for (const RtTransfer& tm : transfers) {
                    if (tm.entry.destSheet != "MZLZ Consolidated") continue;
                    if (!baseColSet.contains(tm.entry.destColumn.toUpper())) continue;
                    if (!tm.entry.rowMap.isEmpty()) {
                        for (auto it = tm.entry.rowMap.constBegin();
                             it != tm.entry.rowMap.constEnd(); ++it)
                            mzlzRows.insert(it.key());
                    } else {
                        for (int r : tm.entry.destRows) mzlzRows.insert(r);
                    }
                    for (auto it = tm.entry.trafficPaxRowMap.constBegin();
                         it != tm.entry.trafficPaxRowMap.constEnd(); ++it)
                        mzlzRows.insert(it.key());
                }
                if (!mzlzRows.isEmpty()) {
                    // Convert month name to 1-based int (January=1 … December=12)
                    const int monthInt = MONTHS.indexOf(step.month) + 1;
                    m_transferService->runCumulativePassRT(
                        mzlzRows, "MZLZ Consolidated", step.year, destKey, monthInt);
                } else {
                    qWarning() << "[RT] No MZLZ rows found for cumulative pass —"
                               << step.month << step.year;
                }
            }

            // Step 5: Save the workbook (same as Execute All)
            if (!m_handler->saveWorkbook(destKey, step.outputPath)) {
                const QString err = QString("%1 %2: Failed to save workbook").arg(step.month).arg(step.year);
                qWarning() << err;
                result.errors.append(err);
                emit stepCompleted(i, false, monthCells);
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
            m_handler->unloadWorkbook(QString("%1_%2_cost").arg(step.month).arg(step.year));
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
