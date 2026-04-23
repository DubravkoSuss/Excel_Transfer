#ifndef MAPPINGUPDATER_H
#define MAPPINGUPDATER_H

#include <QString>
#include <QMap>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QVector>

class ExcelHandler;

// ═══════════════════════════════════════════════════════════════════════════
// Config read from label_definitions.json
// ═══════════════════════════════════════════════════════════════════════════
struct LabelConfig {
    QString sourceSheet        = "Report";
    QString destSheet          = "MZLZ Consolidated";
    QStringList sourceLabelCols = {"A"};       // letters for source file
    QStringList destLabelCols   = {"A"};       // letters for dest file
    QVector<int> sourceLabelNums = {1};        // 1-based column numbers
    QVector<int> destLabelNums   = {1};        // 1-based column numbers
    int     simpleCount        = 0;
    int     sumCount           = 0;
};

// ═══════════════════════════════════════════════════════════════════════════
// Label entry — loaded from label_definitions.json
// ═══════════════════════════════════════════════════════════════════════════
struct LabelEntry {
    QString label;                    // text to search in both files (simple 1:1)

    // ── SUM support ──────────────────────────────────────────────────────
    bool    isSum = false;
    QString destLabel;                // text to find dest row for sum
    QVector<QString> sourceLabels;    // texts to find source rows for sum
};

struct UpdateResult {
    int matched   = 0;
    int notFound  = 0;
    int ambiguous = 0;
    int total     = 0;
    QStringList warnings;
    QStringList details;       // per-entry log lines
};

class MappingUpdater
{
public:
    // ── Column letter to 1-based number ──────────────────────────────────
    static int columnToNumber(const QString& col);

    // ── Normalize a label for comparison ──────────────────────────────────
    static QString normalize(const QString& text);

    // ── Build an index of { normalized_label → row_number } ──────────────
    //    Scans multiple columns, merging results (first occurrence wins)
    static QMap<QString, int> buildLabelIndex(
        ExcelHandler& handler, const QString& fileKey,
        const QString& sheetName, const QVector<int>& labelCols,
        int maxRow = 3000);

    // ── Load config + label definitions from JSON ────────────────────────
    static LabelConfig loadConfig(const QString& path);
    static QVector<LabelEntry> loadLabelDefinitions(const QString& path);

    // ── Update: scan files for labels, rewrite mappings JSON ─────────────
    static UpdateResult updateMappings(
        ExcelHandler& handler,
        const QString& sourceFile, const QString& sourceSheet,
        const QString& destFile,   const QString& destSheet,
        const QString& labelDefPath,
        const QString& mappingsJsonPath,
        const QVector<int>& srcLabelCols = {1},
        const QVector<int>& dstLabelCols = {1});
};

#endif // MAPPINGUPDATER_H