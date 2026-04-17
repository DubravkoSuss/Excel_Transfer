#include "excelsearchservice.h"
#include "../../services/excelhandler.h"
#include <QDebug>
#include <algorithm>

ExcelSearchService::ExcelSearchService(ExcelHandler* handler, QObject* parent)
    : QObject(parent), m_handler(handler) {}

// ── Levenshtein edit-distance ───────────────────────────────────────────────
int ExcelSearchService::editDistance(const QString& a, const QString& b)
{
    const int m = a.length();
    const int n = b.length();
    QVector<QVector<int>> dp(m + 1, QVector<int>(n + 1, 0));

    for (int i = 0; i <= m; ++i) dp[i][0] = i;
    for (int j = 0; j <= n; ++j) dp[0][j] = j;

    for (int i = 1; i <= m; ++i) {
        for (int j = 1; j <= n; ++j) {
            int cost = (a[i - 1].toLower() == b[j - 1].toLower()) ? 0 : 1;
            dp[i][j] = std::min({dp[i - 1][j] + 1,       // deletion
                                 dp[i][j - 1] + 1,       // insertion
                                 dp[i - 1][j - 1] + cost}); // substitution
        }
    }
    return dp[m][n];
}

bool ExcelSearchService::isFuzzyMatch(const QString& term, const QString& candidate, int threshold)
{
    // Skip very short candidates (single chars, numbers etc)
    if (candidate.trimmed().length() < 3) return false;
    // Skip if lengths are too different
    if (std::abs(term.length() - candidate.length()) > threshold) return false;
    
    int dist = editDistance(term, candidate);
    
    // Scale threshold based on word length: longer words tolerate more edits
    int dynamicThreshold = std::max(1, static_cast<int>(term.length() / 3));
    dynamicThreshold = std::min(dynamicThreshold, threshold);
    
    return dist > 0 && dist <= dynamicThreshold;
}

// ── Main search ─────────────────────────────────────────────────────────────
QVector<SearchMatch> ExcelSearchService::search(const SearchRequest& request)
{
    QVector<SearchMatch> results;
    if (!m_handler || request.terms.isEmpty()) return results;

    // Normalise search terms (lowercase, trimmed)
    QStringList normTerms;
    for (const QString& t : request.terms) {
        QString norm = t.trimmed();
        if (!norm.isEmpty()) normTerms.append(norm);
    }
    if (normTerms.isEmpty()) return results;

    // Determine sheets to scan
    QStringList sheets = request.sheets;
    if (sheets.isEmpty())
        sheets = m_handler->getSheetNames(request.fileKey);

    // Track which terms got an exact match (so we only suggest fuzzy for unmatched ones)
    QSet<QString> exactMatched;
    // Fuzzy candidate pool: term → set of candidate strings
    QMap<QString, QSet<QString>> fuzzyCandidates;

    int totalCells = sheets.size() * request.maxRow * request.maxCol;
    int cellsDone = 0;
    int lastPct = -1;

    for (const QString& sheet : sheets) {
        for (int row = 1; row <= request.maxRow; ++row) {
            for (int col = 1; col <= request.maxCol; ++col) {
                QVariant raw = m_handler->getCellValue(request.fileKey, sheet, row, col);
                if (!raw.isValid() || raw.isNull()) continue;

                QString cellText = raw.toString().trimmed();
                if (cellText.isEmpty()) continue;

                QString cellLower = cellText.toLower();

                for (const QString& term : normTerms) {
                    QString termLower = term.toLower();

                    // Case-insensitive exact match
                    if (cellLower == termLower) {
                        SearchMatch m;
                        m.term       = term;
                        m.sheetName  = sheet;
                        m.row        = row;
                        m.col        = col;
                        m.colLetter  = m_handler->columnToLetter(col);
                        m.cellValue  = cellText;
                        m.exactMatch = true;
                        results.append(m);
                        exactMatched.insert(termLower);
                    }
                    // Collect fuzzy candidates
                    else if (!exactMatched.contains(termLower) && isFuzzyMatch(termLower, cellLower)) {
                        fuzzyCandidates[termLower].insert(cellText);
                    }
                }
            }

            // Progress reporting (per-row granularity)
            cellsDone += request.maxCol;
            int pct = (totalCells > 0) ? (cellsDone * 100 / totalCells) : 100;
            if (pct != lastPct) {
                lastPct = pct;
                emit progress(pct, 100, QString("Searching %1 row %2...").arg(sheet).arg(row));
            }
        }
    }

    // Add fuzzy suggestions for terms that had NO exact match
    for (auto it = fuzzyCandidates.constBegin(); it != fuzzyCandidates.constEnd(); ++it) {
        if (exactMatched.contains(it.key())) continue; // skip if exact was found later

        for (const QString& candidate : it.value()) {
            SearchMatch m;
            m.term       = it.key();
            m.cellValue  = candidate;
            m.exactMatch = false; // fuzzy suggestion
            m.sheetName  = "—";
            m.row        = 0;
            m.col        = 0;
            m.colLetter  = "—";
            results.append(m);
        }
    }

    return results;
}
