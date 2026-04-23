#ifndef EXCELSEARCHSERVICE_H
#define EXCELSEARCHSERVICE_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QSet>
#include <QAtomicInt>

class ExcelHandler;

// ── Result structs ──────────────────────────────────────────────────────────

struct SearchMatch {
    QString term;               // Original search term
    QString sheetName;          // Sheet where found
    int     row    = 0;         // 1-based row
    int     col    = 0;         // 1-based column
    QString colLetter;          // Column letter (e.g. "AH")
    QString cellValue;          // Actual cell content
    bool    exactMatch = true;  // true = exact/contains/regex; false = fuzzy suggestion
    QString matchType;          // "Exact" | "Contains" | "Regex" | "Fuzzy"
};

enum class SearchMode {
    Exact    = 0,  // full cell == term (case-insensitive or sensitive)
    Contains = 1,  // cell contains term as substring
    Regex    = 2,  // term is a regular expression
    Fuzzy    = 3   // Levenshtein distance
};

struct SearchRequest {
    QStringList terms;            // List of text terms to search
    QString     fileKey;          // Workbook key in ExcelHandler
    QStringList sheets;           // Which sheets to search (empty = all)
    QString     searchColumn;     // Optional: restrict to this column (e.g. "A")
    int         maxCol      = 500;  // Max column index to scan per row
    int         maxRow      = 5000; // Max row index to scan per sheet
    bool        caseSensitive = false;
    SearchMode  mode        = SearchMode::Contains;  // default changed to Contains
    int         fuzzyThreshold = 3;
    bool        includeFuzzy = true;  // also append fuzzy suggestions for unmatched terms
};

// ── Service ─────────────────────────────────────────────────────────────────

class ExcelSearchService : public QObject
{
    Q_OBJECT

public:
    explicit ExcelSearchService(ExcelHandler* handler, QObject* parent = nullptr);

    /// Run search synchronously, returns all matches + fuzzy suggestions.
    QVector<SearchMatch> search(const SearchRequest& request,
                                 QAtomicInt* stopFlag = nullptr);

    /// Expose handler for use by worker
    ExcelHandler* handler() const { return m_handler; }

signals:
    void progress(int current, int total, const QString& message);

private:
    static int  editDistance(const QString& a, const QString& b);
    static bool isFuzzyMatch(const QString& term, const QString& candidate,
                              int threshold = 3);

    ExcelHandler* m_handler;
};

#endif // EXCELSEARCHSERVICE_H