#include "hybridtransfertab.h"
#include "mainwindow.h"
#include "../features/mappings/mappingrow.h"
#include "../features/transfer/mappingupdater.h"
#include "../services/excelhandler.h"
#include <QFrame>
#include <QLabel>
#include <QHeaderView>
#include <QMessageBox>
#include <QDate>
#include <QTimer>
#include <QSplitter>
#include <QScrollArea>
#include <QFileDialog>
#include <QCoreApplication>
#include <QDialog>
#include <QLineEdit>
#include <QTextEdit>
#include <QGridLayout>

// ── Style helpers (same language as ExcelSearch / Comparator) ──────────────
static QString h_btnPrimary() {
    return "QPushButton {"
           "  background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #3B82F6,stop:1 #2563EB);"
           "  color:white; font-weight:600; font-size:12px;"
           "  padding:7px 18px; border-radius:6px; border:none; }"
           "QPushButton:hover { background:#2563EB; }"
           "QPushButton:pressed { background:#1E40AF; }"
           "QPushButton:disabled { background:#E5E7EB; color:#9CA3AF; }";
}
static QString h_btnSuccess() {
    return "QPushButton {"
           "  background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #7C3AED,stop:1 #6D28D9);"
           "  color:white; font-weight:700; font-size:13px;"
           "  padding:9px 24px; border-radius:7px; border:none; }"
           "QPushButton:hover { background:#6D28D9; }"
           "QPushButton:pressed { background:#5B21B6; }"
           "QPushButton:disabled { background:#E5E7EB; color:#9CA3AF; }";
}
static QString h_btnDanger() {
    return "QPushButton {"
           "  background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #EF4444,stop:1 #DC2626);"
           "  color:white; font-weight:600; font-size:12px;"
           "  padding:7px 18px; border-radius:6px; border:none; }"
           "QPushButton:hover { background:#DC2626; }"
           "QPushButton:pressed { background:#991B1B; }";
}
static QString h_btnNeutral() {
    return "QPushButton {"
           "  background:white; border:1px solid #D1D5DB; color:#374151;"
           "  font-size:12px; padding:5px 12px; border-radius:5px; font-weight:500; }"
           "QPushButton:hover { background:#F3F4F6; border-color:#3B82F6; }"
           "QPushButton:disabled { background:#F9FAFB; color:#9CA3AF; }";
}
static QString h_card() {
    return "QFrame { background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #FAFBFC,stop:1 #F5F7FA);"
           "  border-radius:10px; border:1px solid #E5E7EB; }";
}
static QString h_lbl() { return "font-weight:600; color:#374151; font-size:12px;"; }
static QString h_comboStyle() {
    return "QComboBox { background:white; border:1px solid #D1D5DB; border-radius:6px;"
           "  padding:5px 9px; font-size:12px; color:#111827; }"
           "QComboBox::drop-down { border:none; width:20px; }"
           "QComboBox:hover { border-color:#3B82F6; }";
}

// ═══════════════════════════════════════════════════════════════════════════
// Constructor
// ═══════════════════════════════════════════════════════════════════════════
HybridTransferTab::HybridTransferTab(MainWindow* mainWindow, QWidget* parent)
    : QWidget(parent), m_mainWindow(mainWindow)
{
    m_mappingModel = new MappingModel(this);
    m_mappingController = nullptr;

    QVBoxLayout* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // ── Title bar ─────────────────────────────────────────────────────────
    QFrame* titleBar = new QFrame(this);
    titleBar->setStyleSheet(
        "QFrame { background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "  stop:0 #1E3A5F, stop:1 #4F46E5); border-radius:0; }");
    titleBar->setFixedHeight(60);
    QHBoxLayout* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(20, 0, 20, 0);

    QLabel* icon = new QLabel("🔀", titleBar);
    icon->setStyleSheet("font-size:22px;");
    QLabel* titleLbl = new QLabel("Hybrid Transfer", titleBar);
    titleLbl->setStyleSheet("font-weight:700; font-size:16px; color:white; letter-spacing:-0.3px;");
    QLabel* descLbl = new QLabel(
        "Assign each month to Execute All or Execute RT, then run both in sequence.", titleBar);
    descLbl->setStyleSheet("color:rgba(255,255,255,0.75); font-size:11px;");

    titleLayout->addWidget(icon);
    titleLayout->addSpacing(8);
    QVBoxLayout* ttl = new QVBoxLayout();
    ttl->setSpacing(1);
    ttl->addWidget(titleLbl);
    ttl->addWidget(descLbl);
    titleLayout->addLayout(ttl);
    titleLayout->addStretch();
    rootLayout->addWidget(titleBar);

    // ── Main splitter: left config | right mappings sidebar ───────────────
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setHandleWidth(5);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setStyleSheet(
        "QSplitter::handle { background:#E5E7EB; }"
        "QSplitter::handle:hover { background:#3B82F6; }");
    rootLayout->addWidget(m_splitter, 1);

    // ═══════════════════════════════════════════
    // LEFT PANEL — config
    // ═══════════════════════════════════════════
    QWidget* leftPanel = new QWidget();
    leftPanel->setMinimumWidth(360);
    leftPanel->setMaximumWidth(500);

    QScrollArea* leftScroll = new QScrollArea();
    leftScroll->setWidget(leftPanel);
    leftScroll->setWidgetResizable(true);
    leftScroll->setFrameShape(QFrame::NoFrame);
    leftScroll->setStyleSheet("QScrollArea { background:white; border:none; }");

    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(16, 16, 10, 16);
    leftLayout->setSpacing(12);

    // ── Card: Period Selection ────────────────────────────────────────────
    QFrame* periodCard = new QFrame();
    periodCard->setStyleSheet(h_card());
    QVBoxLayout* periodLayout = new QVBoxLayout(periodCard);
    periodLayout->setContentsMargins(14, 12, 14, 12);
    periodLayout->setSpacing(8);

    QLabel* periodTitle = new QLabel("📅  Period Selection", periodCard);
    periodTitle->setStyleSheet("font-weight:700; font-size:13px; color:#1F2937;");
    periodLayout->addWidget(periodTitle);

    // Year + Type row
    QHBoxLayout* yearTypeRow = new QHBoxLayout();
    yearTypeRow->setSpacing(8);

    QLabel* yearLbl = new QLabel("Year:", periodCard);
    yearLbl->setStyleSheet(h_lbl());
    yearTypeRow->addWidget(yearLbl);

    m_yearCombo = new QComboBox(periodCard);
    m_yearCombo->setStyleSheet(h_comboStyle());
    m_yearCombo->setFixedWidth(80);
    for (int y = 2010; y <= 2043; y++)
        m_yearCombo->addItem(QString::number(y));
    m_yearCombo->setCurrentText(QString::number(QDate::currentDate().year()));
    yearTypeRow->addWidget(m_yearCombo);

    yearTypeRow->addSpacing(8);

    QLabel* typeLbl = new QLabel("Type:", periodCard);
    typeLbl->setStyleSheet(h_lbl());
    yearTypeRow->addWidget(typeLbl);

    m_transferTypeCombo = new QComboBox(periodCard);
    m_transferTypeCombo->addItems({"Execute All", "Execute RT"});
    m_transferTypeCombo->setStyleSheet(h_comboStyle());
    m_transferTypeCombo->setFixedWidth(120);
    yearTypeRow->addWidget(m_transferTypeCombo);
    yearTypeRow->addStretch();
    periodLayout->addLayout(yearTypeRow);

    // Month checkboxes
    QLabel* monthsLbl = new QLabel("Months:", periodCard);
    monthsLbl->setStyleSheet(h_lbl());
    periodLayout->addWidget(monthsLbl);

    QGridLayout* monthGrid = new QGridLayout();
    monthGrid->setHorizontalSpacing(8);
    monthGrid->setVerticalSpacing(6);
    const QStringList shortMonths = {"Jan","Feb","Mar","Apr","May","Jun",
                                     "Jul","Aug","Sep","Oct","Nov","Dec"};
    m_monthCheckboxes.clear();
    for (int i = 0; i < 12; i++) {
        QCheckBox* cb = new QCheckBox(shortMonths[i], periodCard);
        cb->setStyleSheet(
            "QCheckBox { font-size:12px; color:#374151; spacing:4px; }"
            "QCheckBox::indicator { width:14px; height:14px; border-radius:3px;"
            "  border:1px solid #D1D5DB; background:white; }"
            "QCheckBox::indicator:checked { background:#3B82F6; border-color:#3B82F6; }"
            "QCheckBox::indicator:hover { border-color:#3B82F6; }");
        monthGrid->addWidget(cb, i / 6, i % 6);
        m_monthCheckboxes.append(cb);
    }
    periodLayout->addLayout(monthGrid);

    // Quick-select row
    QHBoxLayout* quickRow = new QHBoxLayout();
    quickRow->setSpacing(6);
    QLabel* quickLbl = new QLabel("Quick:", periodCard);
    quickLbl->setStyleSheet("font-size:11px; color:#6B7280; font-weight:500;");
    quickRow->addWidget(quickLbl);
    for (int q = 1; q <= 4; q++) {
        QPushButton* btn = new QPushButton(QString("Q%1").arg(q), periodCard);
        btn->setFixedHeight(24);
        btn->setStyleSheet(h_btnNeutral());
        connect(btn, &QPushButton::clicked, this, [this, q]() { selectQuarter(q); });
        quickRow->addWidget(btn);
    }
    QPushButton* allBtn = new QPushButton("All", periodCard);
    allBtn->setFixedHeight(24);
    allBtn->setStyleSheet(h_btnNeutral());
    connect(allBtn, &QPushButton::clicked, this, &HybridTransferTab::selectAllMonths);
    quickRow->addWidget(allBtn);
    QPushButton* noneBtn = new QPushButton("None", periodCard);
    noneBtn->setFixedHeight(24);
    noneBtn->setStyleSheet(h_btnNeutral());
    connect(noneBtn, &QPushButton::clicked, this, &HybridTransferTab::clearAllMonths);
    quickRow->addWidget(noneBtn);
    quickRow->addStretch();
    periodLayout->addLayout(quickRow);

    // Add button
    m_addBtn = new QPushButton("+ Add Selected Months", periodCard);
    m_addBtn->setStyleSheet(h_btnPrimary());
    periodLayout->addWidget(m_addBtn);

    leftLayout->addWidget(periodCard);

    // ── Card: Assigned Periods ────────────────────────────────────────────
    QFrame* tableCard = new QFrame();
    tableCard->setStyleSheet(h_card());
    QVBoxLayout* tableCardLayout = new QVBoxLayout(tableCard);
    tableCardLayout->setContentsMargins(14, 12, 14, 12);
    tableCardLayout->setSpacing(8);

    QHBoxLayout* tableHdr = new QHBoxLayout();
    QLabel* tableTitle = new QLabel("Assigned Periods", tableCard);
    tableTitle->setStyleSheet("font-weight:700; font-size:13px; color:#1F2937;");
    tableHdr->addWidget(tableTitle);
    tableHdr->addStretch();

    m_moveUpBtn = new QPushButton("▲", tableCard);
    m_moveUpBtn->setFixedSize(28, 26);
    m_moveUpBtn->setStyleSheet(h_btnNeutral());
    m_moveUpBtn->setEnabled(false);
    m_moveUpBtn->setToolTip("Move up");

    m_moveDownBtn = new QPushButton("▼", tableCard);
    m_moveDownBtn->setFixedSize(28, 26);
    m_moveDownBtn->setStyleSheet(h_btnNeutral());
    m_moveDownBtn->setEnabled(false);
    m_moveDownBtn->setToolTip("Move down");

    m_removeBtn = new QPushButton("Remove", tableCard);
    m_removeBtn->setStyleSheet(h_btnDanger());
    m_removeBtn->setEnabled(false);

    m_clearBtn = new QPushButton("Clear All", tableCard);
    m_clearBtn->setStyleSheet(h_btnNeutral());

    tableHdr->addWidget(m_moveUpBtn);
    tableHdr->addWidget(m_moveDownBtn);
    tableHdr->addWidget(m_removeBtn);
    tableHdr->addWidget(m_clearBtn);
    tableCardLayout->addLayout(tableHdr);

    m_table = new QTableWidget(tableCard);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({"Year", "Month", "Type", "Status"});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->setMinimumHeight(180);
    m_table->setMaximumHeight(260);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->setShowGrid(false);
    m_table->setStyleSheet(
        "QTableWidget { background:white; gridline-color:#E5E7EB;"
        "  border-radius:7px; font-size:12px; border:1px solid #E5E7EB; }"
        "QTableWidget::item { padding:5px 8px; }"
        "QTableWidget::item:selected { background:#DBEAFE; color:#1E40AF; }"
        "QTableWidget::item:alternate { background:#F9FAFB; }"
        "QHeaderView::section { background:#F3F4F6; color:#374151; padding:7px 8px;"
        "  font-weight:600; font-size:11px; border:none; border-bottom:2px solid #E5E7EB; }");
    tableCardLayout->addWidget(m_table);
    leftLayout->addWidget(tableCard);

    // ── Card: Execution Options ───────────────────────────────────────────
    QFrame* execCard = new QFrame();
    execCard->setStyleSheet(h_card());
    QVBoxLayout* execLayout = new QVBoxLayout(execCard);
    execLayout->setContentsMargins(14, 12, 14, 12);
    execLayout->setSpacing(8);

    QLabel* execTitle = new QLabel("⚙  Execution Options", execCard);
    execTitle->setStyleSheet("font-weight:700; font-size:13px; color:#1F2937;");
    execLayout->addWidget(execTitle);

    QHBoxLayout* orderRow = new QHBoxLayout();
    QLabel* orderLbl = new QLabel("Order:", execCard);
    orderLbl->setStyleSheet(h_lbl());
    orderRow->addWidget(orderLbl);
    m_executeAllFirstRadio = new QRadioButton("Execute All → RT", execCard);
    m_executeAllFirstRadio->setChecked(true);
    m_executeAllFirstRadio->setStyleSheet("font-size:12px; color:#374151;");
    m_executeRTFirstRadio = new QRadioButton("RT → Execute All", execCard);
    m_executeRTFirstRadio->setStyleSheet("font-size:12px; color:#374151;");
    orderRow->addWidget(m_executeAllFirstRadio);
    orderRow->addWidget(m_executeRTFirstRadio);
    orderRow->addStretch();
    execLayout->addLayout(orderRow);

    m_summaryLabel = new QLabel("No periods assigned yet.", execCard);
    m_summaryLabel->setStyleSheet("color:#6B7280; font-size:11px; font-style:italic;");
    m_summaryLabel->setWordWrap(true);
    execLayout->addWidget(m_summaryLabel);

    m_phaseStatusLabel = new QLabel("", execCard);
    m_phaseStatusLabel->setStyleSheet(
        "color:#3B82F6; font-weight:600; font-size:11px; padding:6px 10px;"
        "background:#EFF6FF; border-radius:5px; border-left:3px solid #3B82F6;");
    m_phaseStatusLabel->setWordWrap(true);
    m_phaseStatusLabel->setVisible(false);
    execLayout->addWidget(m_phaseStatusLabel);

    m_progressBar = new QProgressBar(execCard);
    m_progressBar->setMaximumHeight(5);
    m_progressBar->setTextVisible(false);
    m_progressBar->setVisible(false);
    m_progressBar->setStyleSheet(
        "QProgressBar { background:#E5E7EB; border-radius:2px; border:none; }"
        "QProgressBar::chunk { background:#3B82F6; border-radius:2px; }");
    execLayout->addWidget(m_progressBar);

    leftLayout->addWidget(execCard);

    // ── Action buttons ────────────────────────────────────────────────────
    QHBoxLayout* actionRow = new QHBoxLayout();
    actionRow->setSpacing(8);

    m_executeBtn = new QPushButton("▶ Execute Hybrid Transfer");
    m_executeBtn->setEnabled(false);
    m_executeBtn->setStyleSheet(h_btnSuccess());
    actionRow->addWidget(m_executeBtn);

    m_updateMappingsBtn = new QPushButton("\xF0\x9F\x94\x84 Update Row Mappings");
    m_updateMappingsBtn->setStyleSheet(
        "QPushButton {"
        "  background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #14B8A6,stop:1 #0D9488);"
        "  color:white; font-weight:600; font-size:12px;"
        "  padding:7px 18px; border-radius:6px; border:none; }"
        "QPushButton:hover { background:#0D9488; }"
        "QPushButton:pressed { background:#0F766E; }"
        "QPushButton:disabled { background:#E5E7EB; color:#9CA3AF; }");
    m_updateMappingsBtn->setToolTip(
        "Scan source + destination Excel files by text label in column B,\n"
        "find current row numbers, rewrite JSON mapping files.\n\n"
        "First run: bootstraps label_definitions.json from current mappings.\n"
        "Subsequent: uses saved labels to find rows in new files.");
    connect(m_updateMappingsBtn, &QPushButton::clicked, this, &HybridTransferTab::onUpdateMappings);
    actionRow->addWidget(m_updateMappingsBtn);

    QPushButton* resetBtn = new QPushButton("↺ Reset All");
    resetBtn->setStyleSheet(h_btnDanger());
    connect(resetBtn, &QPushButton::clicked, this, &HybridTransferTab::onResetAll);
    actionRow->addWidget(resetBtn);
    actionRow->addStretch();
    leftLayout->addLayout(actionRow);

    leftLayout->addStretch();

    m_splitter->addWidget(leftScroll);

    // ═══════════════════════════════════════════
    // RIGHT PANEL — Mapping sidebar
    // ═══════════════════════════════════════════
    // setupMappingsSidebar() adds to m_splitter
    setupMappingsSidebar();

    m_splitter->setSizes({420, 700});
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);

    // ── Connections ──────────────────────────────────────────────────────
    connect(m_addBtn,     &QPushButton::clicked, this, &HybridTransferTab::onAddPeriod);
    connect(m_removeBtn,  &QPushButton::clicked, this, &HybridTransferTab::onRemovePeriod);
    connect(m_clearBtn,   &QPushButton::clicked, this, &HybridTransferTab::onClearAll);
    connect(m_executeBtn, &QPushButton::clicked, this, &HybridTransferTab::onExecute);
    connect(m_moveUpBtn,  &QPushButton::clicked, this, &HybridTransferTab::onMoveUp);
    connect(m_moveDownBtn,&QPushButton::clicked, this, &HybridTransferTab::onMoveDown);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
        m_removeBtn->setEnabled(!m_table->selectedItems().isEmpty());
        updateMoveButtons();
    });
}

// ═══════════════════════════════════════════════════════════════════════════
// onAddPeriod
// ═══════════════════════════════════════════════════════════════════════════
void HybridTransferTab::onAddPeriod()
{
    QString year = m_yearCombo->currentText();
    QString type = m_transferTypeCombo->currentText();

    QVector<int> selectedIndices;
    for (int i = 0; i < m_monthCheckboxes.size(); i++) {
        if (m_monthCheckboxes[i]->isChecked())
            selectedIndices.append(i);
    }

    if (selectedIndices.isEmpty()) {
        QMessageBox::warning(this, "No Months Selected", "Please select at least one month.");
        return;
    }

    if (type == "Execute RT") {
        if (selectedIndices.size() < 2) {
            QMessageBox::warning(this, "Insufficient Months",
                "Execute RT requires at least 2 consecutive months.");
            return;
        }
        std::sort(selectedIndices.begin(), selectedIndices.end());
        for (int i = 1; i < selectedIndices.size(); i++) {
            if (selectedIndices[i] != selectedIndices[i-1] + 1) {
                QMessageBox::warning(this, "Non-Consecutive Months",
                    "Execute RT requires consecutive months (e.g. Jan-Feb-Mar).");
                return;
            }
        }
    }

    static const QStringList fullMonths = {
        "January","February","March","April","May","June",
        "July","August","September","October","November","December"};

    int addedCount = 0, skippedCount = 0;
    QStringList skipped;

    for (int idx : selectedIndices) {
        const QString month = fullMonths[idx];
        const QString key   = QString("%1_%2").arg(month, year);

        if (m_assignments.contains(key)) { skippedCount++; skipped.append(month); continue; }

        // Load mapping cards for this period (only first time)
        m_mainWindow->populateFillAllMappingCards(m_mappingController, month, year.toInt());

        m_assignments[key]  = (type == "Execute All") ? "execute_all" : "execute_rt";
        m_orderedKeys.append(key);
        addedCount++;
    }

    populateTable();
    updateSummary();
    updateExecuteButton();
    clearAllMonths();

    if (addedCount == 0) {
        QMessageBox::information(this, "Already Assigned",
            "All selected months are already assigned:\n" + skipped.join(", "));
    } else if (skippedCount > 0) {
        QMessageBox::information(this, "Partially Added",
            QString("Added %1, skipped %2 (already assigned):\n%3")
                .arg(addedCount).arg(skippedCount).arg(skipped.join(", ")));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Month helpers
// ═══════════════════════════════════════════════════════════════════════════
void HybridTransferTab::selectQuarter(int q) {
    int start = (q - 1) * 3;
    for (int i = 0; i < 12; i++) m_monthCheckboxes[i]->setChecked(i >= start && i < start + 3);
}
void HybridTransferTab::selectAllMonths() {
    for (auto* cb : m_monthCheckboxes) cb->setChecked(true);
}
void HybridTransferTab::clearAllMonths() {
    for (auto* cb : m_monthCheckboxes) cb->setChecked(false);
}

// ═══════════════════════════════════════════════════════════════════════════
// Table actions
// ═══════════════════════════════════════════════════════════════════════════
void HybridTransferTab::onRemovePeriod()
{
    QSet<int> rows;
    for (auto* it : m_table->selectedItems()) rows.insert(it->row());
    for (int row : rows) {
        if (row < m_orderedKeys.size()) {
            const QString key = m_orderedKeys[row];
            m_assignments.remove(key);
            m_orderedKeys.removeAll(key);
        }
    }
    populateTable(); updateSummary(); updateExecuteButton();
}

void HybridTransferTab::onClearAll()
{
    m_assignments.clear(); m_orderedKeys.clear();
    populateTable(); updateSummary(); updateExecuteButton();
}

void HybridTransferTab::onResetAll()
{
    if (QMessageBox::question(this, "Reset Hybrid Mode",
            "Clear all assignments, mapping cards and selections?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    m_assignments.clear(); m_orderedKeys.clear(); populateTable();
    if (m_mappingController) m_mappingController->clearAllMappings();
    clearAllMonths();
    m_yearCombo->setCurrentText(QString::number(QDate::currentDate().year()));
    m_transferTypeCombo->setCurrentIndex(0);
    m_executeAllFirstRadio->setChecked(true);
    m_progressBar->setVisible(false); m_progressBar->setValue(0);
    m_phaseStatusLabel->setVisible(false); m_phaseStatusLabel->setText("");
    updateSummary(); updateExecuteButton();
}

// ═══════════════════════════════════════════════════════════════════════════
// Execute
// ═══════════════════════════════════════════════════════════════════════════
void HybridTransferTab::onExecute()
{
    m_config = HybridTransferConfig();
    m_config.executeAllFirst = m_executeAllFirstRadio->isChecked();

    QSet<QString> allKeys;
    const QVector<QString>& ordered = m_orderedKeys;

    for (const QString& key : ordered) {
        if (!m_assignments.contains(key)) continue;
        int us = key.lastIndexOf('_');
        QString month = key.left(us);
        int year = key.mid(us + 1).toInt();
        if (m_assignments[key] == "execute_all") {
            m_config.executeAllPeriods.append({month, year});
            allKeys.insert(key);
        } else {
            m_config.executeRTPeriods.append({month, year});
        }
    }

    if (m_config.isEmpty()) {
        QMessageBox::information(this, "Nothing to Execute", "Add periods first.");
        return;
    }

    if (m_mappingController && !allKeys.isEmpty()) {
        QVector<MappingItem> items = m_mappingController->items();
        for (int i = 0; i < items.size(); ++i) {
            MappingRow* row = m_mappingController->rowAt(i);
            if (!row || !row->isChecked()) continue;
            const MappingItem& item = items[i];
            QString key = QString("%1_%2").arg(item.entry.month).arg(item.year);
            if (key.isEmpty()) key = QString("%1_%2").arg(item.month).arg(item.year);
            if (allKeys.contains(key)) m_config.executeAllItems.append(item);
        }
    }

    emit executeRequested(m_config);
}

// ═══════════════════════════════════════════════════════════════════════════
// Table population
// ═══════════════════════════════════════════════════════════════════════════
void HybridTransferTab::populateTable()
{
    int selRow = m_table->selectedItems().isEmpty() ? -1
                 : m_table->selectedItems().first()->row();
    m_table->setRowCount(0);

    if (m_orderedKeys.isEmpty() && !m_assignments.isEmpty()) {
        QStringList keys = m_assignments.keys();
        static const QStringList months = {
            "January","February","March","April","May","June",
            "July","August","September","October","November","December"};
        std::sort(keys.begin(), keys.end(), [&](const QString& a, const QString& b) {
            int ua = a.lastIndexOf('_'), ub = b.lastIndexOf('_');
            int ya = a.mid(ua+1).toInt(), yb = b.mid(ub+1).toInt();
            if (ya != yb) return ya < yb;
            return months.indexOf(a.left(ua)) < months.indexOf(b.left(ub));
        });
        m_orderedKeys = QVector<QString>(keys.begin(), keys.end());
    }

    for (const QString& key : m_orderedKeys) {
        if (!m_assignments.contains(key)) continue;
        int us = key.lastIndexOf('_');
        QString month = key.left(us), year = key.mid(us+1);
        bool isRT = (m_assignments[key] == "execute_rt");
        int r = m_table->rowCount();
        m_table->insertRow(r);

        auto mk = [&](const QString& txt) {
            auto* it = new QTableWidgetItem(txt);
            if (isRT) it->setBackground(QColor("#FFFBEB"));
            return it;
        };
        m_table->setItem(r, 0, mk(year));
        m_table->setItem(r, 1, mk(month));
        auto* typeIt = new QTableWidgetItem(isRT ? "Execute RT" : "Execute All");
        typeIt->setForeground(QColor(isRT ? "#D97706" : "#059669"));
        QFont f = typeIt->font(); f.setBold(true); typeIt->setFont(f);
        if (isRT) typeIt->setBackground(QColor("#FFFBEB"));
        m_table->setItem(r, 2, typeIt);
        m_table->setItem(r, 3, mk("Pending"));
    }

    if (selRow >= 0 && selRow < m_table->rowCount()) m_table->selectRow(selRow);
    updateMoveButtons();
}

void HybridTransferTab::updateSummary()
{
    int allC = 0, rtC = 0;
    for (const QString& t : m_assignments) { if (t=="execute_all") allC++; else rtC++; }

    if (m_assignments.isEmpty()) {
        m_summaryLabel->setText("No periods assigned yet.");
    } else {
        m_summaryLabel->setText(
            QString("%1 period(s): <b style='color:#059669'>%2 Execute All</b>, "
                    "<b style='color:#D97706'>%3 RT</b>")
                .arg(m_assignments.size()).arg(allC).arg(rtC));
    }
}

void HybridTransferTab::updateExecuteButton()
{
    m_executeBtn->setEnabled(!m_assignments.isEmpty());
}

// ═══════════════════════════════════════════════════════════════════════════
// Phase callbacks
// ═══════════════════════════════════════════════════════════════════════════
void HybridTransferTab::onPhaseStarted(const QString& phaseName)
{
    m_phaseStatusLabel->setText(QString("⏳ %1…").arg(phaseName));
    m_phaseStatusLabel->setStyleSheet(
        "color:#3B82F6; font-weight:600; font-size:11px; padding:6px 10px;"
        "background:#EFF6FF; border-radius:5px; border-left:3px solid #3B82F6;");
    m_phaseStatusLabel->setVisible(true);
    m_executeBtn->setEnabled(false);
}

void HybridTransferTab::onPhaseFinished(const QString& phaseName, bool success)
{
    QString col = success ? "#059669" : "#DC2626";
    QString bg  = success ? "#ECFDF5" : "#FEF2F2";
    m_phaseStatusLabel->setText(QString("%1 %2").arg(success?"✓":"✗", phaseName));
    m_phaseStatusLabel->setStyleSheet(
        QString("color:%1; font-weight:600; font-size:11px; padding:6px 10px;"
                "background:%2; border-radius:5px; border-left:3px solid %1;").arg(col, bg));
}

void HybridTransferTab::onProgressUpdate(int percent, const QString&)
{
    m_progressBar->setValue(percent);
    m_progressBar->setVisible(true);
}

void HybridTransferTab::onAllFinished(bool success, const QString& summary)
{
    m_progressBar->setVisible(false);
    m_executeBtn->setEnabled(true);
    onPhaseFinished("Hybrid Transfer Complete", success);

    QMessageBox mb(this);
    mb.setWindowTitle("Hybrid Transfer Complete");
    mb.setText(success ? "Transfer completed successfully!" : "Transfer completed with errors.");
    mb.setDetailedText(summary);
    mb.setIcon(success ? QMessageBox::Information : QMessageBox::Warning);
    mb.exec();

    QTimer::singleShot(5000, this, [this]() { m_phaseStatusLabel->setVisible(false); });
}

// ═══════════════════════════════════════════════════════════════════════════
// Mapping sidebar (right panel)
// ═══════════════════════════════════════════════════════════════════════════
void HybridTransferTab::setupMappingsSidebar()
{
    m_mappingsSidebar = new QFrame();
    m_mappingsSidebar->setMinimumWidth(400);
    m_mappingsSidebar->setStyleSheet(
        "QFrame { background:#F9FAFB; border:none; }");

    QVBoxLayout* sl = new QVBoxLayout(m_mappingsSidebar);
    sl->setContentsMargins(16, 16, 16, 16);
    sl->setSpacing(10);

    QHBoxLayout* hdr = new QHBoxLayout();
    QLabel* sideTitle = new QLabel("Row Mappings");
    sideTitle->setStyleSheet("font-weight:700; font-size:14px; color:#1F2937;");
    hdr->addWidget(sideTitle);
    hdr->addStretch();

    QPushButton* selAll = new QPushButton("✔ All");
    selAll->setStyleSheet(h_btnNeutral()); selAll->setFixedHeight(24);
    connect(selAll, &QPushButton::clicked, this, [this]() {
        if (m_mappingController) m_mappingController->setAllChecked(true);
    });
    QPushButton* selNone = new QPushButton("✗ None");
    selNone->setStyleSheet(h_btnNeutral()); selNone->setFixedHeight(24);
    connect(selNone, &QPushButton::clicked, this, [this]() {
        if (m_mappingController) m_mappingController->setAllChecked(false);
    });
    hdr->addWidget(selAll);
    hdr->addWidget(selNone);
    sl->addLayout(hdr);

    QFrame* sep = new QFrame();
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("background:#E5E7EB; border:none;");
    sl->addWidget(sep);

    m_mappingsContainer = new QWidget();
    m_mappingsContainer->setStyleSheet("background:transparent;");
    m_mappingsLayout = new QVBoxLayout(m_mappingsContainer);
    m_mappingsLayout->setSpacing(8);
    m_mappingsLayout->setContentsMargins(0, 0, 0, 0);

    QScrollArea* scroll = new QScrollArea();
    scroll->setWidget(m_mappingsContainer);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("QScrollArea { background:transparent; border:none; }");
    sl->addWidget(scroll, 1);

    m_noMappingsLabel = new QLabel("No mappings loaded.\nAdd periods to load mapping cards.");
    m_noMappingsLabel->setStyleSheet(
        "color:#8B92A7; font-style:italic; font-size:12px; padding:40px 20px;"
        "background:rgba(255,255,255,0.5); border-radius:10px; border:2px dashed #D1D5DB;");
    m_noMappingsLabel->setAlignment(Qt::AlignCenter);
    sl->addWidget(m_noMappingsLabel);

    m_mappingController = new MappingController(
        m_mappingModel, m_mappingsContainer, m_mappingsLayout, this);

    connect(m_mappingController, &MappingController::rowCountChanged, this, [this](int cnt) {
        m_noMappingsLabel->setVisible(cnt == 0);
    });

    m_splitter->addWidget(m_mappingsSidebar);
}

// ═══════════════════════════════════════════════════════════════════════════
// Move Up / Move Down
// ═══════════════════════════════════════════════════════════════════════════
QVector<int> HybridTransferTab::rtBlockFor(int rowIndex) const
{
    if (rowIndex < 0 || rowIndex >= m_orderedKeys.size()) return {};
    const QString& key = m_orderedKeys[rowIndex];
    if (!m_assignments.contains(key) || m_assignments[key] == "execute_all")
        return { rowIndex };

    int s = rowIndex;
    while (s > 0 && m_assignments.value(m_orderedKeys[s-1]) == "execute_rt") --s;
    int e = rowIndex;
    while (e+1 < m_orderedKeys.size() && m_assignments.value(m_orderedKeys[e+1]) == "execute_rt") ++e;

    QVector<int> block;
    for (int i = s; i <= e; ++i) block.append(i);
    return block;
}

void HybridTransferTab::updateMoveButtons()
{
    if (m_table->selectedItems().isEmpty()) {
        m_moveUpBtn->setEnabled(false); m_moveDownBtn->setEnabled(false); return;
    }
    int row = m_table->selectedItems().first()->row();
    QVector<int> block = rtBlockFor(row);
    int top = block.isEmpty() ? row : block.first();
    int bot = block.isEmpty() ? row : block.last();
    m_moveUpBtn->setEnabled(top > 0);
    m_moveDownBtn->setEnabled(bot < m_orderedKeys.size() - 1);
}

void HybridTransferTab::onMoveUp()
{
    if (m_table->selectedItems().isEmpty()) return;
    int row = m_table->selectedItems().first()->row();
    QVector<int> block = rtBlockFor(row);
    if (block.isEmpty() || block.first() <= 0) return;
    int top = block.first(), bot = block.last();
    QString displaced = m_orderedKeys[top - 1];
    m_orderedKeys.removeAt(top - 1);
    m_orderedKeys.insert(bot, displaced);
    populateTable();
    m_table->selectRow(row - 1);
}

void HybridTransferTab::onMoveDown()
{
    if (m_table->selectedItems().isEmpty()) return;
    int row = m_table->selectedItems().first()->row();
    QVector<int> block = rtBlockFor(row);
    if (block.isEmpty() || block.last() >= m_orderedKeys.size() - 1) return;
    int top = block.first(), bot = block.last();
    QString displaced = m_orderedKeys[bot + 1];
    m_orderedKeys.removeAt(bot + 1);
    m_orderedKeys.insert(top, displaced);
    populateTable();
    m_table->selectRow(row + 1);
}

// ═══════════════════════════════════════════════════════════════════════════
// Update Row Mappings — label-based row finder
// ═══════════════════════════════════════════════════════════════════════════
void HybridTransferTab::onUpdateMappings()
{
    // Determine JSON directory
    QString jsonDir = QCoreApplication::applicationDirPath() + "/JSON";
    if (!QDir(jsonDir).exists())
        jsonDir = QCoreApplication::applicationDirPath() + "/../JSON";
    if (!QDir(jsonDir).exists())
        jsonDir = "JSON";

    QString labelDefPath = jsonDir + "/label_definitions.json";
    QString mappingsPath = jsonDir + "/mappings.json";

    if (!QFile::exists(labelDefPath)) {
        QMessageBox::critical(this, "Missing File",
            "label_definitions.json not found.\n"
            "Expected at: " + labelDefPath + "\n\n"
            "Create this file with label entries first.");
        return;
    }

    // Load config from JSON
    LabelConfig cfg = MappingUpdater::loadConfig(labelDefPath);

    // ── Build the dialog ──────────────────────────────────────────────────
    QDialog dlg(this);
    dlg.setWindowTitle("🔄 Update Row Mappings");
    dlg.setMinimumWidth(560);
    dlg.setStyleSheet(
        "QDialog { background:white; }"
        "QLabel { color:#374151; }"
        "QLineEdit { background:white; border:1px solid #D1D5DB; border-radius:5px;"
        "  padding:6px 10px; font-size:12px; color:#111827; }"
        "QLineEdit:read-only { background:#F9FAFB; color:#6B7280; }"
        "QPushButton { font-size:12px; padding:6px 14px; border-radius:5px; }");

    QVBoxLayout* mainLayout = new QVBoxLayout(&dlg);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(20, 20, 20, 16);

    // Title
    QLabel* titleLbl = new QLabel("🔄 Update Row Mappings");
    titleLbl->setStyleSheet("font-weight:700; font-size:16px; color:#1F2937;");
    mainLayout->addWidget(titleLbl);

    QLabel* descLbl = new QLabel(
        "Scans both Excel files for label text, finds current row numbers,\n"
        "and rewrites the rowMap in mappings.json.");
    descLbl->setStyleSheet("color:#6B7280; font-size:11px;");
    mainLayout->addWidget(descLbl);

    // ── Config card ──
    QFrame* cfgCard = new QFrame();
    cfgCard->setStyleSheet(
        "QFrame { background:#F0F9FF; border:1px solid #BAE6FD; border-radius:8px; }");
    QGridLayout* cfgGrid = new QGridLayout(cfgCard);
    cfgGrid->setContentsMargins(14, 10, 14, 10);
    cfgGrid->setHorizontalSpacing(12);
    cfgGrid->setVerticalSpacing(6);

    auto addCfgRow = [&](int row, const QString& label, const QString& value) {
        QLabel* l = new QLabel(label);
        l->setStyleSheet("font-weight:600; font-size:12px; color:#0369A1;");
        QLabel* v = new QLabel(value);
        v->setStyleSheet("font-size:12px; color:#1E3A5F;");
        cfgGrid->addWidget(l, row, 0);
        cfgGrid->addWidget(v, row, 1);
    };

    addCfgRow(0, "Source Sheet:", cfg.sourceSheet);
    addCfgRow(1, "Source Label Col(s):", cfg.sourceLabelCols.join(", "));
    addCfgRow(2, "Dest Sheet:", cfg.destSheet);
    addCfgRow(3, "Dest Label Col(s):", cfg.destLabelCols.join(", "));
    addCfgRow(4, "Labels:", QString("%1 simple + %2 SUM = %3 total")
        .arg(cfg.simpleCount).arg(cfg.sumCount).arg(cfg.simpleCount + cfg.sumCount));
    mainLayout->addWidget(cfgCard);

    // ── File selection ──
    QLabel* srcLbl = new QLabel("Source Excel (SAP Report):");
    srcLbl->setStyleSheet("font-weight:600; font-size:12px;");
    mainLayout->addWidget(srcLbl);

    QHBoxLayout* srcRow = new QHBoxLayout();
    QLineEdit* srcEdit = new QLineEdit();
    srcEdit->setReadOnly(true);
    srcEdit->setPlaceholderText("Click Browse to select...");
    QPushButton* srcBtn = new QPushButton("Browse...");
    srcBtn->setStyleSheet(
        "QPushButton { background:#3B82F6; color:white; font-weight:600; border:none; }"
        "QPushButton:hover { background:#2563EB; }");
    srcRow->addWidget(srcEdit, 1);
    srcRow->addWidget(srcBtn);
    mainLayout->addLayout(srcRow);

    QLabel* dstLbl = new QLabel("Destination Excel (MZLZ Cost Control):");
    dstLbl->setStyleSheet("font-weight:600; font-size:12px;");
    mainLayout->addWidget(dstLbl);

    QHBoxLayout* dstRow = new QHBoxLayout();
    QLineEdit* dstEdit = new QLineEdit();
    dstEdit->setReadOnly(true);
    dstEdit->setPlaceholderText("Click Browse to select...");
    QPushButton* dstBtn = new QPushButton("Browse...");
    dstBtn->setStyleSheet(
        "QPushButton { background:#3B82F6; color:white; font-weight:600; border:none; }"
        "QPushButton:hover { background:#2563EB; }");
    dstRow->addWidget(dstEdit, 1);
    dstRow->addWidget(dstBtn);
    mainLayout->addLayout(dstRow);

    // ── Results area (hidden initially) ──
    QFrame* resultsCard = new QFrame();
    resultsCard->setVisible(false);
    resultsCard->setStyleSheet(
        "QFrame { background:#F0FDF4; border:1px solid #BBF7D0; border-radius:8px; }");
    QVBoxLayout* resLayout = new QVBoxLayout(resultsCard);
    resLayout->setContentsMargins(14, 10, 14, 10);
    QLabel* resTitle = new QLabel("Results");
    resTitle->setStyleSheet("font-weight:700; font-size:13px; color:#166534;");
    resLayout->addWidget(resTitle);
    QLabel* resText = new QLabel();
    resText->setStyleSheet("font-size:12px; color:#374151;");
    resText->setWordWrap(true);
    resLayout->addWidget(resText);
    QTextEdit* resDetails = new QTextEdit();
    resDetails->setReadOnly(true);
    resDetails->setMaximumHeight(200);
    resDetails->setStyleSheet(
        "QTextEdit { background:white; border:1px solid #D1D5DB; border-radius:5px;"
        "  font-family:Consolas,monospace; font-size:11px; }");
    resDetails->setVisible(false);
    resLayout->addWidget(resDetails);
    mainLayout->addWidget(resultsCard);

    // ── Buttons ──
    QHBoxLayout* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    QPushButton* cancelBtn = new QPushButton("Cancel");
    cancelBtn->setStyleSheet(
        "QPushButton { background:white; border:1px solid #D1D5DB; color:#374151; font-weight:500; }"
        "QPushButton:hover { background:#F3F4F6; }");
    QPushButton* runBtn = new QPushButton("▶ Run Update");
    runBtn->setEnabled(false);
    runBtn->setStyleSheet(
        "QPushButton { background:#059669; color:white; font-weight:700; border:none; }"
        "QPushButton:hover { background:#047857; }"
        "QPushButton:disabled { background:#E5E7EB; color:#9CA3AF; }");
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(runBtn);
    mainLayout->addLayout(btnRow);

    // ── Connect file browsers ──
    connect(srcBtn, &QPushButton::clicked, &dlg, [&]() {
        QString f = QFileDialog::getOpenFileName(&dlg,
            "Select Source Excel", QString(),
            "Excel Files (*.xlsx *.xlsm *.xls);;All Files (*)");
        if (!f.isEmpty()) {
            srcEdit->setText(f);
            runBtn->setEnabled(!srcEdit->text().isEmpty() && !dstEdit->text().isEmpty());
        }
    });
    connect(dstBtn, &QPushButton::clicked, &dlg, [&]() {
        QString f = QFileDialog::getOpenFileName(&dlg,
            "Select Destination Excel", QString(),
            "Excel Files (*.xlsx *.xlsm *.xls);;All Files (*)");
        if (!f.isEmpty()) {
            dstEdit->setText(f);
            runBtn->setEnabled(!srcEdit->text().isEmpty() && !dstEdit->text().isEmpty());
        }
    });

    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    // ── Run button ──
    connect(runBtn, &QPushButton::clicked, &dlg, [&]() {
        runBtn->setEnabled(false);
        runBtn->setText("Scanning...");
        QCoreApplication::processEvents();

        ExcelHandler handler;
        UpdateResult result = MappingUpdater::updateMappings(
            handler,
            srcEdit->text(), cfg.sourceSheet,
            dstEdit->text(), cfg.destSheet,
            labelDefPath, mappingsPath,
            cfg.sourceLabelNums, cfg.destLabelNums);

        runBtn->setText("▶ Run Update");
        runBtn->setEnabled(true);

        // Show results in-dialog
        bool ok = (result.notFound == 0 && result.ambiguous == 0);
        resultsCard->setStyleSheet(ok
            ? "QFrame { background:#F0FDF4; border:1px solid #BBF7D0; border-radius:8px; }"
            : "QFrame { background:#FEF2F2; border:1px solid #FECACA; border-radius:8px; }");
        resTitle->setStyleSheet(ok
            ? "font-weight:700; font-size:13px; color:#166534;"
            : "font-weight:700; font-size:13px; color:#991B1B;");
        resTitle->setText(ok ? "✓ Update Complete" : "⚠ Update Complete (with issues)");

        resText->setText(QString(
            "Total labels: %1  |  Matched: %2  |  Not found: %3")
            .arg(result.total).arg(result.matched).arg(result.notFound));

        // Build details text
        QString detailsTxt;
        if (!result.warnings.isEmpty()) {
            detailsTxt += "=== NOT FOUND ===\n";
            for (const QString& w : result.warnings)
                detailsTxt += w + "\n";
            detailsTxt += "\n";
        }
        if (!result.details.isEmpty()) {
            detailsTxt += "=== MATCHED ===\n";
            for (const QString& d : result.details)
                detailsTxt += d + "\n";
        }
        resDetails->setPlainText(detailsTxt);
        resDetails->setVisible(true);
        resultsCard->setVisible(true);

        if (m_mainWindow) {
            qDebug() << "[MappingUpdater] Mappings JSON updated. Reload on next use.";
        }
    });

    dlg.exec();
}
