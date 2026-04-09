#include "fillallworker.h"
#include <QDebug>
#include <algorithm>
#include <cmath>
#include "../../core/crashguard.h"

namespace {
bool isYtdTransferType(const QString& transferType)
{
    return transferType == "sap_ytd" || transferType == "ytd";
}
}

// ---------------------------------------------------------------------------
// Main run() — called from worker thread
// ---------------------------------------------------------------------------
void FillAllWorker::run()
{
    FillAllResult result;
    try {
        if (!m_handler || !m_mappings || !m_transfer) {
            result.errors.append("Fill All aborted: worker dependencies are not initialized.");
            emit finished(result);
            return;
        }


    const int year        = m_scan.year;
    const int targetMonth = m_scan.targetMonth;
    const bool hasFinalYtdPhase = std::any_of(
        m_scan.entries.cbegin(), m_scan.entries.cend(),
        [](const FillAllFileEntry& entry) {
            return isYtdTransferType(entry.transferType);
        });
    const int totalProgressSteps = targetMonth + (hasFinalYtdPhase ? 3 : 2);

    static const QStringList monthNames = {
        "January","February","March","April","May","June",
        "July","August","September","October","November","December"
    };

    if (year <= 0) {
        result.errors.append("Fill All aborted: invalid year.");
        emit finished(result);
        return;
    }
    if (targetMonth < 1 || targetMonth > monthNames.size()) {
        result.errors.append(QString("Fill All aborted: invalid target month (%1).").arg(targetMonth));
        emit finished(result);
        return;
    }

    // =========================================================================
    // PHASE 1: Load the destination cost_control file ONCE (isSaveTarget = true)
    // =========================================================================
    const QString targetMonthName = monthNames[targetMonth - 1];
    const QString destKey = QString("%1_%2_cost_control").arg(targetMonthName).arg(year);
    QVector<FillAllFileEntry> deferredYtdEntries;

    emit progress(0, totalProgressSteps, "Loading destination file...");

    if (!m_handler->isLoaded(destKey)) {
        QString loadError;
        bool ok = m_handler->loadWorkbook(m_scan.destFilePath, destKey, {}, &loadError);
        if (!ok) {
            if (loadError.isEmpty()) loadError = "unknown workbook load error";
            result.errors.append(
                QString("Failed to load destination: %1 (%2)")
                    .arg(m_scan.destFilePath, loadError));
            emit finished(result);
            return;
        }
    }

    // =========================================================================
    // PHASE 2: Process months January → targetMonth
    // =========================================================================
    for (int m = 1; m <= targetMonth; m++) {
        const QString monthName = monthNames[m - 1];

        emit progress(m, totalProgressSteps,
                      QString("Processing %1 (%2/%3)...").arg(monthName).arg(m).arg(targetMonth));

        // Collect entries for this month
        QVector<FillAllFileEntry> monthEntries;
        for (const auto& e : m_scan.entries) {
            if (e.monthNumber == m) monthEntries.append(e);
        }

        // Sort by dependency order: traffic_mott first, sap_ytd last
        std::stable_sort(monthEntries.begin(), monthEntries.end(),
            [](const FillAllFileEntry& a, const FillAllFileEntry& b) {
                return FillAllWorker::typeOrder(a.transferType)
                     < FillAllWorker::typeOrder(b.transferType);
            });

        // Process each entry
        for (const auto& fileEntry : monthEntries) {
            if (isYtdTransferType(fileEntry.transferType)) {
                if (m == targetMonth) {
                    // Defer sap_ytd for target month — runs after all months complete (Phase 4)
                    deferredYtdEntries.append(fileEntry);
                    qDebug() << "[FILL_ALL] Deferred final YTD for" << monthName
                             << fileEntry.transferType;
                    continue;
                }
                // For non-target months: run sap_ytd IMMEDIATELY while this month's PAX
                // file is still loaded. The paxRows path sums Jan..currentMonth correctly
                // and also writes per-month MZLZ rows 5-11 monthly columns (G/W/AM/...),
                // overwriting stale =+'TRAFFIC mott 2025'!$G$81 formulas.
                // Falls through to the normal load+execute path below.
            }

            result.totalTransfers++;

            if (!fileEntry.found) {
                result.warnings.append(
                    QString("[%1] %2: %3").arg(monthName, fileEntry.transferType,
                                               fileEntry.statusMessage));
                result.failCount++;
                qDebug() << "[FILL_ALL] SKIP" << monthName << fileEntry.transferType
                         << "-" << fileEntry.statusMessage;
                continue;
            }

            // Build source cache key
            const QString srcKey = buildSourceKey(monthName, year, fileEntry.transferType);

            // Load source if not already in cache
            if (!m_handler->isLoaded(srcKey)) {
                QString loadError;
                bool ok = m_handler->loadWorkbook(fileEntry.sourceFilePath, srcKey, {}, &loadError);
                if (!ok) {
                    if (loadError.isEmpty()) loadError = "unknown workbook load error";
                    result.errors.append(
                        QString("[%1] %2: failed to load %3 (%4)")
                            .arg(monthName, fileEntry.transferType,
                                 fileEntry.sourceFilePath, loadError));
                    result.failCount++;
                    continue;
                }
            }

            // ── Copy Full Sheet shortcut ─────────────────────────────────────
            if (fileEntry.copyFullSheet) {
                const QString srcSheet = fileEntry.sourceSheetName.isEmpty()
                                         ? "Sheet1" : fileEntry.sourceSheetName;
                const QString newName  = fileEntry.customSheetName.isEmpty()
                                         ? srcSheet : fileEntry.customSheetName;
                bool ok = m_handler->copyFullSheet(srcKey, srcSheet, destKey, newName);
                if (!ok) {
                    result.errors.append(
                        QString("[%1] %2: copyFullSheet failed (%3 → %4)")
                            .arg(monthName, fileEntry.transferType, srcSheet, newName));
                    result.failCount++;
                } else {
                    if (!fileEntry.insertAfterSheet.isEmpty())
                        m_handler->setInsertAfter(destKey, newName, fileEntry.insertAfterSheet);
                    result.cellsTransferred++;
                    result.successCount++;
                }
                continue; // skip normal transfer
            }

            // Get mappings for this month + type
            const QVector<MappingEntry> mappings =
                getMappingsForType(monthName, year, fileEntry.transferType);

            if (mappings.isEmpty()) {
                const QString msg = QString("[%1] %2: no mappings loaded")
                                        .arg(monthName, fileEntry.transferType);
                result.warnings.append(msg);
                qWarning() << "[FILL_ALL]" << msg;
                continue;
            }

            // Execute each mapping entry
            for (MappingEntry mapping : mappings) {
                mapping.month      = monthName;
                mapping.sourcePath = fileEntry.sourceFilePath;

                TransferService::Result r = m_transfer->transferEntry(
                    mapping, year, destKey,
                    m_scan.destFilePath,
                    m_scan.destFolder);

                if (r.error.isEmpty()) {
                    result.successCount++;
                    result.cellsTransferred += r.cellsTransferred;
                    qDebug() << "[FILL_ALL] OK" << monthName << fileEntry.transferType
                             << "cells=" << r.cellsTransferred;
                } else {
                    result.failCount++;
                    result.errors.append(
                        QString("[%1] %2: %3").arg(monthName, fileEntry.transferType, r.error));
                    qDebug() << "[FILL_ALL] FAIL" << monthName << fileEntry.transferType
                             << r.error;
                }
            }

            // Unload source to free memory — keep destKey and shared PAX file in cache
            // (PAX is shared by traffic_mott + pax_transfer + sap_ytd, unload after month done)
            const bool isSharedPax = (fileEntry.transferType == "traffic_mott"
                                   || fileEntry.transferType == "pax_transfer"
                                   || fileEntry.transferType == "sap_ytd");
            // sap_ytd shares the PAX key — do not unload mid-month
            const bool keepSourceForFinalYtd =
                (m == targetMonth && !deferredYtdEntries.isEmpty() && fileEntry.transferType == "staff");
            if (!isSharedPax && srcKey != destKey && !keepSourceForFinalYtd) {
                m_handler->unloadWorkbook(srcKey);
                qDebug() << "[FILL_ALL] Unloaded" << srcKey;
            }
        }

        // After all entries for this month are done, unload the PAX file
        // (shared by traffic_mott / pax_transfer / sap_ytd)
        const QString paxKey = QString("%1_%2_pax").arg(monthName).arg(year);
        // Keep PAX loaded for target month (needed by deferred sap_ytd in Phase 4).
        // For non-target months, sap_ytd already ran inline above, so unload now.
        const bool keepPaxForFinalYtd = (m == targetMonth && !deferredYtdEntries.isEmpty());
        if (m_handler->isLoaded(paxKey) && !keepPaxForFinalYtd) {
            m_handler->unloadWorkbook(paxKey);
            qDebug() << "[FILL_ALL] Unloaded PAX" << paxKey;
        }

        qDebug() << "[FILL_ALL] Month" << monthName << "complete —"
                 << "success=" << result.successCount
                 << "fail=" << result.failCount;
    }

    // =========================================================================
    // PHASE 3: Compute Q column in TRAFFIC mott sheet (YTD sums for all rows)
    // Must run AFTER all months' traffic_mott transfers are done.
    // =========================================================================
    emit progress(targetMonth + 1, totalProgressSteps, "Computing traffic YTD sums...");
    computeTrafficQ(destKey, targetMonth);

    // =========================================================================
    // PHASE 4: Execute final-month YTD only once, after Jan-targetMonth is complete
    // =========================================================================
    if (!deferredYtdEntries.isEmpty()) {
        emit progress(targetMonth + 2, totalProgressSteps,
                      QString("Executing %1 YTD...").arg(targetMonthName));

        for (const auto& fileEntry : deferredYtdEntries) {
            result.totalTransfers++;

            if (!fileEntry.found) {
                result.warnings.append(
                    QString("[%1] %2: %3").arg(targetMonthName, fileEntry.transferType,
                                               fileEntry.statusMessage));
                result.failCount++;
                qDebug() << "[FILL_ALL] SKIP" << targetMonthName << fileEntry.transferType
                         << "-" << fileEntry.statusMessage;
                continue;
            }

            const QString srcKey = buildSourceKey(targetMonthName, year, fileEntry.transferType);

            if (!m_handler->isLoaded(srcKey)) {
                QString loadError;
                bool ok = m_handler->loadWorkbook(fileEntry.sourceFilePath, srcKey, {}, &loadError);
                if (!ok) {
                    if (loadError.isEmpty()) loadError = "unknown workbook load error";
                    result.errors.append(
                        QString("[%1] %2: failed to load %3 (%4)")
                            .arg(targetMonthName, fileEntry.transferType,
                                 fileEntry.sourceFilePath, loadError));
                    result.failCount++;
                    continue;
                }
            }

            const QVector<MappingEntry> mappings =
                getMappingsForType(targetMonthName, year, fileEntry.transferType);

            if (mappings.isEmpty()) {
                const QString msg = QString("[%1] %2: no YTD mappings loaded")
                                        .arg(targetMonthName, fileEntry.transferType);
                result.warnings.append(msg);
                qWarning() << "[FILL_ALL]" << msg;
                continue;
            }

            for (MappingEntry mapping : mappings) {
                mapping.month      = targetMonthName;
                mapping.sourcePath = fileEntry.sourceFilePath;

                TransferService::Result r = m_transfer->transferEntry(
                    mapping, year, destKey,
                    m_scan.destFilePath,
                    m_scan.destFolder);

                if (r.error.isEmpty()) {
                    result.successCount++;
                    result.cellsTransferred += r.cellsTransferred;
                    qDebug() << "[FILL_ALL] OK" << targetMonthName << fileEntry.transferType
                             << "cells=" << r.cellsTransferred;
                } else {
                    result.failCount++;
                    result.errors.append(
                        QString("[%1] %2: %3").arg(targetMonthName, fileEntry.transferType, r.error));
                    qDebug() << "[FILL_ALL] FAIL" << targetMonthName << fileEntry.transferType
                             << r.error;
                }
            }

            if (srcKey != destKey && m_handler->isLoaded(srcKey)) {
                m_handler->unloadWorkbook(srcKey);
                qDebug() << "[FILL_ALL] Unloaded" << srcKey;
            }
        }

        const QString targetPaxKey = QString("%1_%2_pax").arg(targetMonthName).arg(year);
        if (m_handler->isLoaded(targetPaxKey)) {
            m_handler->unloadWorkbook(targetPaxKey);
            qDebug() << "[FILL_ALL] Unloaded PAX" << targetPaxKey;
        }

        const QString targetStaffKey = QString("%1_%2_staff").arg(targetMonthName).arg(year);
        if (m_handler->isLoaded(targetStaffKey)) {
            m_handler->unloadWorkbook(targetStaffKey);
            qDebug() << "[FILL_ALL] Unloaded" << targetStaffKey;
        }
    }

    // =========================================================================
    // PHASE 5: Save destination file
    // =========================================================================
    emit progress(totalProgressSteps, totalProgressSteps, "Saving...");

    bool saved = m_handler->saveWorkbook(destKey, m_scan.destFilePath);
    if (!saved) {
        result.errors.append("Failed to save: " + m_scan.destFilePath);
        qWarning() << "[FILL_ALL] Save failed:" << m_scan.destFilePath;
    } else {
        qDebug() << "[FILL_ALL] Saved to" << m_scan.destFilePath;
    }

    m_handler->unloadWorkbook(destKey);
    } catch (...) {
        const QString error = CrashGuard::format("FillAllWorker::run");
        qCritical() << "[CRASH_GUARD]" << error;
        result.errors.append(error);
    }

    emit finished(result);
}

// ---------------------------------------------------------------------------
// buildSourceKey — matches the cache keys used in TransferWorker / TransferService
// ---------------------------------------------------------------------------
QString FillAllWorker::buildSourceKey(const QString& monthName, int year,
                                      const QString& transferType) const
{
    if (transferType == "sap")
        return QString("%1_%2_sap").arg(monthName).arg(year);
    if (transferType == "sap_ytd")
        return QString("%1_%2_sap_ytd").arg(monthName).arg(year);
    if (transferType == "traffic_mott" || transferType == "pax_transfer")
        // Both read from the PAX file — shared key avoids double-loading
        return QString("%1_%2_pax").arg(monthName).arg(year);
    if (transferType == "staff")
        return QString("%1_%2_staff").arg(monthName).arg(year);
    if (transferType == "budget_refi") {
        // Budget/REFI/PrevYear reads FROM the destination cost_control file
        // (always the target month's workbook — the only one loaded in Fill All)
        static const QStringList kMonthNames = {
            "January","February","March","April","May","June",
            "July","August","September","October","November","December"
        };
        if (m_scan.targetMonth < 1 || m_scan.targetMonth > kMonthNames.size()) {
            return QString("%1_%2_cost_control").arg(monthName).arg(year);
        }
        const QString targetMonthName = kMonthNames[m_scan.targetMonth - 1];
        return QString("%1_%2_cost_control").arg(targetMonthName).arg(year);
    }

    // Fallback
    return QString("%1_%2_%3").arg(monthName).arg(year).arg(transferType);
}

// ---------------------------------------------------------------------------
// getMappingsForType — delegates to existing MappingsManager methods
// ---------------------------------------------------------------------------
QVector<MappingEntry> FillAllWorker::getMappingsForType(
    const QString& monthName, int year, const QString& transferType) const
{
    if (transferType == "sap")
        return m_mappings->getMappingsForMonthYear(monthName, year);
    if (transferType == "budget_refi")
        return m_mappings->getDynamicMappingsForMonthYear(monthName, year);
    if (transferType == "traffic_mott")
        return m_mappings->getTrafficMottMappingsForMonthYear(monthName, year);
    if (transferType == "pax_transfer")
        return m_mappings->getPaxTransferMappingsForMonthYear(monthName, year);
    if (transferType == "sap_ytd")
        return m_mappings->getSapYtdMappingsForMonthYear(monthName, year);
    if (transferType == "staff") {
        // Staff uses a single StaffMappingEntry — wrap into MappingEntry
        StaffMappingEntry se = m_mappings->getStaffMappingForMonth(monthName);
        if (!se.sourceColumn.isEmpty()) {
            MappingEntry e;
            e.month             = monthName;
            e.sourceFileType    = "staff";
            e.sourceSheetTemplate = se.sourceSheet;
            e.sourceColumn      = se.sourceColumn;
            e.sourceRows        = { se.sourceRow };
            e.destSheet         = se.destSheet;
            e.destColumn        = se.destColumn;
            e.destRows          = { se.destRow };
            return { e };
        }
        return {};
    }

    return {};
}

// ---------------------------------------------------------------------------
// computeTrafficQ — writes static YTD sums into the Q column of the
//                   TRAFFIC mott sheet in the cost_control workbook.
//                   Called once after ALL months are transferred.
// ---------------------------------------------------------------------------
void FillAllWorker::computeTrafficQ(const QString& destKey, int targetMonth)
{
    if (!m_handler) {
        qWarning() << "[FILL_ALL] computeTrafficQ skipped: Excel handler is null";
        return;
    }
    if (targetMonth < 1 || targetMonth > 12) {
        qWarning() << "[FILL_ALL] computeTrafficQ skipped: invalid target month" << targetMonth;
        return;
    }

    // Find the TRAFFIC mott sheet in cost_control
    QString trafficSheet;
    for (const QString& name : m_handler->getSheetNames(destKey)) {
        if (name.trimmed().toLower().contains("traffic mott")) {
            trafficSheet = name;
            break;
        }
    }
    if (trafficSheet.isEmpty()) {
        qDebug() << "[FILL_ALL] TRAFFIC mott sheet not found in" << destKey
                 << "— skipping Q computation";
        return;
    }

    // TRAFFIC mott dest columns: Jan=D(4), Feb=E(5), ..., Dec=O(15) — sequential
    const int colD  = m_handler->letterToColumn("D"); // January
    const int colQ  = m_handler->letterToColumn("Q"); // Q = YTD sum column
    const int endCol = colD + targetMonth - 1;         // e.g. March → colF

    // Set Q9 and Q10 to month number (metadata used by Excel formula controller)
    m_handler->setCellValue(destKey, trafficSheet, 9,  colQ, targetMonth);
    m_handler->setCellValue(destKey, trafficSheet, 10, colQ, targetMonth);

    // Only sum known data rows (from rowMap keys in Traffic_mott.json).
    // Writing to non-data rows causes the Sheet5 Excel repair dialog.
    static const QVector<int> trafficDataRows = {
        16, 17, 18, 23, 24, 25, 31, 32, 33, 37, 38, 39, 40,
        43, 44, 46, 47, 49, 50, 52, 53, 55, 57, 58, 59, 61, 62,
        68, 69, 70, 78, 79, 80, 85, 86, 93, 94, 95, 100, 101,
        106, 107, 108, 113, 114, 115, 116, 119, 120, 121, 122,
        130, 131, 132, 133, 136, 137, 138, 139, 140,
        148, 149, 150, 152, 156, 157, 158, 161
    };

    int rowsComputed = 0;
    for (int row : trafficDataRows) {
        double ytdSum = 0.0;
        bool hasValue = false;

        for (int col = colD; col <= endCol; col++) {
            QVariant raw = m_handler->getCellValue(destKey, trafficSheet, row, col);
            if (raw.isValid() && raw.canConvert<double>()) {
                ytdSum += raw.toDouble();
                hasValue = true;
            }
        }

        if (hasValue) {
            m_handler->setCellValue(destKey, trafficSheet, row, colQ,
                                    std::round(ytdSum));
            rowsComputed++;
        }
    }

    qDebug() << "[FILL_ALL] Q column computed for" << rowsComputed
             << "rows, months 1-" << targetMonth
             << "colD-col" << QChar('D' + targetMonth - 1)
             << "(" << trafficSheet << ")";
}

