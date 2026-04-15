#include "transferservice.h"
#include <QElapsedTimer>
#include "excelhandler.h"
#include <QRegularExpression>
#include <QDate>
#include <cmath>

// Round to 5 decimal places: first divide by 1000, then round to keep 5 decimals
static double roundTo5(double v) {
    return std::round(v * 100000.0) / 100000.0;
}

TransferService::TransferService(ExcelHandler* handler, QObject* parent)
    : QObject(parent), m_handler(handler) {}

TransferService::Result TransferService::transferEntry(const MappingEntry& entry,
                                                       int year,
                                                       const QString& destKey,
                                                       const QString& destFilePath,
                                                       const QString& baseFolder) {
    QElapsedTimer timer;
    timer.start();
    Result result;
    if (!m_handler) {
        result.error = "Excel handler not available";
        qDebug() << "transferEntry END" << entry.sourceSheetTemplate << "cells" << result.cellsTransferred
                 << "elapsed" << timer.elapsed() << "ms";
        return result;
    }

    // ── Copy Full Sheet ─────────────────────────────────────────────────────
    // If the mapping card has "Copy full sheet" checked, copy the entire source
    // sheet XML into the destination workbook instead of doing a row transfer.
    if (entry.copyFullSheet) {
        if (entry.sourceFileType == "traffic_mott") {
            qWarning() << "[COPY_SHEET] traffic_mott copyFullSheet disabled; using row transfer instead.";
        } else {
            const QString srcKey   = QString("%1_%2_%3")
                                         .arg(entry.month, QString::number(year),
                                              entry.sourceFileType);
            const QString srcSheet = entry.sourceSheetTemplate;
            const QString newName  = entry.customSheetName.isEmpty()
                                     ? srcSheet : entry.customSheetName;

            qInfo() << "[COPY_SHEET] Copying sheet" << srcSheet
                    << "from" << srcKey << "→" << destKey << "as" << newName;

            bool ok = m_handler->copyFullSheet(srcKey, srcSheet, destKey, newName);
            if (!ok) {
                result.error = QString("copyFullSheet failed: %1 → %2").arg(srcSheet, newName);
                qWarning() << result.error;
            } else {
                if (!entry.insertAfterSheet.isEmpty())
                    m_handler->setInsertAfter(destKey, newName, entry.insertAfterSheet);
                result.cellsTransferred = 1;
            }
            return result;
        }
    }

    if (entry.copyFullSheet && entry.sourceFileType == "traffic_mott") {
        // fall through to normal row-based transfer
    }


    // Use entry.month if set, otherwise derive from destKey (format: "Month_year_cost_control")
    QString month = entry.month;
    if (month.isEmpty()) {
        // destKey format: "June_2025_cost_control" — extract month from it
        month = destKey.section('_', 0, 0);
    }

    QString sourceFileType = entry.sourceFileType.isEmpty() ? "cost_control" : entry.sourceFileType;
    bool divideBy1000 = (sourceFileType == "sap" || sourceFileType == "sap_ytd" || sourceFileType == "pax");
    // pax_transfer/traffic_mott: reads from pax file, writes to cost_control — no division
    const bool isPaxTransfer = (sourceFileType == "pax_transfer");
    const bool isTrafficMott = (sourceFileType == "traffic_mott");
    if (entry.destColumn == "GX") {
        // November GX column should be scaled by -1000 instead of +1000
        divideBy1000 = true;
    }
    const bool isYtdRow216 = (sourceFileType == "sap_ytd") && entry.rowMap.contains(216);

    if (sourceFileType == "sap_ytd") {
        TransferService::Result res = handleSapYtd(entry, year, destKey, destFilePath, baseFolder);
        qDebug() << "transferEntry END" << entry.sourceSheetTemplate << "cells" << res.cellsTransferred
                 << "elapsed" << timer.elapsed() << "ms";
        return res;
    }

    if (sourceFileType == "ytd") {
        TransferService::Result res = handleYtd(entry, year, destKey, destFilePath);
        qDebug() << "transferEntry END" << entry.sourceSheetTemplate << "cells" << res.cellsTransferred
                 << "elapsed" << timer.elapsed() << "ms";
        return res;
    }

    QVector<int> srcRows = entry.sourceRows;
    QVector<int> destRows = entry.destRows;

    QString srcKey;
    QString srcFilePath;

    if (sourceFileType == "sap") {
        srcKey = QString("%1_%2_sap").arg(month).arg(year);
        srcFilePath = m_handler->findSAPFile(baseFolder, month, year);
    } else if (sourceFileType == "pax") {
        srcKey = QString("%1_%2_pax").arg(month).arg(year);
        srcFilePath = m_handler->findPaxFile(baseFolder, month, year);
    } else if (isPaxTransfer || isTrafficMott) {
        // source = pax file (Sheet1), dest handled by destKey
        srcKey = QString("%1_%2_pax").arg(month).arg(year);
        srcFilePath = m_handler->findPaxFile(baseFolder, month, year);
    } else if (sourceFileType == "staff") {
        srcKey = QString("%1_%2_staff").arg(month).arg(year);
        srcFilePath = m_handler->findStaffFile(baseFolder, year);
    } else {
        srcKey = QString("%1_%2_cost").arg(month).arg(year);
        srcFilePath = m_handler->findCostControlFile(baseFolder, month, year);
    }

    // Allow callers (Fill All / Execute All preflight) to override source path.
    if (!entry.sourcePath.trimmed().isEmpty()) {
        srcFilePath = entry.sourcePath;
    }

    // Only load if not already in cache (prevents redundant ZIP extractions)
    if (srcFilePath.isEmpty()) {
        result.error = QString("Source file missing for %1 (%2 %3)")
                           .arg(sourceFileType, month)
                           .arg(year);
        qWarning() << result.error;
        return result;
    } else if (!m_handler->isLoaded(srcKey)) {
        QString loadError;
        if (!m_handler->loadWorkbook(srcFilePath, srcKey, {}, &loadError)) {
            if (loadError.isEmpty()) {
                loadError = "unknown workbook load error";
            }
            result.error = QString("Failed to load source workbook: %1 (%2)")
                               .arg(srcFilePath, loadError);
            qWarning() << result.error;
            return result;
        }
    }

    auto resolveSheet = [&](const QString& key, const QString& requested) {
        if (requested.isEmpty()) return requested;
        QString requestedNorm = requested.trimmed().toLower();
        for (const QString& name : m_handler->getSheetNames(key)) {
            if (name.trimmed().toLower() == requestedNorm) {
                return name;
            }
        }

        // Smart fallback: If the requested sheet is "Report" but the SAP file uses the "MM_YYYY" sheet name format
        if (requestedNorm == "report") {
            QString monthNum = ExcelHandler::MONTH_TO_NUM.value(month, "");
            if (!monthNum.isEmpty()) {
                QString altName = QString("%1_%2").arg(monthNum).arg(year);
                for (const QString& name : m_handler->getSheetNames(key)) {
                    if (name.trimmed().toLower() == altName.toLower()) {
                        qInfo() << "TransferService: Found alternate sheet name" << altName << "instead of" << requested;
                        return name;
                    }
                }
            }
        }

        const QStringList names = m_handler->getSheetNames(key);
        if (!names.isEmpty()) {
            qWarning() << "TransferService: sheet" << requested << "not found in" << key
                       << "— falling back to" << names.first();
            return names.first();
        }
        return requested;
    };

    // Resolve {year} placeholder in source sheet template (e.g. "{year} FTE" → "2025 FTE")
    QString sourceSheetTemplate = entry.sourceSheetTemplate;
    sourceSheetTemplate.replace("{year}", QString::number(year));
    sourceSheetTemplate.replace("{year-1}", QString::number(year - 1));
    const QString sourceSheet = resolveSheet(srcKey, sourceSheetTemplate);

    if (!entry.rowMap.isEmpty()) {
        QString destSheet = entry.destSheet;
        if (sourceFileType == "traffic_mott") {
            // Ensure destination sheet exists in cost_control; do not create new sheets
            QString requestedNorm = destSheet.trimmed().toLower();
            QString found;
            for (const QString& name : m_handler->getSheetNames(destKey)) {
                if (name.trimmed().toLower() == requestedNorm) {
                    found = name;
                    break;
                }
            }
            if (found.isEmpty()) {
                qWarning() << "TransferService: dest sheet" << destSheet << "not found in" << destKey
                           << "— skipping to avoid creating new sheet.";
                return result;
            }
            destSheet = found;

            // Update traffic mott month number (Q9/Q10) in traffic file before reading values
            int monthNumber = 0;
            const QStringList months = {
                "January", "February", "March", "April", "May", "June",
                "July", "August", "September", "October", "November", "December"
            };
            for (int i = 0; i < months.size(); ++i) {
                if (entry.month.compare(months[i], Qt::CaseInsensitive) == 0) {
                    monthNumber = i + 1;
                    break;
                }
            }
            if (monthNumber > 0) {
                // Set Q9/Q10 in destination traffic sheet (cost_control)
                int colQ = m_handler->letterToColumn("Q");
                m_handler->setCellValue(destKey, destSheet, 9, colQ, monthNumber);
                m_handler->setCellValue(destKey, destSheet, 10, colQ, monthNumber);
                qDebug() << "[TRAFFIC_MOTT] set Q9/Q10 month" << monthNumber << "in" << destSheet;
            }
        }
        const int sourceColIndex = m_handler->letterToColumn(entry.sourceColumn);
        const int destColIndex = m_handler->letterToColumn(entry.destColumn);
        qDebug() << "[TRANSFER] srcKey=" << srcKey << "sourceSheet=" << sourceSheet
                 << "srcCol=" << entry.sourceColumn << "(" << sourceColIndex << ")"
                 << "destKey=" << destKey << "destSheet=" << destSheet
                 << "destCol=" << entry.destColumn << "(" << destColIndex << ")"
                 << "rowMap size=" << entry.rowMap.size();
        for (auto it = entry.rowMap.constBegin(); it != entry.rowMap.constEnd(); ++it) {
            int destRow = it.key();
            const QVector<int>& srcRowList = it.value();
            if (srcRowList.isEmpty()) continue;

            // Skip rows the user has marked to ignore
            if (entry.ignoredDestRows.contains(destRow)) continue;

            double total = 0.0;
            for (int srcRow : srcRowList) {
                QVariant value = m_handler->getCellValue(srcKey, sourceSheet, srcRow, sourceColIndex);
                if (value.canConvert<double>())
                    total += value.toDouble();
            }

            bool shouldDivide = divideBy1000;
            if (shouldDivide) {
                if (sourceFileType == "pax") {
                    const bool paxDivide = (destRow >= 5 && destRow <= 7);
                    if (paxDivide) {
                        total /= 1000.0;
                        total = roundTo5(total);
                    }
                } else if ((sourceFileType == "sap" || sourceFileType == "sap_ytd")
                           && (destRow == 212 || destRow == 216 || destRow == 218 || destRow == 214
                               || destRow == 222 || destRow == 226)) {
                    // Rows 212-226 PAX/EUR section must not be sign-flipped
                    total /= 1000.0;
                    total = roundTo5(total);
                } else {
                    total /= -1000.0;
                    total = roundTo5(total);
                }
            }

            m_handler->setCellValue(destKey, destSheet, destRow, destColIndex, total);
            result.cellsTransferred++;
        }

        // Q column in TRAFFIC mott 2025 sheet contains Excel SUM formulas — leave them alone.
        // JG values in MZLZ Consolidated are now computed directly from the external
        // TRAFFIC mott xlsx by summing G..currentMonthCol in handleSapYtd.
    } else if (!srcRows.isEmpty()) {
        if (srcRows.size() != destRows.size()) {
            qWarning() << "TransferService: row count mismatch for" << entry.sourceSheetTemplate
                       << "src:" << srcRows.size() << "dest:" << destRows.size()
                       << "— transferring up to min count";
            // Trim to smaller of the two — transfer what we can
            int minCount = qMin(srcRows.size(), destRows.size());
            srcRows  = srcRows.mid(0, minCount);
            destRows = destRows.mid(0, minCount);
        }
        {
            result.cellsTransferred = m_handler->transferData(srcKey,
                                                              sourceSheet,
                                                              entry.sourceColumn,
                                                              srcRows,
                                                              destKey,
                                                              entry.destSheet,
                                                              entry.destColumn,
                                                              destRows,
                                                              sourceFileType,
                                                              divideBy1000);
        }
    }

    // Update cumulative columns IP-IZ for MZLZ Consolidated (Jan-Nov)
    if (entry.destSheet == "MZLZ Consolidated") {
        static const QStringList monthOrder = {
            "January", "February", "March", "April", "May", "June",
            "July", "August", "September", "October", "November", "December"
        };
        static const QMap<QString, QString> baseCols = {
            {"January", "G"}, {"February", "W"}, {"March", "AM"}, {"April", "BD"},
            {"May", "BW"}, {"June", "CP"}, {"July", "DJ"}, {"August", "EF"},
            {"September", "FB"}, {"October", "FY"}, {"November", "GX"}, {"December", "HW"}
        };
        static const QMap<QString, QString> budgetBaseCols = {
            {"January", "F"}, {"February", "V"}, {"March", "AL"}, {"April", "BB"},
            {"May", "BU"}, {"June", "CN"}, {"July", "DG"}, {"August", "EC"},
            {"September", "EY"}, {"October", "FU"}, {"November", "GT"}, {"December", "HS"}
        };
        static const QMap<QString, QString> cumCols = {
            {"January", "IP"}, {"February", "IQ"}, {"March", "IR"}, {"April", "IS"},
            {"May", "IT"}, {"June", "IU"}, {"July", "IV"}, {"August", "IW"},
            {"September", "IX"}, {"October", "IY"}, {"November", "IZ"}
        };

        int monthIdx = monthOrder.indexOf(month);
        QString expectedBase = baseCols.value(month);
        if (monthIdx >= 0 && !expectedBase.isEmpty() && entry.destColumn == expectedBase) {
            int sumEnd = (monthIdx == 0) ? 0 : monthIdx - 1; // month-1 cumulative (Jan uses Jan)
            QSet<int> rowsToSum;
            if (!entry.rowMap.isEmpty()) {
                for (auto it = entry.rowMap.constBegin(); it != entry.rowMap.constEnd(); ++it) {
                    rowsToSum.insert(it.key());
                }
            } else {
                rowsToSum = QSet<int>(entry.destRows.begin(), entry.destRows.end());
            }
            for (int extraRow : entry.cumulativeRows) {
                rowsToSum.insert(extraRow);
            }
            // Always include rows 118–123 in cumulative sums
            for (int r = 115; r <= 123; ++r) rowsToSum.insert(r);
            // Rows 16 and 18 are transfer-only — never include in cumulative IP–IZ sums
            rowsToSum.remove(16);
            rowsToSum.remove(18);

            for (int row : rowsToSum) {
                double cumulative = 0.0;
                for (int i = 0; i <= sumEnd; ++i) {
                    const QString& m = monthOrder[i];
                    QString baseCol = baseCols.value(m);
                    if (row >= 115 && row <= 123 && !budgetBaseCols.value(m).isEmpty()
                        && entry.destColumn != expectedBase) {
                        baseCol = budgetBaseCols.value(m);
                    }
                    if (baseCol.isEmpty()) continue;

                    QVariant raw = m_handler->getCellValue(destKey, entry.destSheet, row,
                                                          m_handler->letterToColumn(baseCol));
                    bool ok = false;
                    double val = raw.toDouble(&ok);
                    if (!ok) {
                        val = 0.0;
                        // DO NOT write 0 back to the cell — that overwrites formula cells
                        // in months we didn't select, causing the Excel repair dialog.
                    }
                    if (row >= 115 && row <= 123) {
                        val = roundTo5(val);
                    }
                    if (row == 115) {
                        qDebug() << "Cum row118" << m << baseCol << "=" << val;
                    }
                    cumulative += val;
                }

                // Write to cumulative column of the previous month (month-1), except January
                int writeIdx = (monthIdx == 0) ? 0 : monthIdx - 1;
                const QString cumCol = cumCols.value(monthOrder[writeIdx]);
                if (!cumCol.isEmpty()) {
                    cumulative = roundTo5(cumulative);
                    if (row == 115) {
                        qDebug() << "Cum row118 write" << cumCol << "=" << cumulative;
                    }
                    m_handler->setCellValue(destKey, entry.destSheet, row,
                                            m_handler->letterToColumn(cumCol), cumulative);
                }
            }
        }
    }

    // Optional: copy full sheet if user explicitly enabled it for this mapping
    if (entry.copyFullSheet) { 
        if (sourceSheet.isEmpty()) {
            qWarning() << "TransferService: copyFullSheet requested but sourceSheetTemplate empty";
        } else {
            const QString targetName = entry.customSheetName.isEmpty() ? sourceSheet : entry.customSheetName;
            bool ok = m_handler->copyFullSheet(srcKey, sourceSheet, destKey, targetName, entry.sourceRows);
            if (ok && !entry.insertAfterSheet.isEmpty()) {
                m_handler->setInsertAfter(destKey, targetName, entry.insertAfterSheet);
            }
            if (!ok) {
                qWarning() << "TransferService: copyFullSheet failed for" << sourceSheet;
            } else {
                result.usedComCopy = true;
            }
        }
    }

    // Saving is handled by TransferWorker after all mappings finish

    qDebug() << "transferEntry END" << entry.sourceSheetTemplate << "cells" << result.cellsTransferred
             << "elapsed" << timer.elapsed() << "ms";
    return result;
}

TransferService::Result TransferService::handleSapYtd(const MappingEntry& entry,
                                                      int year,
                                                      const QString& destKey,
                                                      const QString& destFilePath,
                                                      const QString& baseFolder) {
    Result result;
    QString srcKey = QString("%1_%2_sap_ytd").arg(entry.month).arg(year);
    QString srcFile = !entry.sourcePath.trimmed().isEmpty()
                          ? entry.sourcePath
                          : m_handler->findSapYtdFile(baseFolder, entry.month, year);
    if (srcFile.isEmpty()) {
        qWarning() << "SAP YTD file missing for" << entry.month << year;
        result.error = "SAP YTD file missing";
        return result;
    }
    // Guard: only load if not already pre-loaded by TransferWorker Phase 1
    if (!m_handler->isLoaded(srcKey)) {
        QString loadError;
        if (!m_handler->loadWorkbook(srcFile, srcKey, {}, &loadError)) {
            if (loadError.isEmpty()) loadError = "unknown workbook load error";
            result.error = QString("Failed to load SAP YTD workbook: %1 (%2)")
                               .arg(srcFile, loadError);
            qWarning() << result.error;
            return result;
        }
    }

    const QString sourceSheet = entry.sourceSheetTemplate.isEmpty() ? "Report" : entry.sourceSheetTemplate;
    const QString destSheet = entry.destSheet.isEmpty() ? "MZLZ Consolidated" : entry.destSheet;
    const QString sourceColumn = entry.sourceColumn.isEmpty() ? "C" : entry.sourceColumn;
    const QString destColumn = entry.destColumn.isEmpty() ? "JG" : entry.destColumn;

    for (auto it = entry.rowMap.constBegin(); it != entry.rowMap.constEnd(); ++it) {
        int destRow = it.key();
        const QVector<int>& srcRows = it.value();
        double total = 0.0;
        bool hasValue = false;

        if (srcRows.size() == 1 && srcRows.first() == 0) {
            total = 0.0;
            hasValue = true;
        } else {
            for (int srcRow : srcRows) {
                QVariant value = m_handler->getCellValue(srcKey, sourceSheet, srcRow,
                                                         m_handler->letterToColumn(sourceColumn));
                if (value.canConvert<double>()) {
                    total += value.toDouble();
                    hasValue = true;
                }
            }
        }

        if (!hasValue) continue;
        // JG: rows 212/216/218/222/226 (PAX/EUR section) divide by +1000 (no sign flip); all other rows by -1000
        if (destColumn == "JG") {
            if (destRow == 212 || destRow == 216 || destRow == 218
                || destRow == 222 || destRow == 226) {
                total /= 1000.0;
            } else {
                total /= -1000.0;
            }
            total = roundTo5(total);
        }
        m_handler->setCellValue(destKey, destSheet, destRow,
                                m_handler->letterToColumn(destColumn), total);
        result.cellsTransferred++;
    }

    // ── Traffic mott YTD additions ─────────────────────────────────────────────
    // Reads Q column from TRAFFIC mott sheet in cost_control (destKey).
    // The Q column contains Excel SUM formulas that must be recalculated before
    // reading. We use COM to open the file, auto-repair if needed, force recalc,
    // save, then read via OpenXML.
    //
    // PREVIOUS APPROACH (C++ manual sum from PAX file) — kept for reference:
    // -------------------------------------------------------------------------
    // We previously summed PAX Sheet1 columns G..currentMonth in C++ to get the
    // YTD total, avoiding formula dependency. This worked but required knowing the
    // exact PAX row numbers and column layout. Replaced by COM recalc below.
    // -------------------------------------------------------------------------
    if (!entry.trafficPaxRowMap.isEmpty()) {

        const QString trafficDestCol = entry.trafficDestColumn.isEmpty() ? destColumn : entry.trafficDestColumn;
        const int trafficDestColIndex = m_handler->letterToColumn(trafficDestCol);

        {
            // PAX source file key — pre-loaded as "Month_year_pax"
            // This is the external TRAFFIC mott {year}.xlsx in the Traffic folder.
            // Sheet1 contains single-month values per column (G=Jan, H=Feb, ... col=currentMonth).
            // It does NOT contain YTD — YTD lives in cost_control's TRAFFIC mott sheet col Q.
            //
            // Strategy:
            //   1. Read existing col Q from cost_control TRAFFIC mott sheet
            //      (= correct YTD through previous month, already saved from prior transfers)
            //   2. Read current month's value from external TRAFFIC mott xlsx Sheet1
            //      (= single month value for the selected month)
            //   3. new YTD = existingQ + currentMonthValue
            //   4. Write new YTD → MZLZ JG

            QString paxKey = QString("%1_%2_pax").arg(entry.month).arg(year);
            const QString paxColStr = entry.trafficSourceColumn; // e.g. "P" for October
            if (!paxColStr.isEmpty() && m_handler->isLoaded(paxKey)) {
                const int paxColIdx = m_handler->letterToColumn(paxColStr);
                const QString paxSheet = "Sheet1";
                const int colQ = m_handler->letterToColumn("Q");

                // Find TRAFFIC mott sheet in cost_control (for reading existing Q)
                QString trafficMottSheet;
                for (const QString& name : m_handler->getSheetNames(destKey)) {
                    if (name.trimmed().toLower().contains("traffic mott")) {
                        trafficMottSheet = name;
                        break;
                    }
                }
                {
                    // Compute YTD by summing all months Jan..currentMonth from the
                    // external TRAFFIC mott {year}.xlsx Sheet1.
                    // Each column G=Jan, H=Feb, ... currentMonthCol=currentMonth
                    // contains a single month's value for that row.
                    // Sum them all in C++ → correct YTD, no COM recalc needed.

                    const int colG = m_handler->letterToColumn("G"); // January = col 7

                    qDebug() << "[YTD-TRAFFIC] Summing Jan.." << paxColStr
                             << "from" << paxKey << paxSheet
                             << "paxRowMap=" << entry.trafficPaxRowMap;

                    // Monthly dest column for MZLZ Consolidated (G=Jan, W=Feb, AM=Mar, ...)
                    static const QMap<QString,QString> monthlyDestCols = {
                        {"January","G"},   {"February","W"},  {"March","AM"},
                        {"April","BD"},    {"May","BW"},      {"June","CP"},
                        {"July","DJ"},     {"August","EF"},   {"September","FB"},
                        {"October","FY"},  {"November","GX"}, {"December","HW"}
                    };
                    const QString monthlyColStr = monthlyDestCols.value(entry.month);
                    const int monthlyColIndex = monthlyColStr.isEmpty()
                                                ? -1
                                                : m_handler->letterToColumn(monthlyColStr);

                    for (auto it = entry.trafficPaxRowMap.constBegin();
                         it != entry.trafficPaxRowMap.constEnd(); ++it) {
                        int mzlzRow = it.key();   // MZLZ Consolidated dest row
                        int paxRow  = it.value(); // PAX Sheet1 source row

                        // Sum all months Jan..currentMonth from PAX Sheet1
                        double ytdSum = 0.0;
                        for (int col = colG; col <= paxColIdx; ++col) {
                            QVariant v = m_handler->getCellValue(paxKey, paxSheet, paxRow, col);
                            if (v.canConvert<double>()) ytdSum += v.toDouble();
                        }

                        // Apply ÷1000 for PAX rows (MZLZ rows 5 and 7)
                        double value = ytdSum;
                        if (mzlzRow == 5 || mzlzRow == 7) {
                            value /= 1000.0;
                            value = roundTo5(value);
                        }

                        qInfo() << "[STATUS] YTD-TRAFFIC paxRow" << paxRow
                                << "sumG.." << paxColStr << "=" << ytdSum
                                << (mzlzRow == 5 || mzlzRow == 7 ? "(÷1000)" : "")
                                << "=>" << value
                                << "-> MZLZ row" << mzlzRow
                                << "col" << trafficDestCol;

                        // Write YTD value → JG column
                        m_handler->setCellValue(destKey, destSheet, mzlzRow, trafficDestColIndex, value);
                        result.cellsTransferred++;

                        // Also write the current month's single value → monthly column (G/W/AM/...)
                        // This overwrites cross-sheet formulas like =+'TRAFFIC mott 2025'!$G$81
                        // that would otherwise keep referencing the wrong year.
                        if (monthlyColIndex > 0) {
                            QVariant monthVal = m_handler->getCellValue(paxKey, paxSheet, paxRow, paxColIdx);
                            double singleMonth = monthVal.canConvert<double>() ? monthVal.toDouble() : 0.0;
                            if (mzlzRow == 5 || mzlzRow == 7) {
                                singleMonth /= 1000.0;
                                singleMonth = roundTo5(singleMonth);
                            }
                            m_handler->setCellValue(destKey, destSheet, mzlzRow, monthlyColIndex, singleMonth);
                            result.cellsTransferred++;
                        }
                    }
                }
                // Write FTE value from {year} FTE sheet row 33 → MZLZ row 16, col JG
                // Source column matches month_mappings in staff.json: G=Jan,H=Feb,...R=Dec
                // This runs alongside the YTD traffic write, using the same destKey/destSheet.
                {
                    static const QMap<QString,QString> fteSrcCol = {
                        {"January","D"}, {"February","E"}, {"March","F"}, {"April","G"},
                        {"May","H"}, {"June","I"}, {"July","J"}, {"August","K"},
                        {"September","L"}, {"October","M"}, {"November","N"}, {"December","O"}
                    };
                    QString fteColStr = fteSrcCol.value(entry.month);
                    if (!fteColStr.isEmpty()) {
                        // Find the staff file loaded as "{month}_{year}_staff"
                        QString staffKey = QString("%1_%2_staff").arg(entry.month).arg(year);
                        // Resolve the FTE sheet name (may be "2025 FTE", "2026 FTE" etc.)
                        QString fteSheet;
                        QString fteSheetTemplate = QString("%1 FTE").arg(year);
                        if (m_handler->isLoaded(staffKey)) {
                            for (const QString& name : m_handler->getSheetNames(staffKey)) {
                                if (name.trimmed().toLower() == fteSheetTemplate.toLower()) {
                                    fteSheet = name;
                                    break;
                                }
                            }
                        }
                        if (!fteSheet.isEmpty()) {
                            int fteCol = m_handler->letterToColumn(fteColStr);
                            QVariant fteVal = m_handler->getCellValue(staffKey, fteSheet, 33, fteCol);
                            if (fteVal.canConvert<double>()) {
                                double fteValue = std::round(fteVal.toDouble());
                                m_handler->setCellValue(destKey, destSheet, 16,
                                                        trafficDestColIndex, fteValue);
                                result.cellsTransferred++;
                                qInfo() << "[STATUS] FTE row33 col" << fteColStr
                                        << "=" << fteValue << "-> MZLZ row 16 col" << trafficDestCol;
                            }
                        } else {
                            qWarning() << "[YTD-FTE] Sheet" << fteSheetTemplate
                                       << "not found in" << staffKey << "— skipping FTE write";
                        }
                    }
                }

                // PAX path done — skip legacy Q-column path
                return result;
            }
        }
    }

    return result;
}

TransferService::Result TransferService::handleYtd(const MappingEntry& entry,
                                                   int year,
                                                   const QString& destKey,
                                                   const QString& destFilePath) {
    Q_UNUSED(year);
    Result result;

    const QMap<QString, QPair<QString, QString>> monthToYtdCol = {
        {"January", {"IP", ""}}, {"February", {"IQ", "IP"}}, {"March", {"IR", "IQ"}},
        {"April", {"IS", "IR"}}, {"May", {"IT", "IS"}}, {"June", {"IU", "IT"}},
        {"July", {"IV", "IU"}}, {"August", {"IW", "IV"}}, {"September", {"IX", "IW"}},
        {"October", {"IY", "IX"}}, {"November", {"IZ", "IY"}}, {"December", {"", ""}}
    };

    const QMap<QString, QString> monthToDestCol = {
        {"January", "G"}, {"February", "W"}, {"March", "AM"}, {"April", "BD"},
        {"May", "BW"}, {"June", "CP"}, {"July", "DJ"}, {"August", "EF"},
        {"September", "FB"}, {"October", "FY"}, {"November", "GX"}, {"December", "HW"}
    };

    auto ytdCols = monthToYtdCol.value(entry.month, {"", ""});
    QString ytdCol = ytdCols.first;
    QString prevCol = ytdCols.second;
    if (ytdCol.isEmpty()) {
        return result;
    }

    QString currCol = monthToDestCol.value(entry.month, "G");
    const QVector<int> destRows = entry.destRows;

    for (int destRow : destRows) {
        double currVal = 0.0;
        double prevVal = 0.0;
        QVariant currRaw = m_handler->getCellValue(destKey, entry.destSheet, destRow,
                                                   m_handler->letterToColumn(currCol));
        if (currRaw.canConvert<double>()) {
            currVal = currRaw.toDouble();
        }
        if (!prevCol.isEmpty()) {
            QVariant prevRaw = m_handler->getCellValue(destKey, entry.destSheet, destRow,
                                                       m_handler->letterToColumn(prevCol));
            if (prevRaw.canConvert<double>()) {
                prevVal = prevRaw.toDouble();
            }
        }
        double ytdVal = currVal + prevVal;
        m_handler->setCellValue(destKey, entry.destSheet, destRow,
                                m_handler->letterToColumn(ytdCol), ytdVal);
        result.cellsTransferred++;
    }

    return result;
}
