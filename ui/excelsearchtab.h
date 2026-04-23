#ifndef EXCELSEARCHTAB_H
#define EXCELSEARCHTAB_H

#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QSpinBox>
#include <QPlainTextEdit>
#include <QFrame>
#include <QSplitter>
#include "../features/search/excelsearchservice.h"

class MainWindow;
class ExcelHandler;
class ExcelSearchService;
class SearchWorker;

class ExcelSearchTab : public QWidget
{
    Q_OBJECT

public:
    explicit ExcelSearchTab(MainWindow* mainWindow, QWidget* parent = nullptr);

private slots:
    void onBrowseFile();
    void onLoadFile();
    void onAddTerm();
    void onRemoveTerms();
    void onClearTerms();
    void onSearch();
    void onCancel();
    void onClearResults();
    void onCopyResults();
    void onExportCsv();
    void onQuickTermEntered();

    void onSearchProgress(int current, int total, const QString& message);
    void onSearchFinished(bool success, const QString& message,
                          QVector<SearchMatch> results);

private:
    void setupUI();
    void setSearchRunning(bool running);
    void populateResults(const QVector<SearchMatch>& results);
    void applyRowFilter(const QString& filterText);

    MainWindow*         m_mainWindow    = nullptr;
    ExcelHandler*       m_handler       = nullptr;
    SearchWorker*       m_worker        = nullptr;

    // ── File panel ────────────────────────────────────────────────────────
    QLineEdit*    m_filePathEdit        = nullptr;
    QPushButton*  m_browseBtn           = nullptr;
    QPushButton*  m_loadBtn             = nullptr;
    QComboBox*    m_sheetCombo          = nullptr;
    QLabel*       m_fileStatusLabel     = nullptr;

    // ── Search options ────────────────────────────────────────────────────
    QComboBox*    m_modeCombo           = nullptr;   // Exact / Contains / Regex / Fuzzy
    QCheckBox*    m_caseSensCheck       = nullptr;
    QCheckBox*    m_fuzzyCheck          = nullptr;   // include fuzzy alongside main mode
    QLineEdit*    m_columnEdit          = nullptr;
    QSpinBox*     m_maxRowSpin          = nullptr;
    QSpinBox*     m_maxColSpin          = nullptr;

    // ── Quick-add term field ──────────────────────────────────────────────
    QLineEdit*    m_quickTermEdit       = nullptr;
    QPushButton*  m_addTermBtn          = nullptr;
    QPushButton*  m_removeTermBtn       = nullptr;
    QPushButton*  m_clearTermsBtn       = nullptr;
    QTableWidget* m_termsTable          = nullptr;

    // ── Execute bar ───────────────────────────────────────────────────────
    QPushButton*  m_searchBtn           = nullptr;
    QPushButton*  m_cancelBtn           = nullptr;
    QProgressBar* m_progressBar         = nullptr;
    QLabel*       m_statusLabel         = nullptr;

    // ── Results ───────────────────────────────────────────────────────────
    QLineEdit*    m_filterEdit          = nullptr;
    QTableWidget* m_resultsTable        = nullptr;
    QLabel*       m_resultCountLabel    = nullptr;
    QPushButton*  m_clearResultsBtn     = nullptr;
    QPushButton*  m_copyBtn             = nullptr;
    QPushButton*  m_exportCsvBtn        = nullptr;

    // ── State ─────────────────────────────────────────────────────────────
    QString       m_loadedFileKey;
    QString       m_loadedFilePath;
    bool          m_fileLoaded          = false;
    QVector<SearchMatch> m_allResults;  // full result set for filtering
};

#endif // EXCELSEARCHTAB_H