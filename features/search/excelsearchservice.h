#ifndef EXCELSEARCHSERVICE_H
#define EXCELSEARCHSERVICE_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QSet>

class ExcelHandler;

// ── Result structs ──────────────────────────────────────────────────────────

struct SearchMatch {
    QString term;           // Original search term
    QString sheetName;      // Sheet where found
    int     row    = 0;     // 1-based row
    int     col    = 0;     // 1-based column
    QString colLetter;      // Column letter (e.g. "AH")
    QString cellValue;      // Actual cell content
    bool    exactMatch = true;  // true = exact (case-insensitive), false = fuzzy suggestion
};

struct SearchRequest {
    QStringList terms;      // List of text terms to search
    QString     fileKey;    // Workbook key in ExcelHandler
    QStringList sheets;     // Which sheets to search (empty = all)
    int         maxCol = 300;  // Max column index to scan per row
    int         maxRow = 500;  // Max row index to scan per sheet
};


// ── Service ─────────────────────────────────────────────────────────────────

class ExcelSearchService : public QObject
{
    Q_OBJECT

public:
    explicit ExcelSearchService(ExcelHandler* handler, QObject* parent = nullptr);

    /// Run search synchronously, returns all matches + fuzzy suggestions.
    QVector<SearchMatch> search(const SearchRequest& request);

signals:
    void progress(int current, int total, const QString& message);

private:
    /// Levenshtein edit-distance between two strings (case-insensitive).
    static int editDistance(const QString& a, const QString& b);

    /// Returns true if `candidate` is a plausible fuzzy match for `term`.
    static bool isFuzzyMatch(const QString& term, const QString& candidate, int threshold = 3);

    ExcelHandler* m_handler;
};

#endif // EXCELSEARCHSERVICE_H
