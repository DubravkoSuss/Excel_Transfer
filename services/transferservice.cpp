#include "transferservice.h"
#include <QElapsedTimer>
#include "excelhandler.h"
#include <QRegularExpression>
#include <QDate>
#include <cmath>

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

    // Only load if not already in cache (prevents redundant ZIP extractions)
    if (srcFilePath.isEmpty()) {
        qWarning() << "Source file missing for" << sourceFileType << "(" << month << year << ")";
    } else if (!m_handler->isLoaded(srcKey)) {
        m_handler->loadWorkbook(srcFilePath, srcKey);
    }

    if (!entry.rowMap.isEmpty()) {
        auto resolveSheet = [&](const QString& key, const QString& requested) {
            if (requested.isEmpty()) return requested;
            QString requestedNorm = requested.trimmed().toLower();
            for (const QString& name : m_handler->getSheetNames(key)) {
                if (name.trimmed().toLower() == requestedNorm) {
                    return name;
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

        const QString sourceSheet = resolveSheet(srcKey, entry.sourceSheetTemplate);
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

            double total = 0.0;
            for (int srcRow : srcRowList) {
                QVariant value = m_handler->getCellValue(srcKey, sourceSheet, srcRow, sourceColIndex);
                if (value.canConvert<double>()) {
                    total += value.toDouble();
                }
            }

            bool shouldDivide = divideBy1000;
            if (shouldDivide) {
                if (sourceFileType == "pax") {
                    const bool paxDivide = (destRow >= 5 && destRow <= 7);
                    if (paxDivide) {
                        total /= 1000.0;
                    }
                } else if (sourceFileType == "sap_ytd" && (destRow == 212 || destRow == 216)) {
                    total /= 1000.0;
                } else {
                    total /= -1000.0;
                }
            }

            total = std::round(total);
            m_handler->setCellValue(destKey, destSheet, destRow, destColIndex, total);
            result.cellsTransferred++;
        }

        // After writing all monthly columns for traffic_mott, compute and write Q column
        // as static YTD sums (D through current month's column) so sap_ytd can read them.
        // This replaces the Excel formula result without needing Excel to recalculate.
        // TRAFFIC mott dest columns: Jan=D, Feb=E, Mar=F, Apr=G, May=H, Jun=I,
        //                            Jul=J, Aug=K, Sep=L, Oct=M, Nov=N, Dec=O
        if (isTrafficMott) {
            static const QStringList tmMonthCols = {
                "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O"
            };
            static const QStringList tmMonthNames = {
                "January", "February", "March", "April", "May", "June",
                "July", "August", "September", "October", "November", "December"
            };
            int currentMonthIdx = -1;
            for (int m = 0; m < tmMonthNames.size(); ++m) {
                if (entry.month.compare(tmMonthNames[m], Qt::CaseInsensitive) == 0) {
                    currentMonthIdx = m;
                    break;
                }
            }
            if (currentMonthIdx >= 0) {
                const int colQ = m_handler->letterToColumn("Q");

                // Helper lambda: compute D..currentMonth YTD sum for any row and write to Q
                auto computeAndWriteQ = [&](int row) {
                    double ytdSum = 0.0;
                    for (int m = 0; m <= currentMonthIdx; ++m) {
                        int colIdx = m_handler->letterToColumn(tmMonthCols[m]);
                        QVariant raw = m_handler->getCellValue(destKey, destSheet, row, colIdx);
                        if (raw.canConvert<double>())
                            ytdSum += raw.toDouble();
                    }
                    ytdSum = std::round(ytdSum);
                    m_handler->setCellValue(destKey, destSheet, row, colQ, ytdSum);
                    qDebug() << "[TRAFFIC_MOTT Q]" << destSheet << "row" << row
                             << "D.." << tmMonthCols[currentMonthIdx] << "= " << ytdSum;
                };

                // Step 1: compute Q for all detail rows written by the rowMap.
                // rowMap is QMap<int, QVector<int>>: { destRow (TRAFFIC mott) → srcRows (PAX) }
                // Keys ARE the dest rows in TRAFFIC mott 2025 — correct to use it.key().
                QSet<int> writtenRows;
                for (auto it = entry.rowMap.constBegin(); it != entry.rowMap.constEnd(); ++it) {
                    int r = it.key();
                    if (r > 0) writtenRows.insert(r);
                }
                for (int destRow : writtenRows)
                    computeAndWriteQ(destRow);

                // Step 2: compute Q for the total/summary rows that sap_ytd reads.
                // These rows contain Excel SUM formulas in the real file (never recalculated
                // via OpenXML), so we must compute them here as static values too.
                // Row mapping (TRAFFIC mott 2025 → MZLZ Consolidated JG):
                //   19 → Total passengers
                //   35 → Departing total passengers
                //   81 → Total movements
                //   96 → Departing total movements
                //   132 → Landed total tonnage
                //   159 → Total tonnage
                static const QVector<int> totalRows = {19, 35, 81, 96, 132, 159};
                for (int totalRow : totalRows) {
                    if (!writtenRows.contains(totalRow))  // avoid double-computing
                        computeAndWriteQ(totalRow);
                }

                qDebug() << "[TRAFFIC_MOTT] wrote Q YTD sums for"
                         << writtenRows.size() << "detail rows +"
                         << totalRows.size() << "total rows, month=" << entry.month
                         << "(D.." << tmMonthCols[currentMonthIdx] << ")";
            }
        }
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
                                                              entry.sourceSheetTemplate,
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
                        m_handler->setCellValue(destKey, entry.destSheet, row,
                                                m_handler->letterToColumn(baseCol), 0.0);
                    }
                    if (row >= 115 && row <= 123) {
                        val = std::round(val);
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
        const QString sourceSheet = entry.sourceSheetTemplate;
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
    QString srcFile = m_handler->findSapYtdFile(baseFolder, entry.month, year);
    if (srcFile.isEmpty()) {
        qWarning() << "SAP YTD file missing for" << entry.month << year;
        result.error = "SAP YTD file missing";
        return result;
    }
    // Guard: only load if not already pre-loaded by TransferWorker Phase 1
    if (!m_handler->isLoaded(srcKey)) {
        m_handler->loadWorkbook(srcFile, srcKey);
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
        // JG should be sign-flipped only (no /1000)
        if (destColumn == "JG") {
            if (destRow == 216) {
                total /= 1000.0; // keep positive for row 216
            } else {
                total /= -1000.0;
            }
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
    if (!entry.trafficSourceRows.isEmpty() && entry.trafficSourceRows.size() == entry.trafficDestRows.size()) {

        // Step 1: Force Excel to recalculate the cost_control file via COM.
        // This recalculates the Q column SUM formulas in TRAFFIC mott sheet and saves.
        // DisplayAlerts=false + CorruptLoad=xlRepairFile suppresses all dialogs
        // including the "we found a problem, do you want to repair?" popup.
        qDebug() << "[YTD-TRAFFIC] Triggering COM recalc on:" << destFilePath;
        QString comError;
        bool recalcOk = ExcelHandler::recalcWithCOM(destFilePath, &comError);
        if (!recalcOk) {
            qWarning() << "[YTD-TRAFFIC] COM recalc failed:" << comError
                       << "— falling back to reading potentially stale Q values";
        } else {
            // Reload the workbook from disk so we get the freshly recalculated values
            m_handler->unloadWorkbook(destKey);
            m_handler->loadWorkbook(destFilePath, destKey);
            qDebug() << "[YTD-TRAFFIC] Workbook reloaded with fresh formula results";
        }

        // Step 2: Find the TRAFFIC mott sheet in the (now up-to-date) cost_control workbook
        QString trafficMottSheet;
        for (const QString& name : m_handler->getSheetNames(destKey)) {
            if (name.trimmed().toLower().contains("traffic mott")) {
                trafficMottSheet = name;
                break;
            }
        }
        if (trafficMottSheet.isEmpty()) {
            qWarning() << "[YTD-TRAFFIC] TRAFFIC mott sheet not found in" << destKey;
        } else {
            const QString trafficDestCol = entry.trafficDestColumn.isEmpty() ? destColumn : entry.trafficDestColumn;
            const int trafficDestColIndex = m_handler->letterToColumn(trafficDestCol);
            const int colQ = m_handler->letterToColumn("Q");

            qDebug() << "[YTD-TRAFFIC] Reading Q column from" << trafficMottSheet
                     << "rows=" << entry.trafficSourceRows;

            // Step 3: Read Q column values (now correct after COM recalc) and write to MZLZ JG
            for (int i = 0; i < entry.trafficSourceRows.size(); ++i) {
                int srcRow  = entry.trafficSourceRows[i];   // row in TRAFFIC mott sheet
                int mzlzRow = entry.trafficDestRows[i];     // row in MZLZ Consolidated

                QVariant raw = m_handler->getCellValue(destKey, trafficMottSheet, srcRow, colQ);
                if (!raw.canConvert<double>()) {
                    qDebug() << "[YTD-TRAFFIC] Q" << srcRow << "not numeric, skipping";
                    continue;
                }
                double value = raw.toDouble();

                // Rows 5 (Total PAX) and 7 (Dep total PAX): Q column is already in
                // thousands in the TRAFFIC mott sheet — divide by 1000 for MZLZ.
                // Rows 8,9,10,11 (movements, tonnage): copy as-is.
                if (mzlzRow == 5 || mzlzRow == 7) {
                    value /= 1000.0;
                }
                value = std::round(value);

                qDebug() << "[YTD-TRAFFIC] TRAFFIC mott row" << srcRow
                         << "Q=" << raw.toDouble()
                         << (mzlzRow == 5 || mzlzRow == 7 ? "(÷1000)" : "")
                         << "=" << value << "-> MZLZ row" << mzlzRow << "col" << trafficDestCol;

                m_handler->setCellValue(destKey, destSheet, mzlzRow, trafficDestColIndex, value);
                result.cellsTransferred++;
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
