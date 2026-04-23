#ifndef ROLLOVERWORKER_H
#define ROLLOVERWORKER_H

#include <QThread>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QAtomicInt>

struct RolloverConfig {
    QString sourceFile;         // The loaded previous-year file
    QString destFile;           // The new file to save
    int     targetYear = 2027;  // E.g. 2027
    int     startRow   = 5;
    int     endRow     = 300;
    QString sheetName  = "MZLZ Consolidated";

    // Behaviour flags
    bool dryRun          = false;  // If true: only scan and report, do not write
    bool applySubtotals  = true;   // If true: restore subtotal formulas after clearing
    QString subtotalsConfigPath;   // Path to mzlz_subtotals.json (may be empty → no restore)

    // Column group flags
    bool clearBase       = true;   // Clear base monthly columns (G, W, AM, ...)
    bool clearCumul      = true;   // Clear cumulative columns (IP, IQ, IR, ...)
    bool clearJG         = true;   // Clear JG (YTD) column
    bool clearBudget     = true;   // Clear Budget/REFI/PrevYear columns (D,E,F, T,U,V, AJ,AK,AL, AZ,BA,BB, etc.)

    // Sheet operations
    bool renameSheets    = true;   // Rename year references in sheet names (e.g. "TRAFFIC mott 2025" → "...2026")
    bool processAllSheets = false; // Process additional sheets beyond MZLZ Consolidated
};

// Per-cell result (used by dry-run preview)
struct RolloverCellResult {
    int     row;
    QString col;
    QString action;      // "CLEAR" | "KEEP" | "SKIP"
    QString reason;
    QString originalValue;
    QString formula;
};

class ExcelHandler;

class RolloverWorker : public QThread
{
    Q_OBJECT

public:
    explicit RolloverWorker(const RolloverConfig& config, QObject* parent = nullptr);
    ~RolloverWorker() override;

    void requestStop();

signals:
    void progress(int current, int total, const QString& message);
    void logLine(const QString& line);
    void finished(bool success, const QString& message,
                  int cellsCleared, int cellsPreserved, int subtotalsApplied,
                  int sheetsRenamed = 0);

protected:
    void run() override;

private:
    bool isStipulatedFormula(const QString& formula) const;
    bool loadSubtotals(const QString& path, QMap<int, QString>& subtotalsOut) const;
    QString adaptSubtotalFormula(const QString& templateFormula,
                                 const QString& templateCol,
                                 const QString& targetCol) const;
    int renameYearInSheets(ExcelHandler& handler, int oldYear, int newYear);

    RolloverConfig  m_config;
    QString         m_fileKey;
    QAtomicInt      m_stopRequested { 0 };
};

#endif // ROLLOVERWORKER_H