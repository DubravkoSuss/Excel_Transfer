#include "rolloverworker.h"
#include "../../services/excelhandler.h"
#include <QDebug>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

RolloverWorker::RolloverWorker(const RolloverConfig& config, QObject* parent)
    : QThread(parent), m_config(config)
{
    m_fileKey = "rollover_" + QString::number(QDateTime::currentMSecsSinceEpoch());
}

RolloverWorker::~RolloverWorker()
{
}

void RolloverWorker::requestStop()
{
    m_stopRequested.storeRelaxed(1);
}

// ---------------------------------------------------------------------------
// isStipulatedFormula — returns true if this formula should be KEPT
// ---------------------------------------------------------------------------
bool RolloverWorker::isStipulatedFormula(const QString& formula) const
{
    if (formula.isEmpty()) return false;

    QString upper = formula.toUpper();

    // Anything that pulls external data → WIPE
    if (upper.contains("VLOOKUP") ||
        upper.contains("HLOOKUP") ||
        upper.contains("XLOOKUP") ||
        upper.contains("INDEX")   ||
        upper.contains("MATCH"))
    {
        return false;
    }

    // Cross-sheet references (contain '!') → WIPE
    if (upper.contains("!")) {
        return false;
    }

    // Structural / math formulas → KEEP
    if (upper.contains("SUM")     ||
        upper.contains("AVERAGE") ||
        upper.contains("SUBTOTAL")||
        upper.contains("+")       ||
        upper.contains("-")       ||
        upper.contains("*")       ||
        upper.contains("/"))
    {
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// loadSubtotals — parse mzlz_subtotals.json into row→formula map
// ---------------------------------------------------------------------------
bool RolloverWorker::loadSubtotals(const QString& path,
                                    QMap<int, QString>& subtotalsOut) const
{
    if (path.isEmpty()) return false;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "[ROLLOVER] Cannot open subtotals config:" << path;
        return false;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "[ROLLOVER] Subtotals JSON parse error:" << err.errorString();
        return false;
    }

    QJsonObject obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        bool ok = false;
        int row = it.key().toInt(&ok);
        if (ok) {
            subtotalsOut[row] = it.value().toString();
        }
    }

    return !subtotalsOut.isEmpty();
}

// ---------------------------------------------------------------------------
// adaptSubtotalFormula — replace template column (HW) with targetCol
// ---------------------------------------------------------------------------
QString RolloverWorker::adaptSubtotalFormula(const QString& templateFormula,
                                              const QString& templateCol,
                                              const QString& targetCol) const
{
    QString result = templateFormula;
    result.replace(templateCol, targetCol, Qt::CaseInsensitive);
    return result;
}

// ---------------------------------------------------------------------------
// renameYearInSheets — rename sheet tabs that contain the old year
// e.g. "TRAFFIC mott 2025" → "TRAFFIC mott 2026"
// Returns the number of sheets renamed.
// ---------------------------------------------------------------------------
int RolloverWorker::renameYearInSheets(ExcelHandler& handler, int oldYear, int newYear)
{
    int renamed = 0;
    QStringList sheets = handler.getSheetNames(m_fileKey);
    QString oldStr = QString::number(oldYear);
    QString newStr = QString::number(newYear);

    for (const QString& name : sheets) {
        if (name.contains(oldStr)) {
            QString newName = name;
            newName.replace(oldStr, newStr);
            if (handler.renameSheet(m_fileKey, name, newName)) {
                emit logLine(QString("[OK] Renamed sheet: \"%1\" → \"%2\"").arg(name, newName));
                ++renamed;
            } else {
                emit logLine(QString("[WARN] Failed to rename sheet: \"%1\" → \"%2\"").arg(name, newName));
            }
        }
    }
    return renamed;
}

// ---------------------------------------------------------------------------
// run — main worker logic
// ---------------------------------------------------------------------------
void RolloverWorker::run()
{
    ExcelHandler handler;
    emit progress(0, 100, "Loading workbook...");
    emit logLine(QString("[INFO] Source: %1").arg(m_config.sourceFile));
    emit logLine(QString("[INFO] Destination: %1").arg(m_config.destFile.isEmpty() ? "(dry run)" : m_config.destFile));
    emit logLine(QString("[INFO] Target year: %1").arg(m_config.targetYear));
    emit logLine(QString("[INFO] Sheet: %1").arg(m_config.sheetName));
    emit logLine(QString("[INFO] Row range: %1 – %2").arg(m_config.startRow).arg(m_config.endRow));
    if (m_config.dryRun)
        emit logLine("[INFO] *** DRY RUN — no changes will be written ***");

    // Log column groups
    QStringList groups;
    if (m_config.clearBase) groups << "Base (G–HW)";
    if (m_config.clearCumul) groups << "Cumulative (IP–JA)";
    if (m_config.clearJG) groups << "JG (YTD)";
    emit logLine(QString("[INFO] Column groups: %1").arg(groups.isEmpty() ? "NONE" : groups.join(", ")));
    if (m_config.renameSheets)
        emit logLine(QString("[INFO] Sheet renaming: ON (old year → %1)").arg(m_config.targetYear));

    // ── 1. Load the workbook ──────────────────────────────────────────────
    bool loaded = handler.loadWorkbook(m_config.sourceFile, m_fileKey);
    if (!loaded) {
        emit logLine("[ERROR] Failed to load source workbook.");
        emit finished(false, "Failed to load source workbook.", 0, 0, 0);
        return;
    }
    emit logLine("[OK] Workbook loaded successfully.");

    // ── 2. Verify sheet exists ────────────────────────────────────────────
    QStringList sheets = handler.getSheetNames(m_fileKey);
    emit logLine(QString("[INFO] Found %1 sheets: %2").arg(sheets.size()).arg(sheets.join(", ")));

    if (!sheets.contains(m_config.sheetName)) {
        // Try case-insensitive match
        bool found = false;
        for (const QString& s : sheets) {
            if (s.compare(m_config.sheetName, Qt::CaseInsensitive) == 0) {
                emit logLine(QString("[WARN] Sheet name case mismatch: using \"%1\" instead of \"%2\"")
                                 .arg(s, m_config.sheetName));
                m_config.sheetName = s;
                found = true;
                break;
            }
        }
        if (!found) {
            QString msg = QString("Sheet '%1' not found in workbook.\n\nAvailable sheets:\n%2")
                              .arg(m_config.sheetName, sheets.join(", "));
            emit logLine("[ERROR] " + msg);
            emit finished(false, msg, 0, 0, 0);
            return;
        }
    }

    emit progress(5, 100, "Sheet verified. Loading subtotals config...");
    emit logLine(QString("[INFO] Sheet '%1' found.").arg(m_config.sheetName));

    // ── 3. Load subtotals config ──────────────────────────────────────────
    QMap<int, QString> subtotals;
    bool hasSubtotals = false;
    if (m_config.applySubtotals && !m_config.subtotalsConfigPath.isEmpty()) {
        hasSubtotals = loadSubtotals(m_config.subtotalsConfigPath, subtotals);
        if (hasSubtotals)
            emit logLine(QString("[INFO] Loaded %1 subtotal rules.").arg(subtotals.size()));
        else
            emit logLine("[WARN] Subtotals config could not be loaded — subtotals will not be restored.");
    }

    // ── 4. Build column list based on user selection ──────────────────────
    QStringList baseCols  = {"G","W","AM","BD","BW","CP","DJ","EF","FB","FY","GX","HW"};
    QStringList cumulCols = {"IP","IQ","IR","IS","IT","IU","IV","IW","IX","IY","IZ","JA"};
    QStringList jgCols    = {"JG"};
    // Budget/REFI/PrevYear columns: 3 columns per month from mappings_budget_refi_prev_year.json
    // Jan: D/E/F, Feb: T/U/V, Mar: AJ/AK/AL, Apr: AZ/BA/BB, May: BS/BT/BU, Jun: CL/CM/CN
    // Jul: DE/DF/DG, Aug: EA/EB/EC, Sep: EW/EX/EY, Oct: FS/FT/FU, Nov: GR/GS/GT, Dec: HQ/HR/HS
    QStringList budgetCols = {"D","E","F", "T","U","V", "AJ","AK","AL", "AZ","BA","BB",
                              "BS","BT","BU", "CL","CM","CN", "DE","DF","DG", "EA","EB","EC",
                              "EW","EX","EY", "FS","FT","FU", "GR","GS","GT", "HQ","HR","HS"};

    QStringList allTargetCols;
    if (m_config.clearBase)   allTargetCols += baseCols;
    if (m_config.clearCumul)  allTargetCols += cumulCols;
    if (m_config.clearJG)     allTargetCols += jgCols;
    if (m_config.clearBudget) allTargetCols += budgetCols;

    if (allTargetCols.isEmpty()) {
        emit logLine("[WARN] No column groups selected — nothing to clear.");
        emit finished(true, "No column groups selected. Nothing was changed.", 0, 0, 0, 0);
        return;
    }

    emit progress(8, 100, "Scanning cells...");
    int totalRows = m_config.endRow - m_config.startRow + 1;
    emit logLine(QString("[INFO] Processing %1 columns × %2 rows = %3 cells.")
                     .arg(allTargetCols.size())
                     .arg(totalRows)
                     .arg(allTargetCols.size() * totalRows));

    int totalCells     = allTargetCols.size() * totalRows;
    int cellsProcessed = 0;
    int cellsCleared   = 0;
    int cellsPreserved = 0;
    int lastPct        = 8;

    // ── 5. Main clearing loop ─────────────────────────────────────────────
    for (int row = m_config.startRow; row <= m_config.endRow; ++row)
    {
        if (m_stopRequested.loadRelaxed()) break;

        for (const QString& col : allTargetCols)
        {
            int colIdx = handler.letterToColumn(col);

            QString formula = handler.getCellFormula(m_fileKey, m_config.sheetName, row, colIdx);
            QVariant currentVal = handler.getCellValue(m_fileKey, m_config.sheetName, row, colIdx);

            if (formula.isEmpty()) {
                // Pure data cell → wipe
                if (!currentVal.isNull() && currentVal.isValid()) {
                    if (!m_config.dryRun)
                        handler.deleteCellValue(m_fileKey, m_config.sheetName, row, colIdx);
                    cellsCleared++;
                }
            } else if (isStipulatedFormula(formula)) {
                // Structural formula → keep but clear cached value to 0
                if (!m_config.dryRun)
                    handler.setCellValue(m_fileKey, m_config.sheetName, row, colIdx, 0.0);
                cellsPreserved++;
            } else {
                // Lookup formula or unknown → wipe completely
                if (!m_config.dryRun) {
                    handler.setCellFormula(m_fileKey, m_config.sheetName, row, colIdx, "");
                    handler.deleteCellValue(m_fileKey, m_config.sheetName, row, colIdx);
                }
                cellsCleared++;
            }

            ++cellsProcessed;
            int pct = 8 + (cellsProcessed * 62 / totalCells); // 8 → 70%
            if (pct != lastPct) {
                lastPct = pct;
                emit progress(pct, 100, QString("Processing row %1 / %2…").arg(row).arg(m_config.endRow));
            }
        }
    }

    if (m_stopRequested.loadRelaxed()) {
        emit logLine("[WARN] Operation cancelled by user.");
        emit finished(false, "Operation cancelled by user.", cellsCleared, cellsPreserved, 0, 0);
        return;
    }

    emit logLine(QString("[INFO] Scan complete. Cleared: %1, Preserved: %2.")
                     .arg(cellsCleared).arg(cellsPreserved));

    // ── 6. Restore subtotal formulas ──────────────────────────────────────
    int subtotalsApplied = 0;

    if (!m_config.dryRun && hasSubtotals && m_config.applySubtotals)
    {
        emit progress(70, 100, "Restoring subtotal formulas...");
        emit logLine("[INFO] Restoring subtotal formulas...");

        // Apply subtotals to base columns
        QStringList colsToRestore;
        if (m_config.clearBase) colsToRestore += baseCols;
        // Also apply to cumulative columns if they were cleared
        if (m_config.clearCumul) colsToRestore += cumulCols;

        for (const QString& col : colsToRestore)
        {
            for (auto it = subtotals.begin(); it != subtotals.end(); ++it)
            {
                int row = it.key();
                if (row < m_config.startRow || row > m_config.endRow) continue;

                QString adapted = adaptSubtotalFormula(it.value(), "HW", col);
                int colIdx = handler.letterToColumn(col);
                handler.setCellFormula(m_fileKey, m_config.sheetName, row, colIdx, adapted);
                ++subtotalsApplied;
            }
        }

        emit logLine(QString("[OK] Subtotals applied: %1 formulas across %2 columns.")
                         .arg(subtotalsApplied).arg(colsToRestore.size()));
    }

    // ── 7. Rename sheets with year references ─────────────────────────────
    int sheetsRenamed = 0;
    if (!m_config.dryRun && m_config.renameSheets)
    {
        emit progress(78, 100, "Renaming sheets...");
        int oldYear = m_config.targetYear - 1;
        sheetsRenamed = renameYearInSheets(handler, oldYear, m_config.targetYear);
        if (sheetsRenamed > 0)
            emit logLine(QString("[OK] Renamed %1 sheet(s).").arg(sheetsRenamed));
        else
            emit logLine("[INFO] No sheets needed renaming.");
    }

    // ── DRY RUN result ────────────────────────────────────────────────────
    if (m_config.dryRun) {
        emit progress(100, 100, "Dry run complete.");

        int wouldSubtotals = 0;
        if (hasSubtotals) {
            QStringList colsToRestore;
            if (m_config.clearBase) colsToRestore += baseCols;
            if (m_config.clearCumul) colsToRestore += cumulCols;
            for (const QString& col : colsToRestore) {
                for (auto it = subtotals.begin(); it != subtotals.end(); ++it) {
                    int row = it.key();
                    if (row >= m_config.startRow && row <= m_config.endRow)
                        ++wouldSubtotals;
                }
            }
        }

        int wouldRename = 0;
        if (m_config.renameSheets) {
            int oldYear = m_config.targetYear - 1;
            QString oldStr = QString::number(oldYear);
            for (const QString& name : handler.getSheetNames(m_fileKey)) {
                if (name.contains(oldStr)) ++wouldRename;
            }
        }

        QString msg = QString(
            "DRY RUN complete — no changes written.\n\n"
            "Would clear:    %1 cells\n"
            "Would preserve: %2 cells (structural formulas)\n"
            "Would restore:  %3 subtotal formulas\n"
            "Would rename:   %4 sheet(s)")
            .arg(cellsCleared).arg(cellsPreserved).arg(wouldSubtotals).arg(wouldRename);
        emit logLine("[INFO] " + msg);
        emit finished(true, msg, cellsCleared, cellsPreserved, 0, wouldRename);
        return;
    }

    // ── 8. Save workbook ──────────────────────────────────────────────────
    emit progress(82, 100, "Saving new workbook...");
    emit logLine("[INFO] Saving workbook to: " + m_config.destFile);

    bool saved = handler.saveWorkbook(m_fileKey, m_config.destFile);

    if (saved) {
        emit progress(100, 100, "Done.");
        QString msg = QString(
            "Successfully prepared workbook for year %1.\n\n"
            "Cells cleared:            %2\n"
            "Formulas preserved:       %3\n"
            "Subtotals restored:       %4\n"
            "Sheets renamed:           %5\n\n"
            "Output file:\n%6")
            .arg(m_config.targetYear)
            .arg(cellsCleared)
            .arg(cellsPreserved)
            .arg(subtotalsApplied)
            .arg(sheetsRenamed)
            .arg(m_config.destFile);
        emit logLine("[OK] " + msg);
        emit finished(true, msg, cellsCleared, cellsPreserved, subtotalsApplied, sheetsRenamed);
    } else {
        emit logLine("[ERROR] Failed to save the new workbook.");
        emit finished(false, "Failed to save the new workbook.", cellsCleared, cellsPreserved, subtotalsApplied, sheetsRenamed);
    }
}