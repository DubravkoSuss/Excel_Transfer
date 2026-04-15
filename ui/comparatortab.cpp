#include "comparatortab.h"
#include "mainwindow.h"
#include "../core/mappingmodel.h"
#include "../core/mappingcontroller.h"
#include "../features/mappings/mappingrow.h"
#include "../services/excelhandler.h"
#include <QHBoxLayout>
#include <QHeaderView>
#include <QScrollArea>
#include <QFileInfo>
#include <QFile>
#include <QDate>
#include <QMessageBox>
#include <QApplication>
#include <QGridLayout>
#include <cmath>

// ── Static data ──
const QStringList ComparatorTab::s_monthNames = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

const QMap<QString, QString> ComparatorTab::s_monthToNum = {
    {"January","01"},{"February","02"},{"March","03"},{"April","04"},
    {"May","05"},{"June","06"},{"July","07"},{"August","08"},
    {"September","09"},{"October","10"},{"November","11"},{"December","12"}
};

// ── Helpers ──
static QString comboStyle() {
    return "QComboBox { background: white; border: 1px solid #E5E7EB; border-radius: 6px;"
           "  padding: 4px 8px; font-size: 12px; min-width: 90px; }"
           "QComboBox::drop-down { border: none; }"
           "QComboBox:hover { border-color: #3B82F6; }";
}

// ════════════════════════════════════════════════════════════════
// Constructor
// ════════════════════════════════════════════════════════════════
ComparatorTab::ComparatorTab(MainWindow* mainWindow, QWidget* parent)
    : QWidget(parent), m_mainWindow(mainWindow)
{
    setupUI();
}

// ════════════════════════════════════════════════════════════════
// Setup UI
// ════════════════════════════════════════════════════════════════
void ComparatorTab::setupUI()
{
    QHBoxLayout* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setHandleWidth(5);
    m_splitter->setStyleSheet(
        "QSplitter::handle { background: #E5E7EB; }"
        "QSplitter::handle:hover { background: #3B82F6; }"
    );
    rootLayout->addWidget(m_splitter);

    // ════════════════════════════════════════════
    // LEFT SIDEBAR — Mapping Cards
    // ════════════════════════════════════════════
    QFrame* sidebar = new QFrame();
    sidebar->setMinimumWidth(200);
    sidebar->setStyleSheet("QFrame { background: #F3F4F6; }");
    QVBoxLayout* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(10, 12, 10, 12);
    sidebarLayout->setSpacing(6);

    QHBoxLayout* cardsTitleRow = new QHBoxLayout();
    QLabel* cardsTitle = new QLabel("Mapping Cards");
    cardsTitle->setStyleSheet("font-weight: 700; font-size: 14px; color: #1F2937;");
    cardsTitleRow->addWidget(cardsTitle);
    cardsTitleRow->addStretch();

    QPushButton* btnSelectAll = new QPushButton("✔ All");
    btnSelectAll->setFixedHeight(24);
    btnSelectAll->setStyleSheet(
        "QPushButton { background: #3B82F6; color: white; border-radius: 4px;"
        "  padding: 2px 8px; font-size: 11px; font-weight: 600; border: none; }"
        "QPushButton:hover { background: #2563EB; }"
    );
    QPushButton* btnDeselectAll = new QPushButton("✗ None");
    btnDeselectAll->setFixedHeight(24);
    btnDeselectAll->setStyleSheet(
        "QPushButton { background: #6B7280; color: white; border-radius: 4px;"
        "  padding: 2px 8px; font-size: 11px; font-weight: 600; border: none; }"
        "QPushButton:hover { background: #4B5563; }"
    );
    cardsTitleRow->addWidget(btnSelectAll);
    cardsTitleRow->addWidget(btnDeselectAll);
    sidebarLayout->addLayout(cardsTitleRow);

    connect(btnSelectAll, &QPushButton::clicked, this, [this]() {
        if (m_mappingController) m_mappingController->setAllChecked(true);
    });
    connect(btnDeselectAll, &QPushButton::clicked, this, [this]() {
        if (m_mappingController) m_mappingController->setAllChecked(false);
    });

    QScrollArea* cardsScroll = new QScrollArea();
    cardsScroll->setWidgetResizable(true);
    cardsScroll->setStyleSheet("QScrollArea { border: none; background: transparent; }");

    m_cardsContainer = new QWidget();
    m_cardsLayout = new QVBoxLayout(m_cardsContainer);
    m_cardsLayout->setContentsMargins(0, 0, 0, 0);
    m_cardsLayout->setSpacing(4);
    m_cardsLayout->addStretch();

    cardsScroll->setWidget(m_cardsContainer);
    sidebarLayout->addWidget(cardsScroll, 1);

    m_noMappingsLabel = new QLabel("No mappings loaded.\nSelect year/months and click Load.");
    m_noMappingsLabel->setStyleSheet(
        "color: #8B92A7; font-style: italic; font-size: 12px;"
        "padding: 40px 20px; background: rgba(255,255,255,0.5);"
        "border-radius: 12px; border: 2px dashed #D1D5DB;"
    );
    m_noMappingsLabel->setAlignment(Qt::AlignCenter);
    sidebarLayout->addWidget(m_noMappingsLabel);

    m_mappingModel = new MappingModel(this);
    m_mappingController = new MappingController(m_mappingModel, m_cardsContainer,
                                                m_cardsLayout, this);
    connect(m_mappingController, &MappingController::rowCountChanged, this, [this](int count) {
        m_noMappingsLabel->setVisible(count == 0);
    });

    m_splitter->addWidget(sidebar);

    // ════════════════════════════════════════════
    // RIGHT PANEL
    // ════════════════════════════════════════════
    QWidget* rightPanel = new QWidget();
    rightPanel->setMinimumWidth(500);
    m_mainRightLayout = new QVBoxLayout(rightPanel);
    m_mainRightLayout->setContentsMargins(16, 16, 16, 16);
    m_mainRightLayout->setSpacing(12);

    // Title
    QLabel* titleLabel = new QLabel("Comparator");
    titleLabel->setStyleSheet(
        "font-weight: 700; font-size: 18px; color: #1F2937;"
        "letter-spacing: -0.3px; margin-bottom: 4px;"
    );
    m_mainRightLayout->addWidget(titleLabel);

    QLabel* descLabel = new QLabel(
        "Compare working files with _compare files across multiple months. "
        "Select year, check months, adjust precision, then Load & Compare."
    );
    descLabel->setStyleSheet("color: #6B7280; font-size: 13px; margin-bottom: 8px;");
    descLabel->setWordWrap(true);
    m_mainRightLayout->addWidget(descLabel);

    // ── Selection card ──
    QFrame* selCard = new QFrame();
    selCard->setStyleSheet(
        "QFrame {"
        "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "    stop:0 #FAFBFC, stop:1 #F5F7FA);"
        "  border-radius: 12px; padding: 16px;"
        "}"
    );
    QVBoxLayout* cardLayout = new QVBoxLayout(selCard);
    cardLayout->setSpacing(10);

    // Year row
    QHBoxLayout* yearRow = new QHBoxLayout();
    yearRow->setSpacing(12);
    QLabel* yearLabel = new QLabel("Year:");
    yearLabel->setStyleSheet("font-weight: 600; color: #374151;");
    yearRow->addWidget(yearLabel);

    m_yearCombo = new QComboBox();
    m_yearCombo->setStyleSheet(comboStyle());
    for (int y = 2010; y <= 2043; y++)
        m_yearCombo->addItem(QString::number(y));
    m_yearCombo->setCurrentText(QString::number(QDate::currentDate().year()));
    yearRow->addWidget(m_yearCombo);

    // Precision
    QLabel* precLabel = new QLabel("Precision:");
    precLabel->setStyleSheet("font-weight: 600; color: #374151;");
    yearRow->addWidget(precLabel);

    m_precisionCombo = new QComboBox();
    m_precisionCombo->setStyleSheet(comboStyle());
    m_precisionCombo->addItems({"Whole number", "1 decimal", "2 decimals", "3 decimals", "4 decimals", "Exact"});
    m_precisionCombo->setCurrentIndex(2); // default: 2 decimals
    yearRow->addWidget(m_precisionCombo);

    yearRow->addStretch();
    cardLayout->addLayout(yearRow);

    // Month checkboxes — 2 rows of 6
    QLabel* monthsTitle = new QLabel("Months:");
    monthsTitle->setStyleSheet("font-weight: 600; color: #374151;");
    cardLayout->addWidget(monthsTitle);

    QGridLayout* monthGrid = new QGridLayout();
    monthGrid->setSpacing(6);
    for (int i = 0; i < 12; ++i) {
        m_monthChecks[i] = new QCheckBox(s_monthNames[i]);
        m_monthChecks[i]->setStyleSheet(
            "QCheckBox { font-size: 12px; color: #374151; }"
            "QCheckBox::indicator { width: 16px; height: 16px; }"
        );
        monthGrid->addWidget(m_monthChecks[i], i / 6, i % 6);
    }

    // Quick-select buttons
    QHBoxLayout* quickRow = new QHBoxLayout();
    QPushButton* btnAllMonths = new QPushButton("All");
    btnAllMonths->setFixedHeight(22);
    btnAllMonths->setStyleSheet(
        "QPushButton { background: #3B82F6; color: white; border-radius: 4px;"
        "  padding: 1px 10px; font-size: 11px; font-weight: 600; border: none; }"
        "QPushButton:hover { background: #2563EB; }"
    );
    QPushButton* btnNoMonths = new QPushButton("None");
    btnNoMonths->setFixedHeight(22);
    btnNoMonths->setStyleSheet(
        "QPushButton { background: #6B7280; color: white; border-radius: 4px;"
        "  padding: 1px 10px; font-size: 11px; font-weight: 600; border: none; }"
        "QPushButton:hover { background: #4B5563; }"
    );
    quickRow->addWidget(btnAllMonths);
    quickRow->addWidget(btnNoMonths);
    quickRow->addStretch();

    connect(btnAllMonths, &QPushButton::clicked, this, [this]() {
        for (int i = 0; i < 12; ++i) m_monthChecks[i]->setChecked(true);
    });
    connect(btnNoMonths, &QPushButton::clicked, this, [this]() {
        for (int i = 0; i < 12; ++i) m_monthChecks[i]->setChecked(false);
    });

    cardLayout->addLayout(monthGrid);
    cardLayout->addLayout(quickRow);

    // Buttons row: Load + Compare
    QHBoxLayout* btnRow = new QHBoxLayout();
    m_loadBtn = new QPushButton("⚙ Load");
    m_loadBtn->setStyleSheet(
        "QPushButton { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "    stop:0 #8B5CF6, stop:1 #7C3AED); color: white; font-weight: 600;"
        "  padding: 8px 20px; border-radius: 6px; font-size: 13px; border: none; }"
        "QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "    stop:0 #7C3AED, stop:1 #6D28D9); }"
        "QPushButton:pressed { background: #6D28D9; }"
    );
    btnRow->addWidget(m_loadBtn);

    m_compareBtn = new QPushButton("▶ Compare");
    m_compareBtn->setEnabled(false);
    m_compareBtn->setStyleSheet(
        "QPushButton { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "    stop:0 #059669, stop:1 #047857); color: white; font-weight: 600;"
        "  padding: 8px 20px; border-radius: 6px; font-size: 13px; border: none; }"
        "QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "    stop:0 #047857, stop:1 #065F46); }"
        "QPushButton:pressed { background: #064E3B; }"
        "QPushButton:disabled { background: #E5E7EB; color: #9CA3AF; }"
    );
    btnRow->addWidget(m_compareBtn);
    btnRow->addStretch();
    cardLayout->addLayout(btnRow);

    // Status / file info
    m_statusLabel = new QLabel("Select year and months, then click Load.");
    m_statusLabel->setStyleSheet("color: #6B7280; font-style: italic; padding: 4px 0; font-size: 13px;");
    m_statusLabel->setWordWrap(true);
    cardLayout->addWidget(m_statusLabel);

    m_mainRightLayout->addWidget(selCard);

    // ── Progress bar ──
    m_progressBar = new QProgressBar();
    m_progressBar->setMaximumHeight(6);
    m_progressBar->setTextVisible(false);
    m_progressBar->setVisible(false);
    m_mainRightLayout->addWidget(m_progressBar);

    // ── Summary ──
    m_summaryLabel = new QLabel("");
    m_summaryLabel->setStyleSheet(
        "font-weight: 600; font-size: 14px; padding: 8px; border-radius: 6px;"
    );
    m_summaryLabel->setVisible(false);
    m_mainRightLayout->addWidget(m_summaryLabel);

    // ── Filter bar ──
    QFrame* filterFrame = new QFrame();
    filterFrame->setStyleSheet(
        "QFrame { background: #F9FAFB; border: 1px solid #E5E7EB; border-radius: 8px; padding: 8px; }"
    );
    QHBoxLayout* filterLayout = new QHBoxLayout(filterFrame);
    filterLayout->setSpacing(8);
    filterLayout->setContentsMargins(8, 4, 8, 4);

    QLabel* filterTitle = new QLabel("🔍 Filter:");
    filterTitle->setStyleSheet("font-weight: 600; font-size: 12px; color: #374151;");
    filterLayout->addWidget(filterTitle);

    auto addFilterCombo = [&](const QString& label) -> QComboBox* {
        QLabel* lbl = new QLabel(label);
        lbl->setStyleSheet("font-size: 11px; color: #6B7280; font-weight: 500;");
        filterLayout->addWidget(lbl);
        QComboBox* combo = new QComboBox();
        combo->setStyleSheet(comboStyle());
        combo->addItem("All");
        filterLayout->addWidget(combo);
        return combo;
    };

    m_filterMonth   = addFilterCombo("Month:");
    m_filterMapping = addFilterCombo("Mapping:");
    m_filterSheet   = addFilterCombo("Sheet:");
    m_filterColumn  = addFilterCombo("Column:");

    filterLayout->addStretch();
    m_mainRightLayout->addWidget(filterFrame);

    connect(m_filterMonth,   QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ComparatorTab::applyFilters);
    connect(m_filterMapping, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ComparatorTab::applyFilters);
    connect(m_filterSheet,   QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ComparatorTab::applyFilters);
    connect(m_filterColumn,  QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ComparatorTab::applyFilters);

    // ── Results container (table + undock) ──
    m_resultsContainer = new QWidget();
    m_resultsContainerLayout = new QVBoxLayout(m_resultsContainer);
    m_resultsContainerLayout->setContentsMargins(0, 0, 0, 0);
    m_resultsContainerLayout->setSpacing(4);

    // Undock button row
    QHBoxLayout* undockRow = new QHBoxLayout();
    m_undockBtn = new QPushButton("🗗 Undock");
    m_undockBtn->setFixedHeight(26);
    m_undockBtn->setStyleSheet(
        "QPushButton { background: #4B5563; color: white; border-radius: 4px;"
        "  padding: 2px 12px; font-size: 11px; font-weight: 600; border: none; }"
        "QPushButton:hover { background: #374151; }"
    );
    undockRow->addStretch();
    undockRow->addWidget(m_undockBtn);
    m_resultsContainerLayout->addLayout(undockRow);

    // Table
    m_resultsTable = new QTableWidget();
    m_resultsTable->setColumnCount(8);
    m_resultsTable->setHorizontalHeaderLabels(
        {"Month", "Mapping", "Sheet", "Row", "Column", "Working", "Compare", "Difference"});
    m_resultsTable->horizontalHeader()->setStretchLastSection(true);
    m_resultsTable->setAlternatingRowColors(true);
    m_resultsTable->setMinimumHeight(300);
    m_resultsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultsTable->setSortingEnabled(true);
    m_resultsTable->setStyleSheet(
        "QTableWidget { background: white; gridline-color: #E5E7EB;"
        "  border-radius: 8px; font-size: 13px; }"
        "QTableWidget::item { padding: 8px; }"
        "QTableWidget::item:alternate { background: #F9FAFB; }"
        "QHeaderView::section { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "    stop:0 #F9FAFB, stop:1 #F3F4F6); color: #374151; padding: 10px;"
        "  font-weight: 600; border: none; border-bottom: 2px solid #E5E7EB; }"
    );
    m_resultsContainerLayout->addWidget(m_resultsTable, 1);

    m_mainRightLayout->addWidget(m_resultsContainer, 1);

    m_splitter->addWidget(rightPanel);
    m_splitter->setSizes({420, 800});
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);

    // Connections
    connect(m_loadBtn, &QPushButton::clicked, this, &ComparatorTab::onLoad);
    connect(m_compareBtn, &QPushButton::clicked, this, &ComparatorTab::onCompare);
    connect(m_undockBtn, &QPushButton::clicked, this, &ComparatorTab::onUndock);
}

// ════════════════════════════════════════════════════════════════
// Helpers
// ════════════════════════════════════════════════════════════════
QString ComparatorTab::buildComparePath(const QString& workingPath) const
{
    QFileInfo fi(workingPath);
    QString baseName = fi.completeBaseName();
    QString ext = fi.suffix();
    QString dir = fi.absolutePath();
    return QString("%1/%2_compare.%3").arg(dir, baseName, ext);
}

double ComparatorTab::toleranceFromPrecision() const
{
    int idx = m_precisionCombo ? m_precisionCombo->currentIndex() : 2;
    // 0=Whole, 1=1dp, 2=2dp, 3=3dp, 4=4dp, 5=Exact
    switch (idx) {
        case 0: return 0.5;     // whole number — will use rounding
        case 1: return 0.05;
        case 2: return 0.005;
        case 3: return 0.0005;
        case 4: return 0.00005;
        case 5: return 0.0;     // exact
        default: return 0.005;
    }
}

double ComparatorTab::roundToPrecision(double val, int decimals) const
{
    if (decimals < 0) return val; // exact — no rounding
    double factor = std::pow(10.0, decimals);
    return std::round(val * factor) / factor;
}

// ════════════════════════════════════════════════════════════════
// Load — discover files for each checked month
// ════════════════════════════════════════════════════════════════
void ComparatorTab::onLoad()
{
    if (!m_mainWindow) return;

    int year = m_yearCombo->currentText().toInt();

    // Collect checked months
    QStringList checkedMonths;
    for (int i = 0; i < 12; ++i) {
        if (m_monthChecks[i]->isChecked())
            checkedMonths.append(s_monthNames[i]);
    }

    if (checkedMonths.isEmpty()) {
        m_statusLabel->setText("⚠ Please check at least one month.");
        m_statusLabel->setStyleSheet("color: #D97706; font-style: normal; font-size: 13px;");
        m_compareBtn->setEnabled(false);
        return;
    }

    // Clear previous
    m_mappingController->clearAllMappings();
    m_resultsTable->setRowCount(0);
    m_summaryLabel->setVisible(false);
    m_allMismatches.clear();
    m_monthFiles.clear();

    // Populate mapping cards from the first checked month (structure is the same)
    m_mainWindow->populateFillAllMappingCards(m_mappingController, checkedMonths.first(), year);
    m_mappingController->setAllChecked(true);

    // Discover files for each month
    QString basePath = m_mainWindow->destFolder();
    int foundCount = 0;
    int missingCount = 0;
    QStringList statusLines;

    for (const QString& month : checkedMonths) {
        QString mm = s_monthToNum.value(month, "01");
        QString testFolder = QString("%1/%2/%3/test").arg(basePath).arg(year).arg(mm);

        QString workingPath = m_mainWindow->findCostControlFile(testFolder);
        QString comparePath = workingPath.isEmpty() ? QString() : buildComparePath(workingPath);

        bool workingOk = !workingPath.isEmpty() && QFile::exists(workingPath);
        bool compareOk = !comparePath.isEmpty() && QFile::exists(comparePath);

        if (workingOk && compareOk) {
            m_monthFiles[month] = qMakePair(workingPath, comparePath);
            foundCount++;
            statusLines.append(QString("  ✅ %1: OK").arg(month));
        } else {
            missingCount++;
            statusLines.append(QString("  ❌ %1: %2")
                .arg(month, !workingOk ? "working file missing" : "compare file missing"));
        }
    }

    bool canCompare = foundCount > 0 && m_mappingController->mappingCount() > 0;
    m_compareBtn->setEnabled(canCompare);

    QString statusText = QString("%1 of %2 months ready (%3 mapping cards)\n%4")
        .arg(foundCount).arg(checkedMonths.size())
        .arg(m_mappingController->mappingCount())
        .arg(statusLines.join("\n"));

    m_statusLabel->setText(statusText);
    m_statusLabel->setStyleSheet(canCompare
        ? "color: #059669; font-style: normal; font-size: 12px; white-space: pre;"
        : "color: #D97706; font-style: normal; font-size: 12px; white-space: pre;");
}

// ════════════════════════════════════════════════════════════════
// Compare — loop through months
// ════════════════════════════════════════════════════════════════
void ComparatorTab::onCompare()
{
    if (!m_mainWindow || m_monthFiles.isEmpty()) return;

    m_allMismatches.clear();
    m_resultsTable->setRowCount(0);
    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);

    int precIdx = m_precisionCombo ? m_precisionCombo->currentIndex() : 2;
    // decimals: 0=whole, 1,2,3,4, -1=exact
    int decimals = (precIdx == 5) ? -1 : precIdx;

    int totalCells = 0;
    int totalMonths = m_monthFiles.size();
    int processedMonths = 0;

    // Get mapping items once
    QVector<MappingItem> items = m_mappingController->items();
    QVector<int> checkedIndices;
    for (int i = 0; i < items.size(); ++i) {
        MappingRow* row = m_mappingController->rowAt(i);
        if (row && row->isChecked()) checkedIndices.append(i);
    }

    for (auto it = m_monthFiles.constBegin(); it != m_monthFiles.constEnd(); ++it) {
        const QString& month = it.key();
        const QString& workingPath = it.value().first;
        const QString& comparePath = it.value().second;

        m_statusLabel->setText(QString("Comparing %1...").arg(month));
        QApplication::processEvents();

        ExcelHandler handler;
        const QString workingKey = "cmp_w_" + month;
        const QString compareKey = "cmp_c_" + month;

        if (!handler.loadWorkbook(workingPath, workingKey)) {
            qWarning() << "Comparator: failed to load working file for" << month;
            processedMonths++;
            continue;
        }
        if (!handler.loadWorkbook(comparePath, compareKey)) {
            qWarning() << "Comparator: failed to load compare file for" << month;
            handler.unloadWorkbook(workingKey);
            processedMonths++;
            continue;
        }

        for (int idx : checkedIndices) {
            const MappingEntry& entry = items[idx].entry;
            const QString& destSheet = entry.destSheet;
            const QString& destCol = entry.destColumn;
            int destColNum = handler.letterToColumn(destCol);

            // Primary column comparison
            for (auto rit = entry.rowMap.constBegin(); rit != entry.rowMap.constEnd(); ++rit) {
                int destRow = rit.key();

                QVariant wv = handler.getCellValue(workingKey, destSheet, destRow, destColNum);
                QVariant cv = handler.getCellValue(compareKey, destSheet, destRow, destColNum);

                double wVal = wv.canConvert<double>() ? wv.toDouble() : 0.0;
                double cVal = cv.canConvert<double>() ? cv.toDouble() : 0.0;

                // Round to selected precision
                double rw = roundToPrecision(wVal, decimals);
                double rc = roundToPrecision(cVal, decimals);

                totalCells++;

                double diff = rw - rc;
                bool mismatch = (decimals < 0) ? (wVal != cVal) : (std::abs(diff) > 0.0001);

                if (mismatch) {
                    CompareResult cr;
                    cr.month = month;
                    cr.mappingType = entry.sourceFileType;
                    cr.sheetName = destSheet;
                    cr.row = destRow;
                    cr.column = destCol;
                    cr.workingValue = rw;
                    cr.compareValue = rc;
                    cr.difference = diff;
                    m_allMismatches.append(cr);
                }
            }

            // IP-IZ cumulative columns for MZLZ Consolidated
            if (destSheet.contains("MZLZ Consolidated", Qt::CaseInsensitive)) {
                static const QStringList cumColNames = {
                    "IP", "IQ", "IR", "IS", "IT", "IU", "IV", "IW", "IX", "IY", "IZ"
                };
                for (const QString& cumCol : cumColNames) {
                    int cumColNum = handler.letterToColumn(cumCol);
                    if (cumColNum <= 0) continue;

                    for (auto rit = entry.rowMap.constBegin(); rit != entry.rowMap.constEnd(); ++rit) {
                        int destRow = rit.key();

                        QVariant wv = handler.getCellValue(workingKey, destSheet, destRow, cumColNum);
                        QVariant cv = handler.getCellValue(compareKey, destSheet, destRow, cumColNum);

                        double wVal = wv.canConvert<double>() ? wv.toDouble() : 0.0;
                        double cVal = cv.canConvert<double>() ? cv.toDouble() : 0.0;

                        // IP-IZ always rounded to whole numbers (standard rounding: 20.5→21, 20.4→20)
                        double rw = std::round(wVal);
                        double rc = std::round(cVal);

                        totalCells++;

                        double diff = rw - rc;
                        if (std::abs(diff) > 0.0001) {
                            CompareResult cr;
                            cr.month = month;
                            cr.mappingType = entry.sourceFileType + " (cum)";
                            cr.sheetName = destSheet;
                            cr.row = destRow;
                            cr.column = cumCol;
                            cr.workingValue = rw;
                            cr.compareValue = rc;
                            cr.difference = diff;
                            m_allMismatches.append(cr);
                        }
                    }
                }
            }
        }

        handler.unloadWorkbook(workingKey);
        handler.unloadWorkbook(compareKey);

        processedMonths++;
        m_progressBar->setValue(totalMonths > 0 ? (processedMonths * 100 / totalMonths) : 100);
        QApplication::processEvents();
    }

    m_progressBar->setVisible(false);

    // Populate filters & table
    populateFilterCombos();
    populateResultsTable(m_allMismatches);

    // Summary
    m_summaryLabel->setVisible(true);
    if (m_allMismatches.isEmpty()) {
        m_summaryLabel->setText(QString("✅ Perfect match — %1 months, %2 cells compared, 0 mismatches")
            .arg(processedMonths).arg(totalCells));
        m_summaryLabel->setStyleSheet(
            "font-weight: 600; font-size: 14px; padding: 8px;"
            "background: #ECFDF5; color: #059669; border-radius: 6px;"
            "border-left: 4px solid #059669;"
        );
    } else {
        m_summaryLabel->setText(QString("❌ %1 mismatch(es) out of %2 cells (%3 months)")
            .arg(m_allMismatches.size()).arg(totalCells).arg(processedMonths));
        m_summaryLabel->setStyleSheet(
            "font-weight: 600; font-size: 14px; padding: 8px;"
            "background: #FEF2F2; color: #DC2626; border-radius: 6px;"
            "border-left: 4px solid #DC2626;"
        );
    }

    m_statusLabel->setText(QString("Comparison complete. %1 months, %2 cells, %3 mismatches.")
        .arg(processedMonths).arg(totalCells).arg(m_allMismatches.size()));
    m_statusLabel->setStyleSheet("color: #374151; font-style: normal; font-size: 13px;");
}

// ════════════════════════════════════════════════════════════════
// Populate filter combos from results
// ════════════════════════════════════════════════════════════════
void ComparatorTab::populateFilterCombos()
{
    // Collect unique values
    QSet<QString> months, mappings, sheets, columns;
    for (const CompareResult& cr : m_allMismatches) {
        months.insert(cr.month);
        mappings.insert(cr.mappingType);
        sheets.insert(cr.sheetName);
        columns.insert(cr.column);
    }

    auto repopulate = [](QComboBox* combo, const QSet<QString>& values) {
        QString prev = combo->currentText();
        combo->blockSignals(true);
        combo->clear();
        combo->addItem("All");
        QStringList sorted = values.values();
        sorted.sort();
        combo->addItems(sorted);
        int idx = combo->findText(prev);
        combo->setCurrentIndex(idx >= 0 ? idx : 0);
        combo->blockSignals(false);
    };

    repopulate(m_filterMonth, months);
    repopulate(m_filterMapping, mappings);
    repopulate(m_filterSheet, sheets);
    repopulate(m_filterColumn, columns);
}

// ════════════════════════════════════════════════════════════════
// Apply filters — show/hide table rows
// ════════════════════════════════════════════════════════════════
void ComparatorTab::applyFilters()
{
    QString fMonth   = m_filterMonth->currentText();
    QString fMapping = m_filterMapping->currentText();
    QString fSheet   = m_filterSheet->currentText();
    QString fColumn  = m_filterColumn->currentText();

    int visibleCount = 0;
    for (int r = 0; r < m_resultsTable->rowCount(); ++r) {
        bool show = true;
        if (fMonth != "All" && m_resultsTable->item(r, 0)->text() != fMonth) show = false;
        if (fMapping != "All" && m_resultsTable->item(r, 1)->text() != fMapping) show = false;
        if (fSheet != "All" && m_resultsTable->item(r, 2)->text() != fSheet) show = false;
        if (fColumn != "All" && m_resultsTable->item(r, 4)->text() != fColumn) show = false;

        m_resultsTable->setRowHidden(r, !show);
        if (show) visibleCount++;
    }

    // Update summary with filter info
    if (m_allMismatches.size() > 0 && visibleCount < m_allMismatches.size()) {
        m_summaryLabel->setText(QString("❌ Showing %1 of %2 mismatch(es)")
            .arg(visibleCount).arg(m_allMismatches.size()));
    }
}

// ════════════════════════════════════════════════════════════════
// Populate results table
// ════════════════════════════════════════════════════════════════
void ComparatorTab::populateResultsTable(const QVector<CompareResult>& mismatches)
{
    m_resultsTable->setSortingEnabled(false);
    m_resultsTable->setRowCount(0);

    for (const CompareResult& cr : mismatches) {
        int row = m_resultsTable->rowCount();
        m_resultsTable->insertRow(row);

        m_resultsTable->setItem(row, 0, new QTableWidgetItem(cr.month));
        m_resultsTable->setItem(row, 1, new QTableWidgetItem(cr.mappingType));
        m_resultsTable->setItem(row, 2, new QTableWidgetItem(cr.sheetName));

        // Numeric sort for row number
        auto* rowItem = new QTableWidgetItem();
        rowItem->setData(Qt::DisplayRole, cr.row);
        m_resultsTable->setItem(row, 3, rowItem);

        m_resultsTable->setItem(row, 4, new QTableWidgetItem(cr.column));
        m_resultsTable->setItem(row, 5, new QTableWidgetItem(QString::number(cr.workingValue, 'f', 3)));
        m_resultsTable->setItem(row, 6, new QTableWidgetItem(QString::number(cr.compareValue, 'f', 3)));

        auto* diffItem = new QTableWidgetItem(QString::number(cr.difference, 'f', 3));
        diffItem->setForeground(QColor("#DC2626"));
        diffItem->setData(Qt::FontRole, QFont(diffItem->font().family(), -1, QFont::Bold));
        m_resultsTable->setItem(row, 7, diffItem);
    }

    m_resultsTable->setSortingEnabled(true);
}

// ════════════════════════════════════════════════════════════════
// Undock / Dock
// ════════════════════════════════════════════════════════════════
void ComparatorTab::onUndock()
{
    if (m_floatingDialog) return; // already undocked

    // Remove from main layout
    m_mainRightLayout->removeWidget(m_resultsContainer);

    // Create floating dialog
    m_floatingDialog = new QDialog(this, Qt::Window);
    m_floatingDialog->setWindowTitle("Comparator Results (Floating)");
    m_floatingDialog->resize(1000, 600);
    m_floatingDialog->setStyleSheet("QDialog { background: white; }");

    QVBoxLayout* dlgLayout = new QVBoxLayout(m_floatingDialog);
    dlgLayout->setContentsMargins(8, 8, 8, 8);

    // Dock button inside dialog
    QHBoxLayout* dockRow = new QHBoxLayout();
    QPushButton* dockBtn = new QPushButton("📌 Dock back");
    dockBtn->setFixedHeight(28);
    dockBtn->setStyleSheet(
        "QPushButton { background: #3B82F6; color: white; border-radius: 4px;"
        "  padding: 2px 14px; font-size: 12px; font-weight: 600; border: none; }"
        "QPushButton:hover { background: #2563EB; }"
    );
    connect(dockBtn, &QPushButton::clicked, this, &ComparatorTab::onDock);
    dockRow->addStretch();
    dockRow->addWidget(dockBtn);
    dlgLayout->addLayout(dockRow);

    // Reparent container
    m_resultsContainer->setParent(m_floatingDialog);
    dlgLayout->addWidget(m_resultsContainer, 1);
    m_resultsContainer->show();

    m_undockBtn->setVisible(false); // hide the undock button while floating

    connect(m_floatingDialog, &QDialog::finished, this, &ComparatorTab::onDock);
    m_floatingDialog->show();
}

void ComparatorTab::onDock()
{
    if (!m_floatingDialog) return;

    // Reparent back
    m_resultsContainer->setParent(nullptr);
    m_mainRightLayout->addWidget(m_resultsContainer, 1);
    m_resultsContainer->show();

    m_undockBtn->setVisible(true);

    m_floatingDialog->disconnect(this);
    m_floatingDialog->deleteLater();
    m_floatingDialog = nullptr;
}