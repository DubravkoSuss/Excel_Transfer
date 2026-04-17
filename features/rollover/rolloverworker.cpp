#include "rolloverworker.h"
#include "../../services/excelhandler.h"
#include <QDebug>
#include <QDateTime>

RolloverWorker::RolloverWorker(const RolloverConfig& config, QObject* parent)
    : QThread(parent), m_config(config)
{
    m_fileKey = "rollover_" + QString::number(QDateTime::currentMSecsSinceEpoch());
}

RolloverWorker::~RolloverWorker()
{
}

bool RolloverWorker::isStipulatedFormula(const QString& formula) const
{
    if (formula.isEmpty()) return false;
    
    QString upperForm = formula.toUpper();
    
    // 1. If it contains data retrieval formulas, we WANT to delete it.
    if (upperForm.contains("VLOOKUP") || 
        upperForm.contains("INDEX") || 
        upperForm.contains("MATCH") || 
        upperForm.contains("XLOOKUP") ||
        upperForm.contains("HLOOKUP")) {
        return false; // Not a kept formula
    }
    
    // 2. Otherwise, if it has ARITHMETIC or structural aggregation, keep it
    if (upperForm.contains("SUM") || 
        upperForm.contains("AVERAGE") || 
        upperForm.contains("+") || 
        upperForm.contains("-") || 
        upperForm.contains("*") || 
        upperForm.contains("/")) {
        return true;
    }
    
    // 3. Simple references (e.g., =A1) or other functions -> keep by default to be safe,
    // or delete? The user says: "delete others that are not sum, multiplication or other arithmetics."
    // If it's a raw number string with a leading =, we probably delete, but standard Excel
    // usually keeps simple link refs. The safest is to delete unless it explicitly matches the keep.
    return false;
}

void RolloverWorker::run()
{
    ExcelHandler handler;
    emit progress(0, 100, "Loading workbook...");
    
    // 1. Load the Excel file
    bool loaded = handler.loadWorkbook(m_fileKey, m_config.sourceFile);
    if (!loaded) {
        emit finished(false, "Failed to load source workbook.");
        return;
    }

    // 2. We only target "MZLZ Consolidated"
    const QString sheetName = "MZLZ Consolidated";
    QStringList sheets = handler.getSheetNames(m_fileKey);
    if (!sheets.contains(sheetName)) {
        emit finished(false, "Sheet 'MZLZ Consolidated' not found.");
        return;
    }

    emit progress(10, 100, "Scanning cells...");

    // 3. Define columns to clear
    // 12 Base columns
    QStringList targetCols = {
        "G", "W", "AM", "BD", "BW", "CP", "DJ", "EF", "FB", "FY", "GX", "HW"
    };
    // 12 Cumulative columns
    targetCols << "IP" << "IQ" << "IR" << "IS" << "IT" << "IU" << "IV" << "IW" << "IX" << "IY" << "IZ" << "JA";

    int totalCells = targetCols.size() * (m_config.endRow - m_config.startRow + 1);
    int cellsProcessed = 0;
    int lastPct = 10;
    
    int cellsCleared = 0;
    int cellsPreserved = 0;

    for (int row = m_config.startRow; row <= m_config.endRow; ++row) {
        if (m_stopRequested) break;

        for (const QString& colLetter : targetCols) {
            int col = handler.letterToColumn(colLetter);
            
            // Check formula
            QString formula = handler.getCellFormula(m_fileKey, sheetName, row, col);
            
            if (formula.isEmpty()) {
                // No formula - pure data - WIPE IT
                handler.setCellValue(m_fileKey, sheetName, row, col, 0.0);
                cellsCleared++;
            } else {
                // Has formula
                if (isStipulatedFormula(formula)) {
                    // Safe structural formula - KEEP IT
                    cellsPreserved++;
                } else {
                    // VLOOKUP or unknown text - WIPE IT
                    handler.setCellValue(m_fileKey, sheetName, row, col, 0.0);
                    // Crucial: When we wipe a cell, we must also clear the formula property
                    handler.setCellFormula(m_fileKey, sheetName, row, col, ""); 
                    cellsCleared++;
                }
            }
            
            cellsProcessed++;
            int pct = 10 + (cellsProcessed * 70 / totalCells); // 10% to 80%
            if (pct != lastPct) {
                lastPct = pct;
                emit progress(pct, 100, QString("Clearing data row %1...").arg(row));
            }
        }
    }

    if (m_stopRequested) {
        emit finished(false, "Operation cancelled.");
        return;
    }

    // 4. Save workbook
    emit progress(80, 100, "Saving new workbook...");
    bool saved = handler.saveWorkbook(m_fileKey, m_config.destFile);
    
    if (saved) {
        emit progress(100, 100, "Done.");
        QString msg = QString("Successfully rolled over workbook %1.\n\nCleared %2 cells.\nPreserved %3 formula cells.")
                      .arg(m_config.targetYear)
                      .arg(cellsCleared).arg(cellsPreserved);
        emit finished(true, msg);
    } else {
        emit finished(false, "Failed to save the new workbook.");
    }
}
