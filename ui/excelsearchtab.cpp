#include "excelsearchtab.h"
#include "mainwindow.h"
#include "../services/excelhandler.h"
#include "../features/search/excelsearchservice.h"
#include "../features/search/searchworker.h"

#include <QFileDialog>
#include <QHeaderView>
#include <QMessageBox>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QFileInfo>
#include <QTextStream>
#include <QScrollBar>
#include <QKeyEvent>

// ═══════════════════════════════════════════════════════════════════════════
// Stylesheet helpers (same design language as other tabs)
// ═══════════════════════════════════════════════════════════════════════════

static QString s_btnPrimary() {
    return "QPushButton {"
           "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
           "    stop:0 #3B82F6, stop:1 #2563EB);"
           "  color:white; font-weight:600; font-size:12px;"
           "  padding:7px 18px; border-radius:6px; border:none; }"
           "QPushButton:hover { background:#2563EB; }"
           "QPushButton:pressed { background:#1E40AF; }"
           "QPushButton:disabled { background:#E5E7EB; color:#9CA3AF; }";
}

static QString s_btnSuccess() {
    return "QPushButton {"
           "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
           "    stop:0 #059669, stop:1 #047857);"
           "  color:white; font-weight:700; font-size:14px;"
           "  padding:10px 28px; border-radius:8px; border:none; }"
           "QPushButton:hover { background:#047857; }"
           "QPushButton:pressed { background:#064E3B; }"
           "QPushButton:disabled { background:#E5E7EB; color:#9CA3AF; }";
}

static QString s_btnDanger() {
    return "QPushButton {"
           "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
           "    stop:0 #EF4444, stop:1 #DC2626);"
           "  color:white; font-weight:600; font-size:12px;"
           "  padding:7px 18px; border-radius:6px; border:none; }"
           "QPushButton:hover { background:#DC2626; }"
           "QPushButton:pressed { background:#991B1B; }"
           "QPushButton:disabled { background:#E5E7EB; color:#9CA3AF; }";
}

static QString s_btnNeutral() {
    return "QPushButton {"
           "  background:white; border:1px solid #D1D5DB; color:#374151;"
           "  font-size:12px; padding:5px 12px; border-radius:5px; font-weight:500; }"
           "QPushButton:hover { background:#F3F4F6; border-color:#3B82F6; }"
           "QPushButton:disabled { background:#F9FAFB; color:#9CA3AF; }";
}

static QString s_cardStyle() {
    return "QFrame { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
           "  stop:0 #FAFBFC, stop:1 #F5F7FA);"
           "  border-radius:10px; border:1px solid #E5E7EB; }";
}

static QString s_inputStyle() {
    return "QLineEdit { background:white; border:1px solid #D1D5DB; border-radius:6px;"
           "  padding:5px 9px; font-size:12px; color:#111827; }"
           "QLineEdit:focus { border-color:#3B82F6; }"
           "QLineEdit:read-only { background:#F9FAFB; color:#6B7280; }";
}

static QString s_comboStyle() {
    return "QComboBox { background:white; border:1px solid #D1D5DB; border-radius:6px;"
           "  padding:5px 9px; font-size:12px; color:#111827; }"
           "QComboBox::drop-down { border:none; width:20px; }"
           "QComboBox:hover { border-color:#3B82F6; }";
}

static QString s_spinStyle() {
    return "QSpinBox { background:white; border:1px solid #D1D5DB; border-radius:6px;"
           "  padding:4px 7px; font-size:12px; color:#111827; min-width:70px; }"
           "QSpinBox:focus { border-color:#3B82F6; }"
           "QSpinBox::up-button, QSpinBox::down-button { border:none; width:16px; }";
}

static QString s_labelStyle() {
    return "font-weight:600; color:#374151; font-size:12px;";
}

static QString s_tableStyle() {
    return "QTableWidget { background:white; gridline-color:#E5E7EB;"
           "  border-radius:8px; font-size:12px; border:1px solid #E5E7EB; }"
           "QTableWidget::item { padding:6px 8px; }"
           "QTableWidget::item:selected { background:#DBEAFE; color:#1E40AF; }"
           "QTableWidget::item:alternate { background:#F9FAFB; }"
           "QHeaderView::section { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
           "    stop:0 #F9FAFB, stop:1 #F3F4F6); color:#374151; padding:8px;"
           "  font-weight:600; border:none; border-bottom:2px solid #E5E7EB; }";
}

// ═══════════════════════════════════════════════════════════════════════════
// Constructor
// ═══════════════════════════════════════════════════════════════════════════

ExcelSearchTab::ExcelSearchTab(MainWindow* mainWindow, QWidget* parent)
    : QWidget(parent), m_mainWindow(mainWindow)
{
    m_handler = new ExcelHandler(this);
    setupUI();
}

// ═══════════════════════════════════════════════════════════════════════════
// setupUI
// ═══════════════════════════════════════════════════════════════════════════

void ExcelSearchTab::setupUI()
{
    QVBoxLayout* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // ── Title bar ─────────────────────────────────────────────────────────
    QFrame* titleBar = new QFrame(this);
    titleBar->setStyleSheet(
        "QFrame { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "  stop:0 #1E3A5F, stop:1 #0F766E); border-radius:0; }");
    titleBar->setFixedHeight(60);
    QHBoxLayout* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(20, 0, 20, 0);

    QLabel* icon = new QLabel("🔍", titleBar);
    icon->setStyleSheet("font-size:22px;");
    QLabel* title = new QLabel("Excel Search", titleBar);
    title->setStyleSheet("font-weight:700; font-size:16px; color:white; letter-spacing:-0.3px;");
    QLabel* desc = new QLabel(
        "Search for terms across sheets with Exact, Contains, Regex and Fuzzy modes.", titleBar);
    desc->setStyleSheet("color:rgba(255,255,255,0.75); font-size:11px;");

    titleLayout->addWidget(icon);
    titleLayout->addSpacing(8);
    QVBoxLayout* ttl = new QVBoxLayout();
    ttl->setSpacing(1);
    ttl->addWidget(title);
    ttl->addWidget(desc);
    titleLayout->addLayout(ttl);
    titleLayout->addStretch();
    rootLayout->addWidget(titleBar);

    // ── Main splitter: left config | right results ─────────────────────────
    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(5);
    splitter->setStyleSheet(
        "QSplitter::handle { background:#E5E7EB; }"
        "QSplitter::handle:hover { background:#3B82F6; }");
    rootLayout->addWidget(splitter, 1);

    // ═══════════════════════════════════════════
    // LEFT PANEL — config + terms
    // ═══════════════════════════════════════════
    QWidget* leftPanel = new QWidget();
    leftPanel->setMinimumWidth(340);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(16, 16, 10, 16);
    leftLayout->setSpacing(12);

    // ── Card: File ────────────────────────────────────────────────────────
    QFrame* fileCard = new QFrame();
    fileCard->setStyleSheet(s_cardStyle());
    QVBoxLayout* fileCardLayout = new QVBoxLayout(fileCard);
    fileCardLayout->setContentsMargins(14, 12, 14, 12);
    fileCardLayout->setSpacing(8);

    QLabel* fileTitleLbl = new QLabel("📂  Workbook", fileCard);
    fileTitleLbl->setStyleSheet("font-weight:700; font-size:13px; color:#1F2937;");
    fileCardLayout->addWidget(fileTitleLbl);

    QHBoxLayout* fileRow = new QHBoxLayout();
    m_filePathEdit = new QLineEdit(fileCard);
    m_filePathEdit->setReadOnly(true);
    m_filePathEdit->setPlaceholderText("Select an Excel file…");
    m_filePathEdit->setStyleSheet(s_inputStyle());
    m_browseBtn = new QPushButton("Browse…", fileCard);
    m_browseBtn->setStyleSheet(s_btnNeutral());
    m_browseBtn->setFixedWidth(80);
    m_loadBtn = new QPushButton("Load", fileCard);
    m_loadBtn->setStyleSheet(s_btnPrimary());
    m_loadBtn->setFixedWidth(60);
    m_loadBtn->setEnabled(false);
    fileRow->addWidget(m_filePathEdit);
    fileRow->addWidget(m_browseBtn);
    fileRow->addWidget(m_loadBtn);
    fileCardLayout->addLayout(fileRow);

    QHBoxLayout* sheetRow = new QHBoxLayout();
    QLabel* shLbl = new QLabel("Sheet:", fileCard);
    shLbl->setStyleSheet(s_labelStyle());
    m_sheetCombo = new QComboBox(fileCard);
    m_sheetCombo->setStyleSheet(s_comboStyle());
    m_sheetCombo->setMaxVisibleItems(14);
    m_sheetCombo->addItem("All Sheets");
    m_sheetCombo->setEnabled(false);

    m_fileStatusLabel = new QLabel("No file loaded.", fileCard);
    m_fileStatusLabel->setStyleSheet("color:#6B7280; font-size:11px; font-style:italic;");

    sheetRow->addWidget(shLbl);
    sheetRow->addWidget(m_sheetCombo, 1);
    sheetRow->addSpacing(8);
    sheetRow->addWidget(m_fileStatusLabel, 1);
    fileCardLayout->addLayout(sheetRow);
    leftLayout->addWidget(fileCard);

    // ── Card: Search Options ──────────────────────────────────────────────
    QFrame* optCard = new QFrame();
    optCard->setStyleSheet(s_cardStyle());
    QVBoxLayout* optCardLayout = new QVBoxLayout(optCard);
    optCardLayout->setContentsMargins(14, 12, 14, 12);
    optCardLayout->setSpacing(8);

    QLabel* optTitleLbl = new QLabel("⚙️  Search Options", optCard);
    optTitleLbl->setStyleSheet("font-weight:700; font-size:13px; color:#1F2937;");
    optCardLayout->addWidget(optTitleLbl);

    // Mode row
    QHBoxLayout* modeRow = new QHBoxLayout();
    QLabel* modeLbl = new QLabel("Mode:", optCard);
    modeLbl->setStyleSheet(s_labelStyle());
    m_modeCombo = new QComboBox(optCard);
    m_modeCombo->setStyleSheet(s_comboStyle());
    m_modeCombo->addItems({"Contains", "Exact", "Regex", "Fuzzy"});
    m_modeCombo->setToolTip(
        "Contains — cell text includes the term\n"
        "Exact    — cell text equals the term\n"
        "Regex    — term is a regular expression\n"
        "Fuzzy    — Levenshtein distance matching");
    m_caseSensCheck = new QCheckBox("Case sensitive", optCard);
    m_caseSensCheck->setStyleSheet("font-size:12px; color:#374151;");
    m_fuzzyCheck = new QCheckBox("Also suggest fuzzy", optCard);
    m_fuzzyCheck->setChecked(true);
    m_fuzzyCheck->setStyleSheet("font-size:12px; color:#374151;");
    m_fuzzyCheck->setToolTip("Append fuzzy suggestions for terms that had no exact/contains match.");
    modeRow->addWidget(modeLbl);
    modeRow->addWidget(m_modeCombo);
    modeRow->addSpacing(8);
    modeRow->addWidget(m_caseSensCheck);
    modeRow->addSpacing(8);
    modeRow->addWidget(m_fuzzyCheck);
    modeRow->addStretch();
    optCardLayout->addLayout(modeRow);

    // Restrict column + scan limits row
    QHBoxLayout* limRow = new QHBoxLayout();
    QLabel* colLbl = new QLabel("Column:", optCard);
    colLbl->setStyleSheet(s_labelStyle());
    m_columnEdit = new QLineEdit(optCard);
    m_columnEdit->setPlaceholderText("e.g. B");
    m_columnEdit->setFixedWidth(56);
    m_columnEdit->setStyleSheet(s_inputStyle());
    m_columnEdit->setToolTip("Leave blank to search all columns.");

    QLabel* maxRowLbl = new QLabel("Max rows:", optCard);
    maxRowLbl->setStyleSheet(s_labelStyle());
    m_maxRowSpin = new QSpinBox(optCard);
    m_maxRowSpin->setRange(1, 100000);
    m_maxRowSpin->setValue(5000);
    m_maxRowSpin->setStyleSheet(s_spinStyle());

    QLabel* maxColLbl = new QLabel("Max cols:", optCard);
    maxColLbl->setStyleSheet(s_labelStyle());
    m_maxColSpin = new QSpinBox(optCard);
    m_maxColSpin->setRange(1, 10000);
    m_maxColSpin->setValue(500);
    m_maxColSpin->setStyleSheet(s_spinStyle());

    limRow->addWidget(colLbl);
    limRow->addWidget(m_columnEdit);
    limRow->addSpacing(10);
    limRow->addWidget(maxRowLbl);
    limRow->addWidget(m_maxRowSpin);
    limRow->addSpacing(10);
    limRow->addWidget(maxColLbl);
    limRow->addWidget(m_maxColSpin);
    limRow->addStretch();
    optCardLayout->addLayout(limRow);
    leftLayout->addWidget(optCard);

    // ── Card: Search Terms ─────────────────────────────────────────────────
    QFrame* termsCard = new QFrame();
    termsCard->setStyleSheet(s_cardStyle());
    QVBoxLayout* termsCardLayout = new QVBoxLayout(termsCard);
    termsCardLayout->setContentsMargins(14, 12, 14, 12);
    termsCardLayout->setSpacing(8);

    QLabel* termsTitleLbl = new QLabel("📝  Search Terms", termsCard);
    termsTitleLbl->setStyleSheet("font-weight:700; font-size:13px; color:#1F2937;");
    termsCardLayout->addWidget(termsTitleLbl);

    // Quick-add row
    QHBoxLayout* quickRow = new QHBoxLayout();
    m_quickTermEdit = new QLineEdit(termsCard);
    m_quickTermEdit->setPlaceholderText("Type a term and press Enter or click Add…");
    m_quickTermEdit->setStyleSheet(s_inputStyle());
    m_addTermBtn = new QPushButton("+ Add", termsCard);
    m_addTermBtn->setStyleSheet(s_btnPrimary());
    m_addTermBtn->setFixedWidth(80);
    quickRow->addWidget(m_quickTermEdit);
    quickRow->addWidget(m_addTermBtn);
    termsCardLayout->addLayout(quickRow);

    // Terms table
    m_termsTable = new QTableWidget(0, 1, termsCard);
    m_termsTable->setHorizontalHeaderLabels({"Search Term"});
    m_termsTable->horizontalHeader()->setStretchLastSection(true);
    m_termsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_termsTable->setAlternatingRowColors(true);
    m_termsTable->setStyleSheet(s_tableStyle());
    m_termsTable->setMaximumHeight(180);
    termsCardLayout->addWidget(m_termsTable);

    // Terms action buttons
    QHBoxLayout* termsBtnRow = new QHBoxLayout();
    m_removeTermBtn = new QPushButton("− Remove Selected", termsCard);
    m_removeTermBtn->setStyleSheet(s_btnNeutral());
    m_clearTermsBtn = new QPushButton("✕ Clear All", termsCard);
    m_clearTermsBtn->setStyleSheet(s_btnNeutral());
    termsBtnRow->addWidget(m_removeTermBtn);
    termsBtnRow->addWidget(m_clearTermsBtn);
    termsBtnRow->addStretch();
    termsCardLayout->addLayout(termsBtnRow);
    leftLayout->addWidget(termsCard);
    leftLayout->addStretch();

    // ── Execute bar ───────────────────────────────────────────────────────
    QFrame* execFrame = new QFrame();
    execFrame->setStyleSheet("QFrame { background:transparent; border:none; }");
    QHBoxLayout* execLayout = new QHBoxLayout(execFrame);
    execLayout->setContentsMargins(0, 0, 0, 0);
    execLayout->setSpacing(10);

    m_searchBtn = new QPushButton("🔍  Search", execFrame);
    m_searchBtn->setStyleSheet(s_btnSuccess());
    m_searchBtn->setMinimumHeight(42);
    m_searchBtn->setEnabled(false);

    m_cancelBtn = new QPushButton("⬛ Cancel", execFrame);
    m_cancelBtn->setStyleSheet(s_btnDanger());
    m_cancelBtn->setVisible(false);

    execLayout->addWidget(m_searchBtn);
    execLayout->addWidget(m_cancelBtn);
    execLayout->addStretch();
    leftLayout->addWidget(execFrame);

    // Progress + status
    m_progressBar = new QProgressBar(leftPanel);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(false);
    m_progressBar->setStyleSheet(
        "QProgressBar { background:#E5E7EB; border-radius:5px; height:10px;"
        "  text-align:center; font-size:10px; color:#374151; }"
        "QProgressBar::chunk { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "  stop:0 #0F766E, stop:1 #3B82F6); border-radius:5px; }");
    leftLayout->addWidget(m_progressBar);

    m_statusLabel = new QLabel("Load a workbook and add search terms.", leftPanel);
    m_statusLabel->setStyleSheet("color:#6B7280; font-size:11px; font-style:italic;");
    m_statusLabel->setWordWrap(true);
    leftLayout->addWidget(m_statusLabel);

    splitter->addWidget(leftPanel);

    // ═══════════════════════════════════════════
    // RIGHT PANEL — Results
    // ═══════════════════════════════════════════
    QWidget* rightPanel = new QWidget();
    rightPanel->setMinimumWidth(400);
    QVBoxLayout* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(10, 16, 16, 16);
    rightLayout->setSpacing(10);

    // Results header row
    QHBoxLayout* resHeaderRow = new QHBoxLayout();
    QLabel* resTitleLbl = new QLabel("📊  Results", rightPanel);
    resTitleLbl->setStyleSheet("font-weight:700; font-size:13px; color:#1F2937;");
    m_resultCountLabel = new QLabel("", rightPanel);
    m_resultCountLabel->setStyleSheet("color:#6B7280; font-size:11px;");
    resHeaderRow->addWidget(resTitleLbl);
    resHeaderRow->addWidget(m_resultCountLabel);
    resHeaderRow->addStretch();

    // Filter
    QLabel* filterLbl = new QLabel("Filter:", rightPanel);
    filterLbl->setStyleSheet(s_labelStyle());
    m_filterEdit = new QLineEdit(rightPanel);
    m_filterEdit->setPlaceholderText("Filter results…");
    m_filterEdit->setFixedWidth(180);
    m_filterEdit->setStyleSheet(s_inputStyle());
    resHeaderRow->addWidget(filterLbl);
    resHeaderRow->addWidget(m_filterEdit);
    rightLayout->addLayout(resHeaderRow);

    // Results table — 6 columns
    m_resultsTable = new QTableWidget(0, 6, rightPanel);
    m_resultsTable->setHorizontalHeaderLabels(
        {"Term", "Type", "Sheet", "Cell", "Value", "Match"});
    m_resultsTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    m_resultsTable->horizontalHeader()->setStretchLastSection(false);
    m_resultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultsTable->setAlternatingRowColors(true);
    m_resultsTable->setSortingEnabled(true);
    m_resultsTable->setStyleSheet(s_tableStyle());
    // Column widths
    m_resultsTable->setColumnWidth(0, 120);
    m_resultsTable->setColumnWidth(1, 72);
    m_resultsTable->setColumnWidth(2, 110);
    m_resultsTable->setColumnWidth(3, 60);
    m_resultsTable->setColumnWidth(5, 72);
    rightLayout->addWidget(m_resultsTable, 1);

    // Bottom action row
    QHBoxLayout* actRow = new QHBoxLayout();
    m_clearResultsBtn = new QPushButton("✕ Clear", rightPanel);
    m_clearResultsBtn->setStyleSheet(s_btnNeutral());
    m_copyBtn = new QPushButton("📋 Copy", rightPanel);
    m_copyBtn->setStyleSheet(s_btnNeutral());
    m_copyBtn->setToolTip("Copy all results to clipboard as tab-separated text.");
    m_exportCsvBtn = new QPushButton("💾 Export CSV…", rightPanel);
    m_exportCsvBtn->setStyleSheet(s_btnNeutral());
    m_exportCsvBtn->setToolTip("Save results to a CSV file.");
    actRow->addWidget(m_clearResultsBtn);
    actRow->addStretch();
    actRow->addWidget(m_copyBtn);
    actRow->addWidget(m_exportCsvBtn);
    rightLayout->addLayout(actRow);

    splitter->addWidget(rightPanel);
    splitter->setSizes({390, 700});
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    // ── Wire signals ──────────────────────────────────────────────────────
    connect(m_browseBtn,    &QPushButton::clicked, this, &ExcelSearchTab::onBrowseFile);
    connect(m_loadBtn,      &QPushButton::clicked, this, &ExcelSearchTab::onLoadFile);
    connect(m_addTermBtn,   &QPushButton::clicked, this, &ExcelSearchTab::onAddTerm);
    connect(m_removeTermBtn,&QPushButton::clicked, this, &ExcelSearchTab::onRemoveTerms);
    connect(m_clearTermsBtn,&QPushButton::clicked, this, &ExcelSearchTab::onClearTerms);
    connect(m_searchBtn,    &QPushButton::clicked, this, &ExcelSearchTab::onSearch);
    connect(m_cancelBtn,    &QPushButton::clicked, this, &ExcelSearchTab::onCancel);
    connect(m_clearResultsBtn,&QPushButton::clicked, this, &ExcelSearchTab::onClearResults);
    connect(m_copyBtn,      &QPushButton::clicked, this, &ExcelSearchTab::onCopyResults);
    connect(m_exportCsvBtn, &QPushButton::clicked, this, &ExcelSearchTab::onExportCsv);
    connect(m_quickTermEdit, &QLineEdit::returnPressed, this, &ExcelSearchTab::onQuickTermEntered);
    connect(m_filterEdit,   &QLineEdit::textChanged, this, &ExcelSearchTab::applyRowFilter);
}

// ═══════════════════════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════════════════════

void ExcelSearchTab::setSearchRunning(bool running)
{
    m_searchBtn->setEnabled(!running);
    m_cancelBtn->setVisible(running);
    m_loadBtn->setEnabled(!running && !m_filePathEdit->text().isEmpty());
    m_browseBtn->setEnabled(!running);
    m_progressBar->setVisible(running);
    if (!running) m_progressBar->setValue(0);
}

void ExcelSearchTab::populateResults(const QVector<SearchMatch>& results)
{
    m_resultsTable->setSortingEnabled(false);
    m_resultsTable->setRowCount(0);

    for (int i = 0; i < results.size(); ++i) {
        const auto& m = results[i];
        m_resultsTable->insertRow(i);

        m_resultsTable->setItem(i, 0, new QTableWidgetItem(m.term));

        // Match type badge
        QTableWidgetItem* typeItem = new QTableWidgetItem(m.matchType);
        if (!m.exactMatch || m.matchType == "Fuzzy") {
            typeItem->setForeground(QColor("#D97706")); // amber
            QFont f = typeItem->font(); f.setItalic(true); typeItem->setFont(f);
        } else {
            typeItem->setForeground(QColor("#059669")); // green
        }
        m_resultsTable->setItem(i, 1, typeItem);

        m_resultsTable->setItem(i, 2, new QTableWidgetItem(m.sheetName));
        m_resultsTable->setItem(i, 3, new QTableWidgetItem(
            QString("%1%2").arg(m.colLetter).arg(m.row)));
        m_resultsTable->setItem(i, 4, new QTableWidgetItem(m.cellValue));

        // Match label
        QTableWidgetItem* matchItem = new QTableWidgetItem(m.exactMatch ? "✅" : "💡");
        matchItem->setTextAlignment(Qt::AlignCenter);
        m_resultsTable->setItem(i, 5, matchItem);
    }

    m_resultsTable->setSortingEnabled(true);
    m_resultCountLabel->setText(QString("(%1 results)").arg(results.size()));
}

void ExcelSearchTab::applyRowFilter(const QString& filterText)
{
    QString lower = filterText.toLower();
    for (int row = 0; row < m_resultsTable->rowCount(); ++row) {
        bool show = false;
        if (lower.isEmpty()) {
            show = true;
        } else {
            for (int col = 0; col < m_resultsTable->columnCount(); ++col) {
                QTableWidgetItem* it = m_resultsTable->item(row, col);
                if (it && it->text().toLower().contains(lower)) {
                    show = true;
                    break;
                }
            }
        }
        m_resultsTable->setRowHidden(row, !show);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Slots
// ═══════════════════════════════════════════════════════════════════════════

void ExcelSearchTab::onBrowseFile()
{
    QString startDir = m_mainWindow ? m_mainWindow->destFolder() : QString();
    QString path = QFileDialog::getOpenFileName(
        this, "Select Excel File", startDir,
        "Excel Files (*.xlsx *.xlsm *.xls);;All Files (*)");
    if (path.isEmpty()) return;

    m_filePathEdit->setText(path);
    m_loadBtn->setEnabled(true);
    m_fileLoaded = false;
    m_sheetCombo->setEnabled(false);
    m_searchBtn->setEnabled(false);
    m_fileStatusLabel->setText("Click Load to load sheets.");
    m_fileStatusLabel->setStyleSheet("color:#D97706; font-size:11px;");
}

void ExcelSearchTab::onLoadFile()
{
    QString path = m_filePathEdit->text().trimmed();
    if (path.isEmpty()) return;

    m_loadBtn->setEnabled(false);
    m_fileStatusLabel->setText("Loading…");
    m_fileStatusLabel->setStyleSheet("color:#6B7280; font-size:11px;");
    QApplication::processEvents();

    // Unload any previous
    if (!m_loadedFileKey.isEmpty())
        m_handler->unloadWorkbook(m_loadedFileKey);

    m_loadedFileKey = "search_preloaded_" + QString::number(QDateTime::currentMSecsSinceEpoch());
    m_loadedFilePath = path;

    bool loaded = m_handler->loadWorkbook(path, m_loadedFileKey);
    if (!loaded) {
        QMessageBox::critical(this, "Load Error", "Failed to load Excel file:\n" + path);
        m_loadBtn->setEnabled(true);
        m_fileStatusLabel->setText("Failed to load.");
        m_fileStatusLabel->setStyleSheet("color:#DC2626; font-size:11px;");
        return;
    }

    m_sheetCombo->clear();
    m_sheetCombo->addItem("All Sheets");
    QStringList sheets = m_handler->getSheetNames(m_loadedFileKey);
    m_sheetCombo->addItems(sheets);
    m_sheetCombo->setEnabled(true);

    m_fileLoaded = true;
    m_searchBtn->setEnabled(true);
    m_loadBtn->setEnabled(true);

    QString fname = QFileInfo(path).fileName();
    m_fileStatusLabel->setText(
        QString("✔ %1 sheet(s) loaded").arg(sheets.size()));
    m_fileStatusLabel->setStyleSheet("color:#059669; font-size:11px; font-weight:600;");
    m_statusLabel->setText(QString("Loaded: %1").arg(fname));
}

void ExcelSearchTab::onQuickTermEntered()
{
    onAddTerm();
}

void ExcelSearchTab::onAddTerm()
{
    QString text = m_quickTermEdit->text().trimmed();
    if (text.isEmpty()) {
        // Fall back to adding an empty editable row
        int row = m_termsTable->rowCount();
        m_termsTable->insertRow(row);
        auto* item = new QTableWidgetItem("");
        m_termsTable->setItem(row, 0, item);
        m_termsTable->editItem(item);
        return;
    }

    // Split on newlines so user can paste multiple terms at once
    QStringList parts = text.split('\n', Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        QString t = part.trimmed();
        if (t.isEmpty()) continue;
        int row = m_termsTable->rowCount();
        m_termsTable->insertRow(row);
        m_termsTable->setItem(row, 0, new QTableWidgetItem(t));
    }
    m_quickTermEdit->clear();
}

void ExcelSearchTab::onRemoveTerms()
{
    QList<QTableWidgetItem*> selected = m_termsTable->selectedItems();
    QSet<int> rows;
    for (auto* it : selected) rows.insert(it->row());
    QList<int> sorted = QList<int>(rows.begin(), rows.end());
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    for (int r : sorted) m_termsTable->removeRow(r);
}

void ExcelSearchTab::onClearTerms()
{
    m_termsTable->setRowCount(0);
}

void ExcelSearchTab::onSearch()
{
    if (!m_fileLoaded) {
        QMessageBox::warning(this, "No File", "Please load a workbook first.");
        return;
    }

    QStringList terms;
    for (int i = 0; i < m_termsTable->rowCount(); ++i) {
        auto* it = m_termsTable->item(i, 0);
        if (it && !it->text().trimmed().isEmpty())
            terms.append(it->text().trimmed());
    }
    if (terms.isEmpty()) {
        QMessageBox::warning(this, "No Terms", "Please add at least one search term.");
        return;
    }

    // Stop previous worker
    if (m_worker) {
        m_worker->requestStop();
        m_worker->wait(3000);
        m_worker->deleteLater();
        m_worker = nullptr;
    }

    // Build request
    SearchRequest req;
    req.terms         = terms;
    req.fileKey       = m_loadedFileKey;  // pre-loaded key
    req.caseSensitive = m_caseSensCheck->isChecked();
    req.includeFuzzy  = m_fuzzyCheck->isChecked();
    req.maxRow        = m_maxRowSpin->value();
    req.maxCol        = m_maxColSpin->value();
    req.searchColumn  = m_columnEdit->text().trimmed();

    // Sheet selection
    if (m_sheetCombo->currentIndex() > 0)
        req.sheets.append(m_sheetCombo->currentText());

    // Mode
    int modeIdx = m_modeCombo->currentIndex();
    switch (modeIdx) {
        case 0: req.mode = SearchMode::Contains; break;
        case 1: req.mode = SearchMode::Exact;    break;
        case 2: req.mode = SearchMode::Regex;    break;
        case 3: req.mode = SearchMode::Fuzzy;    break;
        default: req.mode = SearchMode::Contains; break;
    }

    onClearResults();

    // We use the service directly (file already loaded) via a worker that
    // uses the preloaded key. Pass a special "already loaded" marker via fileKey.
    // SearchWorker will detect "search_preloaded_" prefix and skip load.
    m_worker = new SearchWorker(m_handler, req, this);
    connect(m_worker, &SearchWorker::progress,  this, &ExcelSearchTab::onSearchProgress);
    connect(m_worker, &SearchWorker::finished,  this, &ExcelSearchTab::onSearchFinished);

    setSearchRunning(true);
    m_statusLabel->setText(QString("Searching for %1 term(s)…").arg(terms.size()));
    m_worker->start();
}

void ExcelSearchTab::onCancel()
{
    if (m_worker && m_worker->isRunning()) {
        m_cancelBtn->setEnabled(false);
        m_worker->requestStop();
        m_statusLabel->setText("Cancelling…");
    }
}

void ExcelSearchTab::onClearResults()
{
    m_resultsTable->setRowCount(0);
    m_allResults.clear();
    m_resultCountLabel->clear();
}

void ExcelSearchTab::onCopyResults()
{
    if (m_resultsTable->rowCount() == 0) return;

    QStringList lines;
    // Header
    QStringList header;
    for (int c = 0; c < m_resultsTable->columnCount(); ++c) {
        auto* it = m_resultsTable->horizontalHeaderItem(c);
        header << (it ? it->text() : QString());
    }
    lines << header.join('\t');

    for (int r = 0; r < m_resultsTable->rowCount(); ++r) {
        if (m_resultsTable->isRowHidden(r)) continue;
        QStringList row;
        for (int c = 0; c < m_resultsTable->columnCount(); ++c) {
            auto* it = m_resultsTable->item(r, c);
            row << (it ? it->text() : QString());
        }
        lines << row.join('\t');
    }

    QApplication::clipboard()->setText(lines.join('\n'));
    m_statusLabel->setText(
        QString("Copied %1 rows to clipboard.").arg(lines.size() - 1));
}

void ExcelSearchTab::onExportCsv()
{
    if (m_resultsTable->rowCount() == 0) {
        QMessageBox::information(this, "No Results", "There are no results to export.");
        return;
    }

    QString path = QFileDialog::getSaveFileName(
        this, "Export Results as CSV",
        QString("search_results_%1.csv")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")),
        "CSV Files (*.csv);;All Files (*)");
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Export Error",
            "Cannot write to:\n" + path);
        return;
    }

    QTextStream out(&f);
    // Header
    QStringList hdr;
    for (int c = 0; c < m_resultsTable->columnCount(); ++c) {
        auto* it = m_resultsTable->horizontalHeaderItem(c);
        hdr << "\"" + (it ? it->text() : QString()) + "\"";
    }
    out << hdr.join(',') << "\n";

    for (int r = 0; r < m_resultsTable->rowCount(); ++r) {
        if (m_resultsTable->isRowHidden(r)) continue;
        QStringList row;
        for (int c = 0; c < m_resultsTable->columnCount(); ++c) {
            auto* it = m_resultsTable->item(r, c);
            QString val = it ? it->text() : QString();
            val.replace("\"", "\"\"");
            row << "\"" + val + "\"";
        }
        out << row.join(',') << "\n";
    }

    m_statusLabel->setText("Exported to: " + QFileInfo(path).fileName());
}

void ExcelSearchTab::onSearchProgress(int current, int total, const QString& message)
{
    m_progressBar->setMaximum(total);
    m_progressBar->setValue(current);
    m_statusLabel->setText(message);
}

void ExcelSearchTab::onSearchFinished(bool success, const QString& message,
                                       QVector<SearchMatch> results)
{
    setSearchRunning(false);
    m_cancelBtn->setEnabled(true);

    if (success) {
        m_allResults = results;
        populateResults(results);
        m_statusLabel->setText(message);
        m_statusLabel->setStyleSheet(
            results.isEmpty()
                ? "color:#D97706; font-size:11px;"
                : "color:#059669; font-size:11px; font-weight:600;");
    } else {
        m_statusLabel->setText(message);
        m_statusLabel->setStyleSheet("color:#DC2626; font-size:11px;");
    }

    if (m_worker) {
        m_worker->deleteLater();
        m_worker = nullptr;
    }
}