#include "fillallworker.h"
#include <QDebug>
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// Main run() — called from worker thread
// ---------------------------------------------------------------------------
void FillAllWorker::run()
{
    FillAllResult result;

    const int year        = m_scan.year;
    const int targetMonth = m_scan.targetMonth;

    static const QStringList monthNames = {
        "January","February","March","April","May","June",
        "July","August","September","October","November","December"
    };

    // =========================================================================
    // PHASE 1: Load the destination cost_control file ONCE (isSaveTarget = true)
    // =========================================================================
    const QString targetMonthName = monthNames[targetMonth - 1];
    const QString destKey = QString("%1_%2_cost_control").arg(targetMonthName).arg(year);

    emit progress(0, targetMonth + 1, "Loading destination file...");

    if (!m_handler->isLoaded(destKey)) {
        bool ok = m_handler->loadWorkbook(m_scan.destFilePath, destKey);
        if (!ok) {
            result.errors.append("Failed to load destination: " + m_scan.destFilePath);
            emit finished(result);
            return;
        }
    }

    // =========================================================================
    // PHASE 2: Process months January → targetMonth
    // =========================================================================
    for (int m = 1; m <= targetMonth; m++) {
        const QString monthName = monthNames[m - 1];

        emit progress(m, targetMonth + 1,
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
            result.totalTransfers++;

            if (!fileEntry.found) {
                result.warnings.append(
                    QString("[%1] %2: %3").arg(monthName, fileEntry.transferType,
                                               fileEntry.statusMessage));
                result.failCount++;
                qDebug() << "[FILL_ALL] SKIP" << monthName << fileEntry.transferType
                         << "—" << fileEntry.statusMessage;
                continue;
            }

            // Build source cache key
            const QString srcKey = buildSourceKey(monthName, year, fileEntry.transferType);

            // Load source if not already in cache
            if (!m_handler->isLoaded(srcKey)) {
                bool ok = m_handler->loadWorkbook(fileEntry.sourceFilePath, srcKey);
                if (!ok) {
                    result.errors.append(
                        QString("[%1] %2: failed to load %3")
                        .arg(monthName, fileEntry.transferType, fileEntry.sourceFilePath));
                    result.failCount++;
                    continue;
                }
            }

            // Get mappings for this month + type
            const QVector<MappingEntry> mappings =
                getMappingsForType(monthName, year, fileEntry.transferType);

            if (mappings.isEmpty()) {
                qDebug() << "[FILL_ALL] No mappings for" << monthName << fileEntry.transferType;
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
            if (!isSharedPax && srcKey != destKey) {
                m_handler->unloadWorkbook(srcKey);
                qDebug() << "[FILL_ALL] Unloaded" << srcKey;
            }
        }

        // After all entries for this month are done, unload the PAX file
        // (shared by traffic_mott / pax_transfer / sap_ytd)
        const QString paxKey = QString("%1_%2_pax").arg(monthName).arg(year);
        if (m_handler->isLoaded(paxKey)) {
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
    emit progress(targetMonth, targetMonth + 1, "Computing traffic YTD sums...");
    computeTrafficQ(destKey, targetMonth);

    // =========================================================================
    // PHASE 4: Save destination file
    // =========================================================================
    emit progress(targetMonth + 1, targetMonth + 1, "Saving...");

    bool saved = m_handler->saveWorkbook(destKey, m_scan.destFilePath);
    if (!saved) {
        result.errors.append("Failed to save: " + m_scan.destFilePath);
        qWarning() << "[FILL_ALL] Save failed:" << m_scan.destFilePath;
    } else {
        qDebug() << "[FILL_ALL] Saved to" << m_scan.destFilePath;
    }

    m_handler->unloadWorkbook(destKey);

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
    if (transferType == "budget_refi")
        // Budget/REFI/PrevYear reads FROM the destination cost_control file
        return QString("%1_%2_cost_control")
               .arg(monthName).arg(year);

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

    // TRAFFIC mott dest columns: Jan=D(4), Feb=E(5), ..., Dec=O(15)
    const int colD = m_handler->letterToColumn("D"); // January = column D (index)
    const int colQ = m_handler->letterToColumn("Q"); // Q column
    const int endCol = colD + targetMonth - 1;       // e.g. targetMonth=3 → colF

    // Set Q9 and Q10 to month number (metadata used by Excel formula controller)
    m_handler->setCellValue(destKey, trafficSheet, 9,  colQ, targetMonth);
    m_handler->setCellValue(destKey, trafficSheet, 10, colQ, targetMonth);

    // Compute Q for all data rows (1–165 covers the full TRAFFIC mott sheet)
    int rowsComputed = 0;
    for (int row = 1; row <= 165; row++) {
        double ytdSum   = 0.0;
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
             << "(" << trafficSheet << ")";
}
