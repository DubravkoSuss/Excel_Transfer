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

class MainWindow;
class ExcelHandler;
class ExcelSearchService;

class ExcelSearchTab : public QWidget
{
    Q_OBJECT

public:
    explicit ExcelSearchTab(MainWindow* mainWindow, QWidget* parent = nullptr);

private slots:
    void onBrowseFile();
    void onFileLoaded();
    void onAddSearchTerm();
    void onRemoveSelectedTerms();
    void onSearch();
    void onSearchProgress(int current, int total, const QString& message);
    void onClearResults();

private:
    void setupUI();
    QWidget* createFileSection();
    QWidget* createSearchTermsSection();
    QWidget* createResultsSection();

    MainWindow*         m_mainWindow = nullptr;
    ExcelHandler*       m_handler    = nullptr;
    ExcelSearchService* m_searchService = nullptr;

    // File selection
    QLineEdit*    m_filePathEdit   = nullptr;
    QPushButton*  m_browseBtn      = nullptr;
    QPushButton*  m_loadBtn        = nullptr;
    QComboBox*    m_sheetCombo     = nullptr;
    QLabel*       m_fileStatusLabel = nullptr;

    // Search terms table
    QTableWidget* m_termsTable     = nullptr;
    QPushButton*  m_addTermBtn     = nullptr;
    QPushButton*  m_removeTermBtn  = nullptr;

    // Search controls
    QPushButton*  m_searchBtn      = nullptr;
    QProgressBar* m_progressBar    = nullptr;
    QLabel*       m_statusLabel    = nullptr;

    // Results table
    QTableWidget* m_resultsTable   = nullptr;
    QPushButton*  m_clearResultsBtn = nullptr;

    // State
    QString       m_loadedFileKey;
    bool          m_fileLoaded = false;
};

#endif // EXCELSEARCHTAB_H
