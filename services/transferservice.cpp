#include "transferservice.h"
#include <QElapsedTimer>
#include "excelhandler.h"
#include <QRegularExpression>
#include <QDate>
#include <cmath>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QFile>

// Round to 5 decimal places
static double roundTo5(double v) {
    return std::round(v * 100000.0) / 100000.0;
}

// Evaluate a simple Excel formula by reading cell values from the workbook.
// Supports: =SUM(X1:X5), =SUM(X1,X3,X5), =X1+X2-X3, and trailing /constant suffix.
// Sets *ok=true only when the formula was fully understood; callers must NOT trust
// the return value when *ok==false (unsupported functions, cross-sheet refs, etc.).
static double evaluateSimpleFormula(ExcelHandler* handler, const QString& key,
                                    const QString& sheet, const QString& formula,
                                    bool* ok = nullptr)
{
    if (!handler || formula.isEmpty()) { if (ok) *ok = false; return 0.0; }

    QString f = formula.trimmed();
    if (f.startsWith('=')) f = f.mid(1);
    f.remove('$');
    if (f.startsWith('+')) f = f.mid(1);
    f = f.trimmed();

    if (f.isEmpty()) { if (ok) *ok = false; return 0.0; }

    // ── Allowlist gate ──────────────────────────────────────────────────────
    // Reject cross-sheet references (contain '!')
    if (f.contains('!')) { if (ok) *ok = false; return 0.0; }

    // Reject any function call that isn't SUM — allowlist approach.
    // Matches: IFERROR(, SUMPRODUCT(, VLOOKUP(, COUNTA(, etc.
    // Does NOT match: SUM( (negative lookahead).
    static const QRegularExpression nonSumFuncRx(
        "(?!SUM\\b)[A-Z_][A-Z0-9_.]*\\s*\\(",
        QRegularExpression::CaseInsensitiveOption);
    if (nonSumFuncRx.match(f).hasMatch()) { if (ok) *ok = false; return 0.0; }

    // Reject stray parentheses that aren't part of SUM(...)
    // e.g. =(A1+A2)/1000 or =A1*(A2>0) — we don't handle grouping or comparisons
    {
        QString withoutSum = f;
        static const QRegularExpression sumStripRx("SUM\\([^)]*\\)", QRegularExpression::CaseInsensitiveOption);
        withoutSum.remove(sumStripRx);
        if (withoutSum.contains('(') || withoutSum.contains(')')) {
            if (ok) *ok = false; return 0.0;
        }
    }

    // ── Parse trailing /constant divisor ────────────────────────────────────
    double divisor = 1.0;
    static const QRegularExpression divRx("/\\s*(-?[0-9.]+)\\s*$");
    auto divMatch = divRx.match(f);
    if (divMatch.hasMatch()) {
        divisor = divMatch.captured(1).toDouble();
        if (divisor == 0) divisor = 1.0;
        f = f.left(divMatch.capturedStart()).trimmed();
    }

    // ── Cell-ref validator (strict) ─────────────────────────────────────────
    static const QRegularExpression cellRefRx("^([A-Z]+)(\\d+)$", QRegularExpression::CaseInsensitiveOption);

    auto readCell = [&](const QString& cellRef, bool* cellOk) -> double {
        auto m = cellRefRx.match(cellRef.trimmed());
        if (!m.hasMatch()) { if (cellOk) *cellOk = false; return 0.0; }
        if (cellOk) *cellOk = true;
        QString col = m.captured(1);
        int row = m.captured(2).toInt();
        QVariant val = handler->getCellValue(key, sheet, row, handler->letterToColumn(col));
        return val.canConvert<double>() ? val.toDouble() : 0.0;
    };

    auto sumRange = [&](const QString& rangeStr, bool* rangeOk) -> double {
        static const QRegularExpression rangeRx("^([A-Z]+)(\\d+):([A-Z]+)(\\d+)$", QRegularExpression::CaseInsensitiveOption);
        auto m = rangeRx.match(rangeStr.trimmed());
        if (!m.hasMatch()) return readCell(rangeStr, rangeOk);
        if (rangeOk) *rangeOk = true;

        QString col1 = m.captured(1).toUpper(), col2 = m.captured(3).toUpper();
        int row1 = m.captured(2).toInt(), row2 = m.captured(4).toInt();
        double total = 0.0;

        if (col1 == col2) {
            int c = handler->letterToColumn(col1);
            for (int r = qMin(row1, row2); r <= qMax(row1, row2); ++r) {
                QVariant v = handler->getCellValue(key, sheet, r, c);
                if (v.canConvert<double>()) total += v.toDouble();
            }
        } else if (row1 == row2) {
            int c1 = handler->letterToColumn(col1), c2 = handler->letterToColumn(col2);
            for (int c = qMin(c1, c2); c <= qMax(c1, c2); ++c) {
                QVariant v = handler->getCellValue(key, sheet, row1, c);
                if (v.canConvert<double>()) total += v.toDouble();
            }
        }
        return total;
    };

    // ── evaluateToken: evaluate a single operand token ──────────────────────
    // Handles:  SUM(range,...)   |  CellRef   |  Numeric literal
    auto evaluateToken = [&](const QString& tok, bool* tokOk) -> double {
        QString t = tok.trimmed();
        if (t.isEmpty()) { if (tokOk) *tokOk = true; return 0.0; }

        // SUM(...)
        static const QRegularExpression sumTokenRx("^SUM\\((.+)\\)$",
            QRegularExpression::CaseInsensitiveOption);
        auto sm = sumTokenRx.match(t);
        if (sm.hasMatch()) {
            if (tokOk) *tokOk = true;
            double s = 0.0;
            QStringList parts = sm.captured(1).split(',');
            for (const QString& part : parts) {
                bool pok = false;
                s += sumRange(part.trimmed(), &pok);
                if (!pok) { if (tokOk) *tokOk = false; return 0.0; }
            }
            return s;
        }

        // Numeric literal
        bool numOk = false;
        double num = t.toDouble(&numOk);
        if (numOk) { if (tokOk) *tokOk = true; return num; }

        // Cell reference
        bool cellOk = false;
        double v = readCell(t, &cellOk);
        if (tokOk) *tokOk = cellOk;
        return v;
    };

    // ── Arithmetic tokenizer: scan for operators OUTSIDE parentheses ─────────
    // This handles =SUM(A1:A5)+SUM(B1:B3)-C6  and  =A1+B2-SUM(C1:C5)  etc.
    double resultValue = 0.0;
    {
        int sign = 1;
        int depth = 0;       // parenthesis depth — don't split on +/- inside SUM(...)
        QString token;

        for (int i = 0; i <= f.size(); ++i) {
            QChar ch = (i < f.size()) ? f[i] : QChar('+'); // sentinel to flush last token

            if (ch == '(') {
                ++depth;
                token += ch;
            } else if (ch == ')') {
                --depth;
                token += ch;
            } else if ((ch == '+' || ch == '-') && depth == 0) {
                // Flush accumulated token
                token = token.trimmed();
                if (!token.isEmpty()) {
                    bool tokOk = false;
                    double v = evaluateToken(token, &tokOk);
                    if (!tokOk) { if (ok) *ok = false; return 0.0; }
                    resultValue += sign * v;
                }
                sign = (ch == '+') ? 1 : -1;
                token.clear();
            } else {
                token += ch;
            }
        }
    }

    if (ok) *ok = true;
    return (divisor != 1.0) ? (resultValue / divisor) : resultValue;
}

TransferService::TransferService(ExcelHandler* handler, QObject* parent)
    : QObject(parent), m_handler(handler) 
{
    loadSubtotals();
}

void TransferService::loadSubtotals()
{
    QString configPath = QCoreApplication::applicationDirPath() + "/config/mzlz_subtotals.json";
    QFile file(configPath);
    if (!file.exists()) {
        configPath = "C:/Users/dposavac/CLionProjects/Excel_transfer/config/mzlz_subtotals.json";
        file.setFileName(configPath);
    }

    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        QJsonObject obj = doc.object();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            m_subtotalsMap.insert(it.key().toInt(), it.value().toString());
        }
        qDebug() << "[TransferService] Loaded" << m_subtotalsMap.size() << "subtotal formulas from JSON.";
    } else {
        qWarning() << "[TransferService] Could not load mzlz_subtotals.json from" << configPath;
    }
}

static const QStringList monthOrder = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};
// Index 0=Jan, 1=Feb, 2=Mar, 3=Apr, 4=May, 5=Jun, 6=Jul, 7=Aug, 8=Sep, 9=Oct, 10=Nov, 11=Dec
static const QStringList baseColOrder = {
    "G", "W", "AM", "BD", "BW", "CP", "DJ", "EF", "FB", "FY", "GX", "HW"
};
// IP=Jan(0), IQ=Feb(1), IR=Mar(2), IS=Apr(3), IT=May(4), IU=Jun(5), IV=Jul(6), IW=Aug(7), IX=Sep(8), IY=Oct(9), IZ=Nov(10)
static const QStringList cumColOrder = {
    "IP", "IQ", "IR", "IS", "IT", "IU", "IV", "IW", "IX", "IY", "IZ"
};
// Rows that should avoid horizontal accumulation (Direct Copy from base month to cumulative)
static const QSet<int> directCopyRows = { 12, 13, 16, 18 };


TransferService::Result TransferService::transferEntry(const MappingEntry& entry,
                                                       int year,
                                                       const QString& destKey,
                                                       const QString& destFilePath,
                                                       const QString& baseFolder,
                                                       bool skipCumulative) {
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
    const bool isYtdRow216 = (sourceFileType == "sap_ytd") && entry.rowMap.contains(216);

    if (sourceFileType == "sap_ytd") {
        TransferService::Result res = handleSapYtd(entry, year, destKey, destFilePath, baseFolder);
        qDebug() << "transferEntry END" << entry.sourceSheetTemplate << "cells" << res.cellsTransferred
                 << "elapsed" << timer.elapsed() << "ms";
        return res;
    }

    // handleYtd removed — cumulative columns are now computed exclusively by
    // runCumulativePass / runCumulativePassAllMonths after all data is written.

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
                // Fallback: partial match for any "traffic mott" sheet (handles year mismatch,
                // e.g. cost_control file still has "TRAFFIC mott 2025" when year=2024)
                for (const QString& name : m_handler->getSheetNames(destKey)) {
                    if (name.trimmed().toLower().contains("traffic mott")) {
                        qInfo() << "[TRAFFIC_MOTT] Exact sheet" << destSheet
                                << "not found in" << destKey
                                << "— using fallback:" << name;
                        found = name;
                        break;
                    }
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

            // Rows 12, 13, 16 are headcount rows — never divide or round by 1000
            const bool isHeadcountRow = (destRow == 12 || destRow == 13 || destRow == 16);
            bool shouldDivide = divideBy1000 && !isHeadcountRow;
            if (shouldDivide) {
                if (sourceFileType == "pax") {
                    const bool paxDivide = (destRow >= 5 && destRow <= 7);
                    if (paxDivide) {
                        total /= 1000.0;
                        total = roundTo5(total);
                    }
                } else if ((sourceFileType == "sap" || sourceFileType == "sap_ytd")
                           && (destRow >= 210 && destRow <= 226 && destRow % 2 == 0)) {
                    // Rows 210-226 (even) PAX/EUR section must not be sign-flipped
                    total /= 1000.0;
                    total = roundTo5(total);
                } else {
                    total /= -1000.0;
                    total = roundTo5(total);
                }
            }

            m_handler->setCellValue(destKey, destSheet, destRow, destColIndex, total);
            result.cellsTransferred++;

            // Q column in TRAFFIC mott 2025 sheet contains Excel SUM formulas — leave them alone.
            // JG values in MZLZ Consolidated are now computed directly from the external
            // TRAFFIC mott xlsx by summing G..currentMonthCol in handleSapYtd.
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

    // Saving is handled by TransferWorker after all mappings finish

    // ── MZLZ Consolidated Auto-Sum ───────────────────────────────────────────
    // Cumulative pass is NO LONGER run here per-mapping.
    // TransferWorker runs it ONCE after ALL mappings for a given month have
    // finished writing their base data. This prevents stale intermediate
    // values when multiple mappings target the same cumulative columns.
    // Fill All uses its own dedicated runCumulativePassAllMonths call.

    qDebug() << "transferEntry END" << entry.sourceSheetTemplate << "cells" << result.cellsTransferred
             << "elapsed" << timer.elapsed() << "ms";
    return result;
}

// ── Execute All cumulative pass ─────────────────────────────────────────────
// When the user transfers month M, the cumulative column written is for M-1.
// Example: user selects May (idx 4) → writes IS (April, idx 3) = "Reported 1-4".
// May's own cumulative (IT) is written when June is selected.
// January (idx 0): targetIdx-1 = -1 → nothing to write (no prior cumulative).
void TransferService::runCumulativePassExecuteAll(const QSet<int>& allRows,
                                                   const QString& destSheet,
                                                   int year,
                                                   const QString& destKey,
                                                   const QString& targetMonth,
                                                   const QMap<QString, QString>& monthDestKeys,
                                                   bool clearFirst)
{
    Q_UNUSED(year);
    if (!m_handler) return;

    int targetIdx = monthOrder.indexOf(targetMonth);
    if (targetIdx <= 0) {
        qDebug() << "[CUM_EXEC_ALL] No prior cumulative for month:" << targetMonth;
        return;
    }

    // Write cumulative for the PREVIOUS month (targetIdx - 1)
    const int cumIdx        = targetIdx - 1;
    const int cumColNum     = m_handler->letterToColumn(cumColOrder[cumIdx]);
    // baseColNum = base column FOR the month we are writing the cumulative of.
    // This base data lives in THAT month's own file (prevMonthDestKey).
    const int baseColNum    = m_handler->letterToColumn(baseColOrder[cumIdx]);
    const int prevCumColNum = (cumIdx > 0)
                              ? m_handler->letterToColumn(cumColOrder[cumIdx - 1])
                              : -1; // January cumulative: no prior

    // prevMonthDestKey: the destKey of the month whose base+cum we need to read.
    // e.g. writing IW (Aug cum) when targetMonth=Sep:
    //   - base EF lives in August's file
    //   - prevCum IV lives in July's file
    const QString prevMonthName     = (cumIdx >= 0 && cumIdx < monthOrder.size())
                                      ? monthOrder[cumIdx] : QString();
    const QString prevPrevMonthName = (cumIdx > 0 && (cumIdx-1) < monthOrder.size())
                                      ? monthOrder[cumIdx - 1] : QString();

    // Resolve per-month destKeys. Fall back to the current file if not provided.
    const QString baseMonthKey    = monthDestKeys.value(prevMonthName, destKey);
    const QString prevCumKey      = monthDestKeys.value(prevPrevMonthName, destKey);

    QSet<int> rowsToSum = allRows;
    for (int r : m_subtotalsMap.keys()) rowsToSum.insert(r);
    for (int r = 5; r <= 235; ++r) rowsToSum.insert(r);

    QList<int> subRows = m_subtotalsMap.keys();
    std::sort(subRows.begin(), subRows.end());

    qDebug() << "[CUM_EXEC_ALL] targetMonth=" << targetMonth
             << "writingCumFor=" << prevMonthName
             << "base=" << baseColOrder[cumIdx] << "from" << baseMonthKey
             << "cum="  << cumColOrder[cumIdx]
             << "prevCum=" << (prevCumColNum >= 0 ? cumColOrder[cumIdx-1] : QString("none"))
             << "from" << prevCumKey
             << "clearFirst=" << clearFirst
             << "rows=" << rowsToSum.size();

    // ── OPTIONAL CLEAR: delete rows 5-228 of the cumulative column ────────────
    // Called on the second run (after first save) so any stale template values
    // are removed (not zeroed) before the clean recompute — equivalent to
    // selecting the column in Excel and pressing Delete.
    if (clearFirst) {
        for (int row = 5; row <= 228; ++row) {
            m_handler->deleteCellValue(destKey, destSheet, row, cumColNum);
        }
        qDebug() << "[CUM_EXEC_ALL] clearFirst: deleted rows 5-228 col" << cumColOrder[cumIdx];
    }

    // ── PASS 1: Normal rows — prevCum (from prevPrevMonth file) + base (from prevMonth file) ──
    // Each month's file only contains its own month's base data.
    // Read base from the month-being-accumulated's own file, and prevCum from
    // the month before that's file.  Write the computed cumulative into the
    // TARGET month's file (the one currently being transferred).
    for (int row : rowsToSum) {
        if (m_subtotalsMap.contains(row)) continue;

        // Skip aesthetic spacer rows
        if (m_handler->getCellValue(destKey, destSheet, row, 2).toString().trimmed().isEmpty())
            continue;

        // Read base from the correct month's file
        QVariant baseRaw = m_handler->getCellValue(baseMonthKey, destSheet, row, baseColNum);
        double base = baseRaw.canConvert<double>() ? baseRaw.toDouble() : 0.0;

        if (directCopyRows.contains(row)) {
            m_handler->setCellValue(destKey, destSheet, row, cumColNum, roundTo5(base));
        } else {
            double prevCum = 0.0;
            if (prevCumColNum >= 0) {
                // Read prevCum from the correct prior month's file
                QVariant prevRaw = m_handler->getCellValue(prevCumKey, destSheet, row, prevCumColNum);
                prevCum = prevRaw.canConvert<double>() ? prevRaw.toDouble() : 0.0;
            }
            m_handler->setCellValue(destKey, destSheet, row, cumColNum, roundTo5(prevCum + base));
        }
    }

    // ── PASS 2: Subtotals – ascending/descending convergence loop ────────────
    const QString currentCumLetter = cumColOrder[cumIdx];
    const int maxConvergenceIter = 5;
    for (int iter = 0; iter < maxConvergenceIter; ++iter) {
        bool changed = false;

        for (int row : subRows) {
            QString f = m_subtotalsMap.value(row);
            f.replace("HW", currentCumLetter);
            double val = roundTo5(evaluateSimpleFormula(m_handler, destKey, destSheet, f));
            QVariant prev = m_handler->getCellValue(destKey, destSheet, row, cumColNum);
            double prevVal = prev.canConvert<double>() ? prev.toDouble() : 0.0;
            if (qAbs(val - prevVal) > 0.001) changed = true;
            m_handler->setCellValue(destKey, destSheet, row, cumColNum, val);
        }

        for (int i = subRows.size() - 1; i >= 0; --i) {
            int row = subRows[i];
            QString f = m_subtotalsMap.value(row);
            f.replace("HW", currentCumLetter);
            double val = roundTo5(evaluateSimpleFormula(m_handler, destKey, destSheet, f));
            QVariant prev = m_handler->getCellValue(destKey, destSheet, row, cumColNum);
            double prevVal = prev.canConvert<double>() ? prev.toDouble() : 0.0;
            if (qAbs(val - prevVal) > 0.001) changed = true;
            m_handler->setCellValue(destKey, destSheet, row, cumColNum, val);
        }

        if (!changed) {
            qDebug() << "[CUM_EXEC_ALL] Column" << currentCumLetter
                     << "subtotals converged after" << (iter + 1) << "iteration(s)";
            break;
        }
    }
}


void TransferService::runCumulativePassAllMonths(const QSet<int>& allRows,
                                                  const QString& destSheet,
                                                  int year,
                                                  const QString& destKey,
                                                  const QString& targetMonth)
{
    Q_UNUSED(year);
    if (!m_handler) return;

    int targetIdx = monthOrder.indexOf(targetMonth);
    if (targetIdx < 0) return;

    // Use the complete row set passed in, plus subtotals
    QSet<int> rowsToSum = allRows;
    for (int r : m_subtotalsMap.keys()) rowsToSum.insert(r);
    // Force inclusion of all MZLZ Report rows so empty/unmapped rows get a literal 0 instead of blank
    for (int r = 5; r <= 235; ++r) rowsToSum.insert(r);

    qDebug() << "[CUM_PASS_ALL] targetMonth=" << targetMonth
             << "columns 0.." << targetIdx
             << "rows=" << rowsToSum.size();

    QMap<int, double> runningSums;
    for (int m = 0; m < targetIdx && m < cumColOrder.size(); ++m) {
        int cumColIdx  = m_handler->letterToColumn(cumColOrder[m]);  // IP, IQ, IR, ...
        int baseColIdx = m_handler->letterToColumn(baseColOrder[m]); // G, W, AM, ...

        qDebug() << "  [CUM_PASS_ALL] m=" << m
                 << "baseCol=" << baseColOrder[m] << "cumCol=" << cumColOrder[m];

        // ── PASS 1: Normal Rows (Calculate accumulating sums) ──
        for (int row : rowsToSum) {
            if (m_subtotalsMap.contains(row)) {
                continue; // Handled dynamically in Pass 2
            }

            // If there is no descriptive text in Column B (index 2), treat it as an aesthetic spacer and skip
            if (m_handler->getCellValue(destKey, destSheet, row, 2).toString().trimmed().isEmpty()) {
                continue;
            }

            // Read the base column value directly.
            // Do NOT evaluate formulas here: base columns are written by transferEntry
            // with setCellValue (plain values). Evaluating any residual formula from the
            // Excel template would override the correctly-transferred value with a
            // stale formula result (often 0), causing all cumulative columns from
            // month 3 onward to repeat the month-2 value.
            QVariant raw = m_handler->getCellValue(destKey, destSheet, row, baseColIdx);
            double val = raw.canConvert<double>() ? raw.toDouble() : 0.0;

            if (directCopyRows.contains(row)) {
                runningSums[row] = val;
            } else {
                runningSums[row] += val;
            }
            m_handler->setCellValue(destKey, destSheet, row, cumColIdx, roundTo5(runningSums[row]));
        }

        // ── PASS 2: Subtotals – ascending/descending convergence loop ──
        // Some subtotals reference other subtotals at higher row numbers
        // (e.g. row 154 = HW158 - HW155), so a single ascending pass won't resolve
        // all dependencies.  We alternate ascending ↑ and descending ↓ passes until
        // every value stabilises (or a hard cap is reached).
        QList<int> subRows = m_subtotalsMap.keys();
        std::sort(subRows.begin(), subRows.end());
        QString currentCumLetter = cumColOrder[m];

        const int maxConvergenceIter = 5;
        for (int iter = 0; iter < maxConvergenceIter; ++iter) {
            bool changed = false;

            // ── Ascending pass (low row → high row) ──
            for (int row : subRows) {
                QString f = m_subtotalsMap.value(row);
                f.replace("HW", currentCumLetter);
                double val = roundTo5(evaluateSimpleFormula(m_handler, destKey, destSheet, f));
                QVariant prev = m_handler->getCellValue(destKey, destSheet, row, cumColIdx);
                double prevVal = prev.canConvert<double>() ? prev.toDouble() : 0.0;
                if (qAbs(val - prevVal) > 0.001) changed = true;
                runningSums[row] = val;
                m_handler->setCellValue(destKey, destSheet, row, cumColIdx, val);
            }

            // ── Descending pass (high row → low row) ──
            for (int i = subRows.size() - 1; i >= 0; --i) {
                int row = subRows[i];
                QString f = m_subtotalsMap.value(row);
                f.replace("HW", currentCumLetter);
                double val = roundTo5(evaluateSimpleFormula(m_handler, destKey, destSheet, f));
                QVariant prev = m_handler->getCellValue(destKey, destSheet, row, cumColIdx);
                double prevVal = prev.canConvert<double>() ? prev.toDouble() : 0.0;
                if (qAbs(val - prevVal) > 0.001) changed = true;
                runningSums[row] = val;
                m_handler->setCellValue(destKey, destSheet, row, cumColIdx, val);
            }

            if (!changed) {
                qDebug() << "[CUM_PASS_ALL] Subtotals converged at month" << m
                         << "after" << (iter + 1) << "iteration(s)";
                break;
            }
        }
    }
}


// ── Execute RT cumulative pass ──────────────────────────────────────────────
// Exact same logic as runCumulativePassExecuteAll but takes int month (1=Jan,
// 12=Dec) instead of a QString. Computes ONLY the single cumulative column for
// `month` (e.g. IQ for February). prevCum is read directly from the file —
// already correct from the prior RT step or the copied source file.
// Completely decoupled from Execute All so neither path can break the other.
void TransferService::runCumulativePassRT(const QSet<int>& destRows,
                                          const QString& sheetName,
                                          int year,
                                          const QString& destKey,
                                          int month)
{
    Q_UNUSED(year);
    if (!m_handler) return;

    // month is 1-based; convert to 0-based index
    const int targetIdx = month - 1;
    if (targetIdx < 0 || targetIdx >= cumColOrder.size()) {
        qWarning() << "[CUM_RT] Invalid month:" << month;
        return;
    }

    const int baseColIdx    = m_handler->letterToColumn(baseColOrder[targetIdx]);
    const int cumColIdx     = m_handler->letterToColumn(cumColOrder[targetIdx]);
    const int prevCumColIdx = (targetIdx > 0)
                              ? m_handler->letterToColumn(cumColOrder[targetIdx - 1])
                              : -1; // January: no previous cumulative

    // Extend with subtotals and full MZLZ report range (same as Execute All)
    QSet<int> rowsToSum = destRows;
    for (int r : m_subtotalsMap.keys()) rowsToSum.insert(r);
    for (int r = 5; r <= 235; ++r) rowsToSum.insert(r);

    QList<int> subRows = m_subtotalsMap.keys();
    std::sort(subRows.begin(), subRows.end());

    qDebug() << "[CUM_RT] month=" << month
             << "base=" << baseColOrder[targetIdx]
             << "cum="  << cumColOrder[targetIdx]
             << "prevCum=" << (prevCumColIdx >= 0 ? cumColOrder[targetIdx - 1] : QString("none"));

    // ── PASS 1: Normal rows — cum = prevCum + base ───────────────────────────
    for (int row : rowsToSum) {
        if (m_subtotalsMap.contains(row)) continue;

        // Skip aesthetic spacer rows (no label in column B)
        if (m_handler->getCellValue(destKey, sheetName, row, 2).toString().trimmed().isEmpty())
            continue;

        // Read base value (plain — transferEntry always uses setCellValue)
        QVariant baseRaw = m_handler->getCellValue(destKey, sheetName, row, baseColIdx);
        double base = baseRaw.canConvert<double>() ? baseRaw.toDouble() : 0.0;

        if (directCopyRows.contains(row)) {
            // Headcount rows: cum = base (no accumulation)
            m_handler->setCellValue(destKey, sheetName, row, cumColIdx, roundTo5(base));
        } else {
            // Normal rows: cum = prevCum + base
            double prevCum = 0.0;
            if (prevCumColIdx >= 0) {
                QVariant prevRaw = m_handler->getCellValue(destKey, sheetName, row, prevCumColIdx);
                prevCum = prevRaw.canConvert<double>() ? prevRaw.toDouble() : 0.0;
            }
            m_handler->setCellValue(destKey, sheetName, row, cumColIdx, roundTo5(prevCum + base));
        }
    }

    // ── PASS 2: Subtotals — ascending/descending convergence loop ────────────
    const QString cumLetter = cumColOrder[targetIdx];
    const int maxIter = 5;
    for (int iter = 0; iter < maxIter; ++iter) {
        bool changed = false;

        // Ascending pass (low row → high row)
        for (int row : subRows) {
            QString f = m_subtotalsMap.value(row);
            f.replace("HW", cumLetter);
            double val = roundTo5(evaluateSimpleFormula(m_handler, destKey, sheetName, f));
            QVariant prev = m_handler->getCellValue(destKey, sheetName, row, cumColIdx);
            double prevVal = prev.canConvert<double>() ? prev.toDouble() : 0.0;
            if (qAbs(val - prevVal) > 0.001) changed = true;
            m_handler->setCellValue(destKey, sheetName, row, cumColIdx, val);
        }

        // Descending pass (high row → low row)
        for (int i = subRows.size() - 1; i >= 0; --i) {
            int row = subRows[i];
            QString f = m_subtotalsMap.value(row);
            f.replace("HW", cumLetter);
            double val = roundTo5(evaluateSimpleFormula(m_handler, destKey, sheetName, f));
            QVariant prev = m_handler->getCellValue(destKey, sheetName, row, cumColIdx);
            double prevVal = prev.canConvert<double>() ? prev.toDouble() : 0.0;
            if (qAbs(val - prevVal) > 0.001) changed = true;
            m_handler->setCellValue(destKey, sheetName, row, cumColIdx, val);
        }

        if (!changed) {
            qDebug() << "[CUM_RT] Subtotals converged after" << (iter + 1) << "iteration(s)";
            break;
        }
    }

    qDebug() << "[CUM_RT] Done — cumulative column" << cumLetter << "written for month" << month;
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

    // ── Resolve source sheet name (same fallback logic as transferEntry::resolveSheet) ──
    // SAP YTD files sometimes use "03_2026" instead of "Report".
    QString sourceSheet = entry.sourceSheetTemplate.isEmpty() ? "Report" : entry.sourceSheetTemplate;
    {
        const QString requested = sourceSheet;
        const QString requestedNorm = requested.trimmed().toLower();
        bool exactFound = false;
        for (const QString& name : m_handler->getSheetNames(srcKey)) {
            if (name.trimmed().toLower() == requestedNorm) { exactFound = true; break; }
        }
        if (!exactFound) {
            // Try MM_YYYY fallback (e.g. "Report" → "03_2026")
            if (requestedNorm == "report") {
                const QString monthNum = ExcelHandler::MONTH_TO_NUM.value(entry.month, "");
                if (!monthNum.isEmpty()) {
                    const QString altName = QString("%1_%2").arg(monthNum).arg(year);
                    for (const QString& name : m_handler->getSheetNames(srcKey)) {
                        if (name.trimmed().toLower() == altName.toLower()) {
                            qInfo() << "[SAP_YTD] Sheet" << requested << "not found, using" << name;
                            sourceSheet = name;
                            break;
                        }
                    }
                }
            }
            // Still not found? Use first available sheet as last resort
            if (sourceSheet == requested) {
                const QStringList names = m_handler->getSheetNames(srcKey);
                if (!names.isEmpty()) {
                    qWarning() << "[SAP_YTD] Sheet" << requested << "not found in" << srcKey
                               << "— falling back to" << names.first();
                    sourceSheet = names.first();
                }
            }
        }
    }
    const QString destSheet = entry.destSheet.isEmpty() ? "MZLZ Consolidated" : entry.destSheet;
    const QString sourceColumn = entry.sourceColumn.isEmpty() ? "C" : entry.sourceColumn;
    const QString destColumn = entry.destColumn.isEmpty() ? "JG" : entry.destColumn;

    // ── Diagnostic logging ──────────────────────────────────────────────────
    qInfo() << "[SAP_YTD_DIAG] srcKey=" << srcKey
            << "srcFile=" << srcFile
            << "sourceSheet=" << sourceSheet
            << "sourceColumn=" << sourceColumn
            << "destColumn=" << destColumn
            << "rowMap.size=" << entry.rowMap.size()
            << "isLoaded=" << m_handler->isLoaded(srcKey)
            << "sheetNames=" << m_handler->getSheetNames(srcKey);

    // Sample check: try reading a known cell to see if data exists
    if (!entry.rowMap.isEmpty()) {
        int firstDestRow = entry.rowMap.constBegin().key();
        const QVector<int>& firstSrcRows = entry.rowMap.constBegin().value();
        if (!firstSrcRows.isEmpty() && firstSrcRows.first() != 0) {
            int testSrcRow = firstSrcRows.first();
            int testCol = m_handler->letterToColumn(sourceColumn);
            QVariant testVal = m_handler->getCellValue(srcKey, sourceSheet, testSrcRow, testCol);
            qInfo() << "[SAP_YTD_DIAG] Sample cell: sheet=" << sourceSheet
                    << "row=" << testSrcRow << "col=" << sourceColumn << "(" << testCol << ")"
                    << "value=" << testVal << "type=" << testVal.typeName()
                    << "canConvertDouble=" << testVal.canConvert<double>();
        }
    }

    int readCount = 0, emptyCount = 0, zeroSrcCount = 0;
    for (auto it = entry.rowMap.constBegin(); it != entry.rowMap.constEnd(); ++it) {
        int destRow = it.key();
        const QVector<int>& srcRows = it.value();
        double total = 0.0;
        bool hasValue = false;

        if (srcRows.size() == 1 && srcRows.first() == 0) {
            total = 0.0;
            hasValue = true;
            zeroSrcCount++;
        } else {
            for (int srcRow : srcRows) {
                QVariant value = m_handler->getCellValue(srcKey, sourceSheet, srcRow,
                                                         m_handler->letterToColumn(sourceColumn));
                if (value.canConvert<double>()) {
                    total += value.toDouble();
                    hasValue = true;
                    readCount++;
                } else {
                    // Try formula evaluation fallback — .xlsm files (especially 2026+)
                    // may contain formula cells without cached numeric values
                    QString valStr = value.toString().trimmed();
                    if (valStr.startsWith('=')) {
                        bool fOk = false;
                        double fVal = evaluateSimpleFormula(m_handler, srcKey, sourceSheet, valStr, &fOk);
                        if (fOk) {
                            total += fVal;
                            hasValue = true;
                            readCount++;
                            qDebug() << "[SAP_YTD] Formula fallback: srcRow=" << srcRow
                                     << "formula=" << valStr << "result=" << fVal;
                        } else {
                            emptyCount++;
                            if (emptyCount <= 5) {
                                qWarning() << "[SAP_YTD_DIAG] Empty/non-numeric cell: srcRow=" << srcRow
                                           << "col=" << sourceColumn << "value=" << value
                                           << "type=" << value.typeName();
                            }
                        }
                    } else {
                        emptyCount++;
                        if (emptyCount <= 5) {
                            qWarning() << "[SAP_YTD_DIAG] Empty/non-numeric cell: srcRow=" << srcRow
                                       << "col=" << sourceColumn << "value=" << value
                                       << "type=" << value.typeName();
                        }
                    }
                }
            }
        }

        // Always write a value (even 0) so we never leave stale template data in JG
        if (!hasValue) total = 0.0;
        // JG: rows 212/216/218/222/226 (PAX/EUR section) divide by +1000 (no sign flip); all other rows by -1000
        // Rows 12, 13, 16 are headcount rows — never divide or round
        const bool isHeadcountRowYtd = (destRow == 12 || destRow == 13 || destRow == 16);
        if (destColumn == "JG" && !isHeadcountRowYtd) {
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

    qInfo() << "[SAP_YTD_DIAG] Transfer complete: cellsTransferred=" << result.cellsTransferred
            << "readCount=" << readCount << "emptyCount=" << emptyCount
            << "zeroSrcCount=" << zeroSrcCount;

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