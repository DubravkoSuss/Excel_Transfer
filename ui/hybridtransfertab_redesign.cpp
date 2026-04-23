#include "hybridtransfertab.h"
#include "mainwindow.h"
#include "../features/mappings/mappingrow.h"
#include <QFrame>
#include <QLabel>
#include <QHeaderView>
#include <QMessageBox>
#include <QDate>
#include <QTimer>
#include <QSplitter>
#include <QScrollArea>

// ═══════════════════════════════════════════════════════════════════════════
// Stylesheet helpers (matching Excel Search tab design)
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

static QString s_comboStyle() {
    return "QComboBox { background:white; border:1px solid #D1D5DB; border-radius:6px;"
           "  padding:5px 9px; font-size:12px; color:#111827; }"
           "QComboBox::drop-down { border:none; width:20px; }"
           "QComboBox:hover { border-color:#3B82F6; }";
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

HybridTransferTab::HybridTransferTab(MainWindow* mainWindow, QWidget* parent)
    : QWidget(parent), m_mainWindow(mainWindow)
{
    // Create mapping model and controller for this tab
    m_mappingModel = new MappingModel(this);
    m_mappingController = nullptr;  // Will be initialized in setupMappingsSidebar
    
    setupUI();
}

void HybridTransferTab::setupUI()
{
    QVBoxLayout* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // ── Title bar ─────────────────────────────────────────────────────────
    QFrame* titleBar = new QFrame(this);
    titleBar->setStyleSheet(
        "QFrame { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "  stop:0 #7C3AED, stop:1 #6D28D9); border-radius:0; }");
    titleBar->setFixedHeight(60);
    QHBoxLayout* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(20, 0, 20, 0);

    QLabel* icon = new QLabel("⚡", titleBar);
    icon->setStyleSheet("font-size:22px;");
    QLabel* title = new QLabel("Hybrid Transfer", titleBar);
    title->setStyleSheet("font-weight:700; font-size:16px; color:white; letter-spacing:-0.3px;");
    QLabel* desc = new QLabel(
        "Assign months to Execute All or Execute RT, then run both modes in sequence.", titleBar);
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

    // ── Main splitter: left config | right assigned periods ───────────────
    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(5);
    splitter->setStyleSheet(
        "QSplitter::handle { background:#E5E7EB; }"
        "QSplitter::handle:hover { background:#3B82F6; }");
    rootLayout->addWidget(splitter, 1);

    // ═══════════════════════════════════════════
    // LEFT PANEL — Configuration
    // ═══════════════════════════════════════════
    QWidget* leftPanel = new QWidget();
    leftPanel->setMinimumWidth(340);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(16, 16, 10, 16);
    leftLayout->setSpacing(12);

    // ── Card: Period Selection ────────────────────────────────────────────
    QFrame* periodCard = new QFrame();
    periodCard->setStyleSheet(s_cardStyle());
    QVBoxLayout* periodCardLayout = new QVBoxLayout(periodCard);
    periodCardLayout->setContentsMargins(14, 12, 14, 12);
    periodCardLayout->setSpacing(8);

    QLabel* periodTitleLbl = new QLabel("📅  Period Selection", periodCard);
    periodTitleLbl->setStyleSheet("font-weight:700; font-size:13px; color:#1F2937;");
    periodCardLayout->addWidget(periodTitleLbl);

    // Year + Transfer Type row
    QHBoxLayout* selRow = new QHBoxLayout();
    QLabel* yearLabel = new QLabel("Year:", periodCard);
    yearLabel->setStyleSheet(s_labelStyle());
    m_yearCombo = new QComboBox(periodCard);
    m_yearCombo->setStyleSheet(s_comboStyle());
    for (int y = 2010; y <= 2043; y++)
        m_yearCombo->addItem(QString::number(y));
    m_yearCombo->setCurrentText(QString::number(QDate::currentDate().year()));

    QLabel* typeLabel = new QLabel("Type:", periodCard);
    typeLabel->setStyleSheet(s_labelStyle());
    m_transferTypeCombo = new QComboBox(periodCard);
    m_transferTypeCombo->addItems({"Execute All", "Execute RT"});
    m_transferTypeCombo->setStyleSheet(s_comboStyle());

    selRow->addWidget(yearLabel);
    selRow->addWidget(m_yearCombo);
    selRow->addSpacing(10);
    selRow->addWidget(typeLabel);
    selRow->addWidget(m_transferTypeCombo);
    selRow->addStretch();
    periodCardLayout->addLayout(selRow);

    // Month checkboxes label
    QLabel* monthLabel = new QLabel("Select Months:", periodCard);
    monthLabel->setStyleSheet(s_labelStyle());
    periodCardLayout->addWidget(monthLabel);

    // Month checkboxes grid
    QGridLayout* monthGrid = new QGridLayout();
    monthGrid->setSpacing(8);
    
    QStringList months = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    
    m_monthCheckboxes.clear();
    for (int i = 0; i < 12; i++) {
        QCheckBox* cb = new QCheckBox(months[i], periodCard);
        cb->setStyleSheet(
            "QCheckBox { font-size:11px; color:#111827; padding:4px; font-weight:600; }"
            "QCheckBox::indicator { width:16px; height:16px; border-radius:3px; border:2px solid #9CA3AF; }"
            "QCheckBox::indicator:checked { background:#3B82F6; border-color:#3B82F6; }"
            "QCheckBox::indicator:hover { border-color:#3B82F6; background:#EFF6FF; }"
        );
        monthGrid->addWidget(cb, i / 6, i % 6);
        m_monthCheckboxes.append(cb);
    }
    periodCardLayout->addLayout(monthGrid);

    // Quick select buttons
    QHBoxLayout* quickRow = new QHBoxLayout();
    QLabel* quickLbl = new QLabel("Quick:", periodCard);
    quickLbl->setStyleSheet("font-size:11px; color:#6B7280;");
    quickRow->addWidget(quickLbl);

    QString quickBtnStyle = 
        "QPushButton { background:white; color:#374151; padding:3px 10px;"
        "  border:1px solid #D1D5DB; border-radius:4px; font-size:10px; font-weight:600; }"
        "QPushButton:hover { background:#F3F4F6; border-color:#3B82F6; color:#3B82F6; }"
        "QPushButton:pressed { background:#E5E7EB; }";

    QPushButton* btnQ1 = new QPushButton("Q1", periodCard);
    QPushButton* btnQ2 = new QPushButton("Q2", periodCard);
    QPushButton* btnQ3 = new QPushButton("Q3", periodCard);
    QPushButton* btnQ4 = new QPushButton("Q4", periodCard);
    QPushButton* btnAll = new QPushButton("All", periodCard);
    QPushButton* btnNone = new QPushButton("None", periodCard);
    
    btnQ1->setStyleSheet(quickBtnStyle);
    btnQ2->setStyleSheet(quickBtnStyle);
    btnQ3->setStyleSheet(quickBtnStyle);
    btnQ4->setStyleSheet(quickBtnStyle);
    btnAll->setStyleSheet(quickBtnStyle);
    btnNone->setStyleSheet(quickBtnStyle);

    connect(btnQ1, &QPushButton::clicked, this, [this]() { selectQuarter(1); });
    connect(btnQ2, &QPushButton::clicked, this, [this]() { selectQuarter(2); });
    connect(btnQ3, &QPushButton::clicked, this, [this]() { selectQuarter(3); });
    connect(btnQ4, &QPushButton::clicked, this, [this]() { selectQuarter(4); });
    connect(btnAll, &QPushButton::clicked, this, [this]() { selectAllMonths(); });
    connect(btnNone, &QPushButton::clicked, this, [this]() { clearAllMonths(); });

    quickRow->addWidget(btnQ1);
    quickRow->addWidget(btnQ2);
    quickRow->addWidget(btnQ3);
    quickRow->addWidget(btnQ4);
    quickRow->addWidget(btnAll);
    quickRow->addWidget(btnNone);
    quickRow->addStretch();
    periodCardLayout->addLayout(quickRow);

    // Add button
    m_addBtn = new QPushButton("+ Add Selected Months", periodCard);
    m_addBtn->setStyleSheet(s_btnPrimary());
    periodCardLayout->addWidget(m_addBtn);

    leftLayout->addWidget(periodCard);

    // ── Card: Execution Options ───────────────────────────────────────────
    QFrame* execCard = new QFrame();
    execCard->setStyleSheet(s_cardStyle());
    QVBoxLayout* execCardLayout = new QVBoxLayout(execCard);
    execCardLayout->setContentsMargins(14, 12, 14, 12);
    execCardLayout->setSpacing(8);

    QLabel* execTitleLbl = new QLabel("⚙️  Execution Options", execCard);
    execTitleLbl->setStyleSheet("font-weight:700; font-size:13px; color:#1F2937;");
    execCardLayout->addWidget(execTitleLbl);

    QLabel* orderLbl = new QLabel("Execution Order:", execCard);
    orderLbl->setStyleSheet(s_labelStyle());
    execCardLayout->addWidget(orderLbl);

    m_executeAllFirstRadio = new QRadioButton("Execute All first, then RT", execCard);
    m_executeAllFirstRadio->setChecked(true);
    m_executeAllFirstRadio->setStyleSheet("font-size:11px; color:#374151; font-weight:500;");
    execCardLayout->addWidget(m_executeAllFirstRadio);

    m_executeRTFirstRadio = new QRadioButton("Execute RT first, then Execute All", execCard);
    m_executeRTFirstRadio->setStyleSheet("font-size:11px; color:#374151; font-weight:500;");
    execCardLayout->addWidget(m_executeRTFirstRadio);

    leftLayout->addWidget(execCard);
    leftLayout->addStretch();

    // ── Execute bar ───────────────────────────────────────────────────────
    QFrame* execFrame = new QFrame();
    execFrame->setStyleSheet("QFrame { background:transparent; border:none; }");
    QHBoxLayout* execLayout = new QHBoxLayout(execFrame);
    execLayout->setContentsMargins(0, 0, 0, 0);
    execLayout->setSpacing(10);

    m_executeBtn = new QPushButton("▶ Execute Hybrid Transfer", execFrame);
    m_executeBtn->setStyleSheet(s_btnSuccess());
    m_executeBtn->setMinimumHeight(42);
    m_executeBtn->setEnabled(false);

    QPushButton* resetBtn = new QPushButton("↺ Reset All", execFrame);
    resetBtn->setStyleSheet(s_btnDanger());
    connect(resetBtn, &QPushButton::clicked, this, &HybridTransferTab::onResetAll);

    execLayout->addWidget(m_executeBtn);
    execLayout->addWidget(resetBtn);
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
        "  stop:0 #7C3AED, stop:1 #3B82F6); border-radius:5px; }");
    leftLayout->addWidget(m_progressBar);

    m_phaseStatusLabel = new QLabel("", leftPanel);
    m_phaseStatusLabel->setStyleSheet("color:#6B7280; font-size:11px; font-style:italic;");
    m_phaseStatusLabel->setWordWrap(true);
    m_phaseStatusLabel->setVisible(false);
    leftLayout->addWidget(m_phaseStatusLabel);

    m_summaryLabel = new QLabel("No periods assigned yet.", leftPanel);
    m_summaryLabel->setStyleSheet("color:#6B7280; font-size:11px; font-style:italic;");
    m_summaryLabel->setWordWrap(true);
    leftLayout->addWidget(m_summaryLabel);

    splitter->addWidget(leftPanel);

    // ═══════════════════════════════════════════
    // RIGHT PANEL — Assigned Periods + Mappings
    // ═══════════════════════════════════════════
    QWidget* rightPanel = new QWidget();
    rightPanel->setMinimumWidth(400);
    QVBoxLayout* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(10, 16, 16, 16);
    rightLayout->setSpacing(10);

    // Assigned Periods header
    QHBoxLayout* periodsHeaderRow = new QHBoxLayout();
    QLabel* periodsTitleLbl = new QLabel("📋  Assigned Periods", rightPanel);
    periodsTitleLbl->setStyleSheet("font-weight:700; font-size:13px; color:#1F2937;");
    periodsHeaderRow->addWidget(periodsTitleLbl);
    periodsHeaderRow->addStretch();

    // Move buttons
    m_moveUpBtn = new QPushButton("▲ Up", rightPanel);
    m_moveDownBtn = new QPushButton("▼ Down", rightPanel);
    m_moveUpBtn->setStyleSheet(s_btnNeutral());
    m_moveDownBtn->setStyleSheet(s_btnNeutral());
    m_moveUpBtn->setEnabled(false);
    m_moveDownBtn->setEnabled(false);
    periodsHeaderRow->addWidget(m_moveUpBtn);
    periodsHeaderRow->addWidget(m_moveDownBtn);

    m_removeBtn = new QPushButton("✗ Remove", rightPanel);
    m_removeBtn->setStyleSheet(s_btnNeutral());
    m_removeBtn->setEnabled(false);
    periodsHeaderRow->addWidget(m_removeBtn);

    m_clearBtn = new QPushButton("✗ Clear All", rightPanel);
    m_clearBtn->setStyleSheet(s_btnNeutral());
    periodsHeaderRow->addWidget(m_clearBtn);

    rightLayout->addLayout(periodsHeaderRow);

    // Assigned periods table
    m_table = new QTableWidget(0, 4, rightPanel);
    m_table->setHorizontalHeaderLabels({"Year", "Month", "Transfer Type", "Status"});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setStyleSheet(s_tableStyle());
    m_table->setMinimumHeight(200);
    rightLayout->addWidget(m_table);

    // Mappings sidebar (collapsible)
    setupMappingsSidebar();
    rightLayout->addWidget(m_mappingsSidebar);

    splitter->addWidget(rightPanel);
    splitter->setSizes({390, 700});
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    // ── Wire signals ──────────────────────────────────────────────────────
    connect(m_addBtn, &QPushButton::clicked, this, &HybridTransferTab::onAddPeriod);
    connect(m_removeBtn, &QPushButton::clicked, this, &HybridTransferTab::onRemovePeriod);
    connect(m_clearBtn, &QPushButton::clicked, this, &HybridTransferTab::onClearAll);
    connect(m_executeBtn, &QPushButton::clicked, this, &HybridTransferTab::onExecute);
    connect(m_moveUpBtn, &QPushButton::clicked, this, &HybridTransferTab::onMoveUp);
    connect(m_moveDownBtn, &QPushButton::clicked, this, &HybridTransferTab::onMoveDown);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
        m_removeBtn->setEnabled(!m_table->selectedItems().isEmpty());
        updateMoveButtons();
    });
}

void HybridTransferTab::setupMappingsSidebar()
{
    // Create collapsible mappings section
    m_mappingsSidebar = new QFrame();
    m_mappingsSidebar->setStyleSheet(s_cardStyle());
    m_mappingsSidebar->setMaximumHeight(300);
    
    QVBoxLayout* sidebarLayout = new QVBoxLayout(m_mappingsSidebar);
    sidebarLayout->setContentsMargins(14, 12, 14, 12);
    sidebarLayout->setSpacing(8);
    
    // Header with title and Select All/Deselect All buttons
    QHBoxLayout* sidebarHeaderLayout = new QHBoxLayout();
    QLabel* title = new QLabel("🗂️  Row Mappings");
    title->setStyleSheet("font-weight:700; font-size:13px; color:#1F2937;");
    sidebarHeaderLayout->addWidget(title);
    sidebarHeaderLayout->addStretch();

    QPushButton* selectAllCardsBtn = new QPushButton("Select All");
    selectAllCardsBtn->setStyleSheet(s_btnNeutral());
    connect(selectAllCardsBtn, &QPushButton::clicked, this, [this]() {
        if (m_mappingController) m_mappingController->setAllChecked(true);
    });
    sidebarHeaderLayout->addWidget(selectAllCardsBtn);

    QPushButton* deselectAllCardsBtn = new QPushButton("Deselect All");
    deselectAllCardsBtn->setStyleSheet(s_btnNeutral());
    connect(deselectAllCardsBtn, &QPushButton::clicked, this, [this]() {
        if (m_mappingController) m_mappingController->setAllChecked(false);
    });
    sidebarHeaderLayout->addWidget(deselectAllCardsBtn);

    sidebarLayout->addLayout(sidebarHeaderLayout);

    // Mappings scroll area
    m_mappingsContainer = new QWidget();
    m_mappingsContainer->setStyleSheet("background:transparent;");
    m_mappingsLayout = new QVBoxLayout(m_mappingsContainer);
    m_mappingsLayout->setSpacing(6);
    m_mappingsLayout->setContentsMargins(0, 0, 0, 0);

    QScrollArea* scroll = new QScrollArea();
    scroll->setWidget(m_mappingsContainer);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(
        "QScrollArea { background:transparent; border:none; }"
        "QScrollBar:vertical { background:transparent; width:8px; border-radius:4px; }"
        "QScrollBar::handle:vertical { background:rgba(0,0,0,0.15); border-radius:4px; min-height:30px; }"
        "QScrollBar::handle:vertical:hover { background:rgba(0,0,0,0.25); }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0px; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }"
    );
    scroll->setMaximumHeight(200);
    sidebarLayout->addWidget(scroll, 1);

    // No mappings label
    m_noMappingsLabel = new QLabel("No mappings loaded.\nAdd periods to load mappings.");
    m_noMappingsLabel->setStyleSheet(
        "color:#8B92A7; font-style:italic; font-size:11px;"
        "padding:20px; background:rgba(255,255,255,0.5);"
        "border-radius:8px; border:2px dashed #D1D5DB;"
    );
    m_noMappingsLabel->setAlignment(Qt::AlignCenter);
    sidebarLayout->addWidget(m_noMappingsLabel);
    
    // Initialize mapping controller
    m_mappingModel = new MappingModel(this);
    m_mappingController = new MappingController(
        m_mappingModel,
        m_mappingsContainer,
        m_mappingsLayout,
        this
    );
    
    // Connect to update no mappings label visibility
    connect(m_mappingController, &MappingController::rowCountChanged, this, [this](int count) {
        m_noMappingsLabel->setVisible(count == 0);
    });
}

// Rest of the implementation remains the same...
// (onAddPeriod, selectQuarter, selectAllMonths, clearAllMonths, onRemovePeriod, 
//  onClearAll, onResetAll, onExecute, populateTable, updateSummary, updateExecuteButton,
//  onPhaseStarted, onPhaseFinished, onProgressUpdate, onAllFinished, rtBlockFor,
//  updateMoveButtons, onMoveUp, onMoveDown)
