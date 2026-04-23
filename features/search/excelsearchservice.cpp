#include "excelsearchservice.h"
#include "../../services/excelhandler.h"
#include <QDebug>
#include <QRegularExpression>
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
            dp[i][j] = std::min({dp[i - 1][j] + 1,
                                 dp[i][j - 1] + 1,
                                 dp[i - 1][j - 1] + cost});
        }
    }
    return dp[m][n];
}

bool ExcelSearchService::isFuzzyMatch(const QString& term, const QString& candidate, int threshold)
{
    if (candidate.trimmed().length() < 3) return false;
    if (std::abs(term.length() - candidate.length()) > threshold) return false;

    int dist = editDistance(term, candidate);
    int dynamicThreshold = std::max(1, static_cast<int>(term.length() / 3));
    dynamicThreshold = std::min(dynamicThreshold, threshold);

    return dist > 0 && dist <= dynamicThreshold;
}

// ── Main search ─────────────────────────────────────────────────────────────
QVector<SearchMatch> ExcelSearchService::search(const SearchRequest& request,
                                                  QAtomicInt* stopFlag)
{
    QVector<SearchMatch> results;
    if (!m_handler || request.terms.isEmpty()) return results;

    // Collect non-empty terms
    QStringList normTerms;
    for (const QString& t : request.terms) {
        QString norm = t.trimmed();
        if (!norm.isEmpty()) normTerms.append(norm);
    }
    if (normTerms.isEmpty()) return results;

    // Precompile regex patterns (only for Regex mode)
    QVector<QRegularExpression> regexList;
    if (request.mode == SearchMode::Regex) {
        for (const QString& t : normTerms) {
            QRegularExpression::PatternOptions opts = request.caseSensitive
                ? QRegularExpression::NoPatternOption
                : QRegularExpression::CaseInsensitiveOption;
            regexList.append(QRegularExpression(t, opts));
        }
    }

    Qt::CaseSensitivity cs = request.caseSensitive
        ? Qt::CaseSensitive : Qt::CaseInsensitive;

    // Determine sheets to scan
    QStringList sheets = request.sheets;
    if (sheets.isEmpty())
        sheets = m_handler->getSheetNames(request.fileKey);

    // Column range
    int startCol = 1;
    int endCol   = request.maxCol;
    if (!request.searchColumn.trimmed().isEmpty()) {
        QString colStr = request.searchColumn.trimmed();
        bool isNumeric = false;
        int colNum = colStr.toInt(&isNumeric);
        if (isNumeric) {
            startCol = colNum;
            endCol   = colNum;
        } else {
            int targetCol = m_handler->letterToColumn(colStr);
            if (targetCol > 0) {
                startCol = targetCol;
                endCol   = targetCol;
            }
        }
    }

    // Track exact-matched terms so fuzzy isn't shown for them
    QSet<QString> exactMatched;

    int totalRows      = sheets.size() * request.maxRow;
    int progressCounter = 0;
    int lastPct        = -1;

    for (const QString& sheet : sheets)
    {
        for (int row = 1; row <= request.maxRow; ++row)
        {
            if (stopFlag && stopFlag->loadRelaxed()) goto done;

            for (int col = startCol; col <= endCol; ++col)
            {
                QVariant raw = m_handler->getCellValue(request.fileKey, sheet, row, col);
                if (!raw.isValid() || raw.isNull()) continue;

                QString cellText = raw.toString().trimmed();
                if (cellText.isEmpty()) continue;

                for (int ti = 0; ti < normTerms.size(); ++ti)
                {
                    const QString& term = normTerms[ti];
                    bool matched = false;
                    QString matchType;

                    switch (request.mode)
                    {
                    case SearchMode::Exact:
                        matched = (cellText.compare(term, cs) == 0);
                        matchType = "Exact";
                        break;

                    case SearchMode::Contains:
                        matched = cellText.contains(term, cs);
                        matchType = "Contains";
                        break;

                    case SearchMode::Regex:
                        if (ti < regexList.size()) {
                            auto m = regexList[ti].match(cellText);
                            matched = m.hasMatch();
                        }
                        matchType = "Regex";
                        break;

                    case SearchMode::Fuzzy:
                        matched = isFuzzyMatch(
                            request.caseSensitive ? term : term.toLower(),
                            request.caseSensitive ? cellText : cellText.toLower(),
                            request.fuzzyThreshold);
                        matchType = "Fuzzy";
                        break;
                    }

                    if (matched) {
                        SearchMatch sm;
                        sm.term       = term;
                        sm.sheetName  = sheet;
                        sm.row        = row;
                        sm.col        = col;
                        sm.colLetter  = m_handler->columnToLetter(col);
                        sm.cellValue  = cellText;
                        sm.exactMatch = (request.mode != SearchMode::Fuzzy);
                        sm.matchType  = matchType;
                        results.append(sm);

                        if (request.mode != SearchMode::Fuzzy)
                            exactMatched.insert(term.toLower());
                    }
                    // Append fuzzy suggestions for terms not yet exactly matched
                    else if (request.includeFuzzy
                             && request.mode != SearchMode::Fuzzy
                             && !exactMatched.contains(term.toLower())
                             && isFuzzyMatch(term.toLower(), cellText.toLower(),
                                             request.fuzzyThreshold))
                    {
                        SearchMatch sm;
                        sm.term       = term;
                        sm.sheetName  = sheet;
                        sm.row        = row;
                        sm.col        = col;
                        sm.colLetter  = m_handler->columnToLetter(col);
                        sm.cellValue  = cellText;
                        sm.exactMatch = false;
                        sm.matchType  = "Fuzzy";
                        results.append(sm);
                    }
                }
            }

            ++progressCounter;
            int pct = (totalRows > 0) ? (progressCounter * 100 / totalRows) : 100;
            if (pct != lastPct) {
                lastPct = pct;
                emit progress(pct, 100,
                              QString("Searching sheet '%1' row %2…").arg(sheet).arg(row));
            }
        }
    }

done:
    return results;
}