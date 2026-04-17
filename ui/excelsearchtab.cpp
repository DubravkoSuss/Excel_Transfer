#include "excelsearchtab.h"
#include "mainwindow.h"
#include "../services/excelhandler.h"
#include "../features/search/excelsearchservice.h"

#include <QFileDialog>
#include <QHeaderView>
#include <QMessageBox>
#include <QGroupBox>
#include <QApplication>
#include <thread>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>

ExcelSearchTab::ExcelSearchTab(MainWindow* mainWindow, QWidget* parent)
    : QWidget(parent), m_mainWindow(mainWindow)
{
    // Retrieve the excel handler from MainWindow (assuming there is a way or we instantiate one)
    // For now, we'll instantiate our own or assume we can get it. Let's ask MainWindow?
    // MappingsManager uses its own, let's just create a new one for search to not block others,
    // or access it if it's public. 
    // Actually, creating a new ExcelHandler instance for search is safer.
    m_handler = new ExcelHandler(this);
    m_searchService = new ExcelSearchService(m_handler, this);

    connect(m_searchService, &ExcelSearchService::progress, this, &ExcelSearchTab::onSearchProgress);

    setupUI();
}

void ExcelSearchTab::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(16);

    mainLayout->addWidget(createFileSection());
    mainLayout->addWidget(createSearchTermsSection());
    mainLayout->addWidget(createResultsSection());

    // Search Controls underneath terms
    QHBoxLayout* searchLayout = new QHBoxLayout();
    m_searchBtn = new QPushButton("Search", this);
    m_searchBtn->setMinimumHeight(40);
    m_searchBtn->setProperty("class", "primary-button");
    m_searchBtn->setEnabled(false);
    connect(m_searchBtn, &QPushButton::clicked, this, &ExcelSearchTab::onSearch);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setVisible(false);

    m_statusLabel = new QLabel("Ready", this);

    searchLayout->addWidget(m_searchBtn);
    searchLayout->addWidget(m_progressBar);
    searchLayout->addWidget(m_statusLabel);
    
    mainLayout->addLayout(searchLayout);
}

QWidget* ExcelSearchTab::createFileSection()
{
    QGroupBox* group = new QGroupBox("1. Select Excel File to Search", this);
    QVBoxLayout* layout = new QVBoxLayout(group);

    QHBoxLayout* fileLayout = new QHBoxLayout();
    m_filePathEdit = new QLineEdit(this);
    m_filePathEdit->setReadOnly(true);
    m_filePathEdit->setPlaceholderText("No file selected...");

    m_browseBtn = new QPushButton("Browse...", this);
    connect(m_browseBtn, &QPushButton::clicked, this, &ExcelSearchTab::onBrowseFile);

    m_loadBtn = new QPushButton("Load Sheets", this);
    m_loadBtn->setEnabled(false);
    connect(m_loadBtn, &QPushButton::clicked, this, &ExcelSearchTab::onFileLoaded);

    fileLayout->addWidget(m_filePathEdit);
    fileLayout->addWidget(m_browseBtn);
    fileLayout->addWidget(m_loadBtn);

    QHBoxLayout* sheetLayout = new QHBoxLayout();
    QLabel* sheetLabel = new QLabel("Target Sheet:", this);
    m_sheetCombo = new QComboBox(this);
    m_sheetCombo->setEnabled(false);
    m_sheetCombo->addItem("All Sheets");

    m_fileStatusLabel = new QLabel("", this);

    sheetLayout->addWidget(sheetLabel);
    sheetLayout->addWidget(m_sheetCombo);
    sheetLayout->addWidget(m_fileStatusLabel);
    sheetLayout->addStretch();

    layout->addLayout(fileLayout);
    layout->addLayout(sheetLayout);

    return group;
}

QWidget* ExcelSearchTab::createSearchTermsSection()
{
    QGroupBox* group = new QGroupBox("2. Search Terms", this);
    QVBoxLayout* layout = new QVBoxLayout(group);

    m_termsTable = new QTableWidget(0, 1, this);
    m_termsTable->setHorizontalHeaderLabels({"Search Term"});
    m_termsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_termsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_termsTable->setAlternatingRowColors(true);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_addTermBtn = new QPushButton("+ Add Term", this);
    m_removeTermBtn = new QPushButton("- Remove Selected", this);

    connect(m_addTermBtn, &QPushButton::clicked, this, &ExcelSearchTab::onAddSearchTerm);
    connect(m_removeTermBtn, &QPushButton::clicked, this, &ExcelSearchTab::onRemoveSelectedTerms);

    btnLayout->addWidget(m_addTermBtn);
    btnLayout->addWidget(m_removeTermBtn);
    btnLayout->addStretch();

    layout->addWidget(m_termsTable);
    layout->addLayout(btnLayout);

    return group;
}

QWidget* ExcelSearchTab::createResultsSection()
{
    QGroupBox* group = new QGroupBox("3. Search Results", this);
    QVBoxLayout* layout = new QVBoxLayout(group);

    m_resultsTable = new QTableWidget(0, 5, this);
    m_resultsTable->setHorizontalHeaderLabels({"Search Term", "Status", "Sheet", "Cell", "Value Found"});
    m_resultsTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_resultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    m_clearResultsBtn = new QPushButton("Clear Results", this);
    connect(m_clearResultsBtn, &QPushButton::clicked, this, &ExcelSearchTab::onClearResults);

    layout->addWidget(m_resultsTable);
    
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    btnLayout->addWidget(m_clearResultsBtn);
    layout->addLayout(btnLayout);

    return group;
}

void ExcelSearchTab::onBrowseFile()
{
    QString path = QFileDialog::getOpenFileName(this, "Select Excel File", "", "Excel Files (*.xlsx *.xls *.xlsm)");
    if (path.isEmpty()) return;

    m_filePathEdit->setText(path);
    m_loadBtn->setEnabled(true);
    m_fileLoaded = false;
    m_sheetCombo->setEnabled(false);
    m_searchBtn->setEnabled(false);
    m_fileStatusLabel->setText("File selected. Click 'Load Sheets'.");
}

void ExcelSearchTab::onFileLoaded()
{
    QString path = m_filePathEdit->text();
    if (path.isEmpty()) return;

    m_loadBtn->setEnabled(false);
    m_fileStatusLabel->setText("Loading...");
    QApplication::processEvents();

    m_loadedFileKey = "search_target_" + QString::number(QDateTime::currentMSecsSinceEpoch());
    
    bool loaded = m_handler->loadWorkbook(m_loadedFileKey, path);
    if (!loaded) {
        QMessageBox::critical(this, "Error", "Failed to load Excel file.");
        m_loadBtn->setEnabled(true);
        m_fileStatusLabel->setText("Failed to load.");
        return;
    }

    m_sheetCombo->clear();
    m_sheetCombo->addItem("All Sheets"); // index 0
    QStringList sheets = m_handler->getSheetNames(m_loadedFileKey);
    m_sheetCombo->addItems(sheets);

    m_sheetCombo->setEnabled(true);
    m_searchBtn->setEnabled(true);
    m_fileLoaded = true;
    m_fileStatusLabel->setText("File loaded successfully.");
}

void ExcelSearchTab::onAddSearchTerm()
{
    int row = m_termsTable->rowCount();
    m_termsTable->insertRow(row);
    QTableWidgetItem* item = new QTableWidgetItem("");
    m_termsTable->setItem(row, 0, item);
    m_termsTable->editItem(item);
}

void ExcelSearchTab::onRemoveSelectedTerms()
{
    QList<QTableWidgetItem*> selected = m_termsTable->selectedItems();
    QSet<int> rowsToDel;
    for (auto* item : selected) rowsToDel.insert(item->row());

    QList<int> rowsList = rowsToDel.values();
    std::sort(rowsList.begin(), rowsList.end(), std::greater<int>());

    for (int row : rowsList) {
        m_termsTable->removeRow(row);
    }
}

void ExcelSearchTab::onClearResults()
{
    m_resultsTable->setRowCount(0);
}

void ExcelSearchTab::onSearchProgress(int current, int total, const QString& message)
{
    m_progressBar->setMaximum(total);
    m_progressBar->setValue(current);
    m_statusLabel->setText(message);
}

void ExcelSearchTab::onSearch()
{
    if (!m_fileLoaded) return;

    QStringList terms;
    for (int i = 0; i < m_termsTable->rowCount(); ++i) {
        QTableWidgetItem* item = m_termsTable->item(i, 0);
        if (item && !item->text().trimmed().isEmpty()) {
            terms.append(item->text().trimmed());
        }
    }

    if (terms.isEmpty()) {
        QMessageBox::warning(this, "No Terms", "Please add at least one search term.");
        return;
    }

    m_searchBtn->setEnabled(false);
    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);
    onClearResults();

    SearchRequest req;
    req.terms = terms;
    req.fileKey = m_loadedFileKey;
    
    // If specific sheet selected
    if (m_sheetCombo->currentIndex() > 0) {
        req.sheets.append(m_sheetCombo->currentText());
    }

    // Run search (this blocks the UI, could be moved to QThread but fine for POC)
    // To keep UI responsive we'll use QtConcurrent if possible, but let's just 
    // run it synchronously first with processEvents in the progress signal.
    
    m_statusLabel->setText("Searching...");
    QApplication::processEvents();

    QVector<SearchMatch> results = m_searchService->search(req);

    m_resultsTable->setRowCount(results.size());
    for (int i = 0; i < results.size(); ++i) {
        const auto& match = results[i];
        
        m_resultsTable->setItem(i, 0, new QTableWidgetItem(match.term));
        
        QTableWidgetItem* statusItem = new QTableWidgetItem(match.exactMatch ? "Exact Match" : "Did you mean?");
        if (!match.exactMatch) {
            statusItem->setForeground(QBrush(Qt::darkYellow));
            QFont f = statusItem->font();
            f.setItalic(true);
            statusItem->setFont(f);
        } else {
            statusItem->setForeground(QBrush(Qt::darkGreen));
        }
        m_resultsTable->setItem(i, 1, statusItem);
        
        m_resultsTable->setItem(i, 2, new QTableWidgetItem(match.sheetName));
        m_resultsTable->setItem(i, 3, new QTableWidgetItem(QString("%1%2").arg(match.colLetter).arg(match.row)));
        m_resultsTable->setItem(i, 4, new QTableWidgetItem(match.cellValue));
    }

    m_searchBtn->setEnabled(true);
    m_progressBar->setVisible(false);
    m_statusLabel->setText(QString("Found %1 matches.").arg(results.size()));
}
