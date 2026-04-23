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
#include <QRegularExpression>
#include <cmath>

// ── Inline formula evaluator (SUM and arithmetic within same sheet) ──────────
// Returns the computed value and sets *ok=true when the formula was understood.
// On failure (*ok=false) callers should fall back to the cached <v> value.
static double evalFormula(ExcelHandler* handler, const QString& key,
                          const QString& sheet, const QString& formula,
                          bool* ok = nullptr)
{
    if (!handler || formula.isEmpty()) { if (ok) *ok = false; return 0.0; }
    QString f = formula.trimmed();
    if (f.startsWith('=')) f = f.mid(1);
    f.remove('$');
    if (f.startsWith('+')) f = f.mid(1);
    f = f.trimmed();
    if (f.isEmpty() || f.contains('!')) { if (ok) *ok = false; return 0.0; }

    // Reject non-SUM functions
    static const QRegularExpression nonSumFuncRx(
        "(?!SUM\\b)[A-Z_][A-Z0-9_.]*\\s*\\(",
        QRegularExpression::CaseInsensitiveOption);
    if (nonSumFuncRx.match(f).hasMatch()) { if (ok) *ok = false; return 0.0; }

    // Strip trailing /constant
    double divisor = 1.0;
    static const QRegularExpression divRx("/\\s*(-?[0-9.]+)\\s*$");
    auto divMatch = divRx.match(f);
    if (divMatch.hasMatch()) {
        divisor = divMatch.captured(1).toDouble();
        if (divisor == 0) divisor = 1.0;
        f = f.left(divMatch.capturedStart()).trimmed();
    }

    static const QRegularExpression cellRefRx("^([A-Z]+)(\\d+)$", QRegularExpression::CaseInsensitiveOption);
    auto readCell = [&](const QString& ref, bool* cellOk) -> double {
        auto m = cellRefRx.match(ref.trimmed());
        if (!m.hasMatch()) { if (cellOk) *cellOk = false; return 0.0; }
        if (cellOk) *cellOk = true;
        QVariant v = handler->getCellValue(key, sheet, m.captured(2).toInt(),
                                           handler->letterToColumn(m.captured(1)));
        return v.canConvert<double>() ? v.toDouble() : 0.0;
    };

    auto sumRange = [&](const QString& rangeStr, bool* rangeOk) -> double {
        static const QRegularExpression rangeRx("^([A-Z]+)(\\d+):([A-Z]+)(\\d+)$",
                                                QRegularExpression::CaseInsensitiveOption);
        auto m = rangeRx.match(rangeStr.trimmed());
        if (!m.hasMatch()) return readCell(rangeStr, rangeOk);
        if (rangeOk) *rangeOk = true;
        QString c1 = m.captured(1).toUpper(), c2 = m.captured(3).toUpper();
        int r1 = m.captured(2).toInt(), r2 = m.captured(4).toInt();
        double total = 0.0;
        if (c1 == c2) {
            int c = handler->letterToColumn(c1);
            for (int r = qMin(r1,r2); r <= qMax(r1,r2); ++r) {
                QVariant v = handler->getCellValue(key, sheet, r, c);
                if (v.canConvert<double>()) total += v.toDouble();
            }
        } else if (r1 == r2) {
            int col1 = handler->letterToColumn(c1), col2 = handler->letterToColumn(c2);
            for (int c = qMin(col1,col2); c <= qMax(col1,col2); ++c) {
                QVariant v = handler->getCellValue(key, sheet, r1, c);
                if (v.canConvert<double>()) total += v.toDouble();
            }
        }
        return total;
    };

    static const QRegularExpression sumRx("^SUM\\((.+)\\)$", QRegularExpression::CaseInsensitiveOption);
    auto sumMatch = sumRx.match(f);
    double result = 0.0;

    if (sumMatch.hasMatch()) {
        for (const QString& part : sumMatch.captured(1).split(',')) {
            bool partOk = false;
            result += sumRange(part.trimmed(), &partOk);
            if (!partOk) { if (ok) *ok = false; return 0.0; }
        }
    } else {
        // Arithmetic: cell refs / numbers with +/-
        double cur = 0.0; int sign = 1; int i = 0; QString tok;
        while (i <= f.size()) {
            QChar ch = (i < f.size()) ? f[i] : QChar('+');
            if (ch == '+' || ch == '-') {
                tok = tok.trimmed();
                if (!tok.isEmpty()) {
                    bool numOk = false;
                    double num = tok.toDouble(&numOk);
                    if (numOk) { cur += sign * num; }
                    else {
                        bool cellOk = false;
                        double v = readCell(tok, &cellOk);
                        if (!cellOk) { if (ok) *ok = false; return 0.0; }
                        cur += sign * v;
                    }
                }
                sign = (ch == '+') ? 1 : -1;
                tok.clear();
            } else { tok += ch; }
            ++i;
        }
        result = cur;
    }
    if (ok) *ok = true;
    return (divisor != 1.0) ? (result / divisor) : result;
}

// Helper: get the effective numeric value of a cell.
// IMPORTANT: We always read the plain cached <v> value, NOT re-evaluate formulas.
// Re-evaluating formulas causes false mismatches when working and compare files
// have different formula ranges for the same logical cell (e.g. =SUM(G5:CP5) vs
// =SUM(G5:BW5) because compare file was copied from May and formula wasn't updated).
// Our transfer always writes plain values via setCellValue, so <v> is authoritative.
static double effectiveValue(ExcelHandler* handler, const QString& key,
                             const QString& sheet, int row, int col)
{
    QVariant v = handler->getCellValue(key, sheet, row, col);
    return v.canConvert<double>() ? v.toDouble() : 0.0;
}

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
    sidebar->setMaximumWidth(420);
    sidebar->setStyleSheet("QFrame { background: #F3F4F6; border: none; }");
    QVBoxLayout* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(10, 12, 10, 12);
    sidebarLayout->setSpacing(6);

    QHBoxLayout* cardsTitleRow = new QHBoxLayout();
    QLabel* cardsTitle = new QLabel("Mapping Cards");
    cardsTitle->setStyleSheet("font-weight: 700; font-size: 14px; color: #1F2937; background: transparent;");
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

    connect(btnSelectAll,   &QPushButton::clicked, this, [this]() { if (m_mappingController) m_mappingController->setAllChecked(true);  });
    connect(btnDeselectAll, &QPushButton::clicked, this, [this]() { if (m_mappingController) m_mappingController->setAllChecked(false); });

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
    // RIGHT PANEL — scrollable content
    // ════════════════════════════════════════════
    QWidget* rightPanel = new QWidget();
    rightPanel->setMinimumWidth(500);
    rightPanel->setStyleSheet("QWidget { background: white; }");

    QScrollArea* rightScroll = new QScrollArea();
    rightScroll->setWidgetResizable(true);
    rightScroll->setStyleSheet("QScrollArea { border: none; background: white; }");
    rightScroll->setWidget(rightPanel);

    m_mainRightLayout = new QVBoxLayout(rightPanel);
    m_mainRightLayout->setContentsMargins(24, 20, 24, 20);
    m_mainRightLayout->setSpacing(0);

    // ── Header ──
    QLabel* titleLabel = new QLabel("Comparator");
    titleLabel->setStyleSheet(
        "font-weight: 700; font-size: 20px; color: #111827; background: transparent;"
        "letter-spacing: -0.3px;"
    );
    m_mainRightLayout->addWidget(titleLabel);
    m_mainRightLayout->addSpacing(4);

    QLabel* descLabel = new QLabel(
        "Compare working files with _compare files across multiple months. "
        "Select year, check months, adjust precision, then Load & Compare."
    );
    descLabel->setStyleSheet("color: #6B7280; font-size: 13px; background: transparent;");
    descLabel->setWordWrap(true);
    m_mainRightLayout->addWidget(descLabel);
    m_mainRightLayout->addSpacing(16);

    // ── Settings panel ──
    QFrame* settingsPanel = new QFrame();
    settingsPanel->setStyleSheet(
        "QFrame { background: #F9FAFB; border: 1px solid #E5E7EB;"
        " border-radius: 10px; }"
    );
    QVBoxLayout* settingsLayout = new QVBoxLayout(settingsPanel);
    settingsLayout->setContentsMargins(16, 14, 16, 14);
    settingsLayout->setSpacing(12);

    // Row 1: Year + Precision
    QHBoxLayout* row1 = new QHBoxLayout();
    row1->setSpacing(8);

    QLabel* yearLabel = new QLabel("Year:");
    yearLabel->setStyleSheet("font-weight: 600; font-size: 13px; color: #374151; background: transparent; min-width: 32px;");
    row1->addWidget(yearLabel);

    m_yearCombo = new QComboBox();
    m_yearCombo->setFixedWidth(80);
    m_yearCombo->setStyleSheet(comboStyle());
    for (int y = 2010; y <= 2043; y++)
        m_yearCombo->addItem(QString::number(y));
    m_yearCombo->setCurrentText(QString::number(QDate::currentDate().year()));
    row1->addWidget(m_yearCombo);

    row1->addSpacing(16);

    QLabel* precLabel = new QLabel("Precision:");
    precLabel->setStyleSheet("font-weight: 600; font-size: 13px; color: #374151; background: transparent;");
    row1->addWidget(precLabel);

    m_precisionCombo = new QComboBox();
    m_precisionCombo->setFixedWidth(120);
    m_precisionCombo->setStyleSheet(comboStyle());
    m_precisionCombo->addItems({"Whole number", "1 decimal", "2 decimals", "3 decimals", "4 decimals", "Exact"});
    m_precisionCombo->setCurrentIndex(2);
    row1->addWidget(m_precisionCombo);

    row1->addStretch();
    settingsLayout->addLayout(row1);

    // Divider
    QFrame* div1 = new QFrame();
    div1->setFrameShape(QFrame::HLine);
    div1->setStyleSheet("background: #E5E7EB; border: none; max-height: 1px;");
    settingsLayout->addWidget(div1);

    // Row 2: Month label + All/None
    QHBoxLayout* monthTitleRow = new QHBoxLayout();
    QLabel* monthsTitle = new QLabel("Months:");
    monthsTitle->setStyleSheet("font-weight: 600; font-size: 13px; color: #374151; background: transparent;");
    monthTitleRow->addWidget(monthsTitle);
    monthTitleRow->addStretch();

    QPushButton* btnAllMonths = new QPushButton("All");
    btnAllMonths->setFixedSize(48, 24);
    btnAllMonths->setStyleSheet(
        "QPushButton { background: #3B82F6; color: white; border-radius: 4px;"
        "  font-size: 11px; font-weight: 600; border: none; }"
        "QPushButton:hover { background: #2563EB; }"
    );
    QPushButton* btnNoMonths = new QPushButton("None");
    btnNoMonths->setFixedSize(48, 24);
    btnNoMonths->setStyleSheet(
        "QPushButton { background: #6B7280; color: white; border-radius: 4px;"
        "  font-size: 11px; font-weight: 600; border: none; }"
        "QPushButton:hover { background: #4B5563; }"
    );
    monthTitleRow->addWidget(btnAllMonths);
    monthTitleRow->addWidget(btnNoMonths);
    settingsLayout->addLayout(monthTitleRow);

    connect(btnAllMonths, &QPushButton::clicked, this, [this]() { for (int i=0;i<12;++i) m_monthChecks[i]->setChecked(true);  });
    connect(btnNoMonths,  &QPushButton::clicked, this, [this]() { for (int i=0;i<12;++i) m_monthChecks[i]->setChecked(false); });

    // Month checkboxes — 2 rows × 6
    QGridLayout* monthGrid = new QGridLayout();
    monthGrid->setHorizontalSpacing(12);
    monthGrid->setVerticalSpacing(8);
    for (int i = 0; i < 12; ++i) {
        m_monthChecks[i] = new QCheckBox(s_monthNames[i]);
        m_monthChecks[i]->setStyleSheet(
            "QCheckBox { font-size: 13px; color: #374151; background: transparent; spacing: 6px; }"
            "QCheckBox::indicator { width: 15px; height: 15px; border-radius: 3px;"
            "  border: 1px solid #D1D5DB; background: white; }"
            "QCheckBox::indicator:checked { background: #3B82F6; border-color: #3B82F6;"
            "  image: url(none); }"
        );
        monthGrid->addWidget(m_monthChecks[i], i / 6, i % 6);
    }
    settingsLayout->addLayout(monthGrid);

    // Divider
    QFrame* div2 = new QFrame();
    div2->setFrameShape(QFrame::HLine);
    div2->setStyleSheet("background: #E5E7EB; border: none; max-height: 1px;");
    settingsLayout->addWidget(div2);

    // Row 3: Action buttons
    QHBoxLayout* btnRow = new QHBoxLayout();
    btnRow->setSpacing(10);

    m_loadBtn = new QPushButton("⚙  Load");
    m_loadBtn->setFixedHeight(36);
    m_loadBtn->setMinimumWidth(110);
    m_loadBtn->setCursor(Qt::PointingHandCursor);
    m_loadBtn->setStyleSheet(
        "QPushButton { background: #7C3AED; color: white; font-weight: 600;"
        "  padding: 0 20px; border-radius: 6px; font-size: 13px; border: none; }"
        "QPushButton:hover { background: #6D28D9; }"
        "QPushButton:pressed { background: #5B21B6; }"
    );
    btnRow->addWidget(m_loadBtn);

    m_compareBtn = new QPushButton("▶  Compare");
    m_compareBtn->setEnabled(false);
    m_compareBtn->setFixedHeight(36);
    m_compareBtn->setMinimumWidth(120);
    m_compareBtn->setCursor(Qt::PointingHandCursor);
    m_compareBtn->setStyleSheet(
        "QPushButton { background: #059669; color: white; font-weight: 600;"
        "  padding: 0 20px; border-radius: 6px; font-size: 13px; border: none; }"
        "QPushButton:hover { background: #047857; }"
        "QPushButton:pressed { background: #065F46; }"
        "QPushButton:disabled { background: #E5E7EB; color: #9CA3AF; }"
    );
    btnRow->addWidget(m_compareBtn);
    btnRow->addStretch();
    settingsLayout->addLayout(btnRow);

    // Status label
    m_statusLabel = new QLabel("Select year and months, then click Load.");
    m_statusLabel->setStyleSheet(
        "color: #6B7280; font-style: italic; font-size: 12px; background: transparent;"
    );
    m_statusLabel->setWordWrap(true);
    settingsLayout->addWidget(m_statusLabel);

    m_mainRightLayout->addWidget(settingsPanel);
    m_mainRightLayout->addSpacing(10);

    // ── Progress bar ──
    m_progressBar = new QProgressBar();
    m_progressBar->setMaximumHeight(5);
    m_progressBar->setTextVisible(false);
    m_progressBar->setVisible(false);
    m_progressBar->setStyleSheet(
        "QProgressBar { background: #E5E7EB; border-radius: 2px; border: none; }"
        "QProgressBar::chunk { background: #3B82F6; border-radius: 2px; }"
    );
    m_mainRightLayout->addWidget(m_progressBar);
    m_mainRightLayout->addSpacing(4);

    // ── Summary label ──
    m_summaryLabel = new QLabel("");
    m_summaryLabel->setStyleSheet("font-weight: 600; font-size: 14px; padding: 10px 14px; border-radius: 8px;");
    m_summaryLabel->setVisible(false);
    m_summaryLabel->setWordWrap(true);
    m_mainRightLayout->addWidget(m_summaryLabel);
    m_mainRightLayout->addSpacing(8);

    // ── Filter bar ──
    QFrame* filterFrame = new QFrame();
    filterFrame->setStyleSheet(
        "QFrame { background: #F9FAFB; border: 1px solid #E5E7EB; border-radius: 8px; }"
    );
    QHBoxLayout* filterLayout = new QHBoxLayout(filterFrame);
    filterLayout->setSpacing(8);
    filterLayout->setContentsMargins(12, 6, 12, 6);

    QLabel* filterTitle = new QLabel("🔍 Filter:");
    filterTitle->setStyleSheet("font-weight: 600; font-size: 12px; color: #374151; background: transparent;");
    filterLayout->addWidget(filterTitle);

    auto addFilterCombo = [&](const QString& label) -> QComboBox* {
        QLabel* lbl = new QLabel(label);
        lbl->setStyleSheet("font-size: 11px; color: #6B7280; font-weight: 500; background: transparent;");
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
    m_mainRightLayout->addSpacing(6);

    connect(m_filterMonth,   QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ComparatorTab::applyFilters);
    connect(m_filterMapping, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ComparatorTab::applyFilters);
    connect(m_filterSheet,   QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ComparatorTab::applyFilters);
    connect(m_filterColumn,  QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ComparatorTab::applyFilters);

    // ── Results container (table + undock) ──
    m_resultsContainer = new QWidget();
    m_resultsContainer->setStyleSheet("QWidget { background: transparent; }");
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
    m_resultsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_resultsTable->setAlternatingRowColors(true);
    m_resultsTable->setMinimumHeight(280);
    m_resultsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultsTable->setSortingEnabled(true);
    m_resultsTable->setShowGrid(false);
    m_resultsTable->verticalHeader()->setVisible(false);
    m_resultsTable->setStyleSheet(
        "QTableWidget { background: white; gridline-color: #F3F4F6;"
        "  border: 1px solid #E5E7EB; border-radius: 8px; font-size: 13px; }"
        "QTableWidget::item { padding: 7px 10px; color: #1F2937; border-bottom: 1px solid #F3F4F6; }"
        "QTableWidget::item:selected { background: #EFF6FF; color: #1D4ED8; }"
        "QTableWidget::item:alternate { background: #F9FAFB; }"
        "QHeaderView::section { background: #F3F4F6; color: #374151; padding: 8px 10px;"
        "  font-weight: 600; font-size: 12px; border: none;"
        "  border-bottom: 2px solid #E5E7EB; border-right: 1px solid #E5E7EB; }"
        "QHeaderView::section:last { border-right: none; }"
    );
    // Set reasonable default column widths
    m_resultsTable->setColumnWidth(0, 90);   // Month
    m_resultsTable->setColumnWidth(1, 120);  // Mapping
    m_resultsTable->setColumnWidth(2, 160);  // Sheet
    m_resultsTable->setColumnWidth(3, 50);   // Row
    m_resultsTable->setColumnWidth(4, 60);   // Column
    m_resultsTable->setColumnWidth(5, 100);  // Working
    m_resultsTable->setColumnWidth(6, 100);  // Compare

    m_resultsContainerLayout->addWidget(m_resultsTable, 1);
    m_mainRightLayout->addWidget(m_resultsContainer, 1);

    m_splitter->addWidget(rightScroll);
    m_splitter->setSizes({380, 900});
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);

    // Connections
    connect(m_loadBtn,    &QPushButton::clicked, this, &ComparatorTab::onLoad);
    connect(m_compareBtn, &QPushButton::clicked, this, &ComparatorTab::onCompare);
    connect(m_undockBtn,  &QPushButton::clicked, this, &ComparatorTab::onUndock);
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

        QSet<QString> reported; // dedup set: "sheet!row!col"
        auto checkCell = [&](const QString& sheet, int row, int colNum, const QString& colLetter, const QString& typeTag) {
            QString dedupKey = QString("%1!%2!%3").arg(sheet).arg(row).arg(colLetter);
            if (reported.contains(dedupKey)) return;
            reported.insert(dedupKey);

            // Use effectiveValue: evaluates SUM/arithmetic formulas inline so that
            // stale cached <v> values in either file don't cause false mismatches.
            double wVal = effectiveValue(&handler, workingKey, sheet, row, colNum);
            double cVal = effectiveValue(&handler, compareKey, sheet, row, colNum);

            double rw = roundToPrecision(wVal, decimals);
            double rc = roundToPrecision(cVal, decimals);

            totalCells++;

            double diff = rw - rc;
            bool mismatch = (decimals < 0) ? (wVal != cVal) : (std::abs(diff) > 0.0001);

            if (mismatch) {
                CompareResult cr;
                cr.month = month;
                cr.mappingType = typeTag;
                cr.sheetName = sheet;
                cr.row = row;
                cr.column = colLetter;
                cr.workingValue = rw;
                cr.compareValue = rc;
                cr.difference = diff;
                m_allMismatches.append(cr);
            }
        };

        // Primary mappings loop
        for (int idx : checkedIndices) {
            const MappingEntry& entry = items[idx].entry;
            const QString& destSheet = entry.destSheet;
            const QString& destCol = entry.destColumn;
            int destColNum = handler.letterToColumn(destCol);

            for (auto rit = entry.rowMap.constBegin(); rit != entry.rowMap.constEnd(); ++rit) {
                checkCell(destSheet, rit.key(), destColNum, destCol, entry.sourceFileType);
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