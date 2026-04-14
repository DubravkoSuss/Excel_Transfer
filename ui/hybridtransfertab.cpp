#include "hybridtransfertab.h"
#include "mainwindow.h"
#include <QFrame>
#include <QLabel>
#include <QHeaderView>
#include <QMessageBox>
#include <QDate>
#include <QTimer>
#include <QSplitter>

HybridTransferTab::HybridTransferTab(MainWindow* mainWindow, QWidget* parent)
    : QWidget(parent), m_mainWindow(mainWindow)
{
    // Create mapping model and controller for this tab
    m_mappingModel = new MappingModel(this);
    m_mappingController = nullptr;  // Will be initialized in setupMappingsSidebar
    
    // Create main horizontal splitter for sidebar + content
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(1);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(m_splitter);
    
    // Setup mappings sidebar
    setupMappingsSidebar();
    
    // Create right side content
    QWidget* rightContent = new QWidget();
    QVBoxLayout* rightLayout = new QVBoxLayout(rightContent);
    rightLayout->setContentsMargins(16, 16, 16, 16);
    rightLayout->setSpacing(12);

    // ── Title ──
    QLabel* titleLabel = new QLabel("Hybrid Transfer");
    titleLabel->setStyleSheet(
        "font-weight: 700; font-size: 18px; color: #1F2937; "
        "letter-spacing: -0.3px; margin-bottom: 4px;"
    );
    rightLayout->addWidget(titleLabel);

    QLabel* descLabel = new QLabel(
        "Assign each month to either Execute All (cell-by-cell) or "
        "Execute RT (rolling transfer), then run both in sequence automatically.");
    descLabel->setStyleSheet(
        "color: #6B7280; font-size: 13px; margin-bottom: 8px;");
    descLabel->setWordWrap(true);
    rightLayout->addWidget(descLabel);

    // ── Assignment Card ──
    QFrame* assignCard = new QFrame();
    assignCard->setStyleSheet(
        "QFrame {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #FAFBFC, stop:1 #F5F7FA);"
        "  border-radius: 12px;"
        "  padding: 16px;"
        "}"
    );
    QVBoxLayout* cardLayout = new QVBoxLayout(assignCard);

    QLabel* assignTitle = new QLabel("Assign Periods");
    assignTitle->setStyleSheet("font-weight: 600; font-size: 14px; color: #374151;");
    cardLayout->addWidget(assignTitle);

    // Selection row
    QHBoxLayout* selRow = new QHBoxLayout();
    selRow->setSpacing(10);

    QLabel* yearLabel = new QLabel("Year:");
    yearLabel->setStyleSheet("font-weight: 600; color: #374151;");
    selRow->addWidget(yearLabel);
    
    m_yearCombo = new QComboBox();
    m_yearCombo->setStyleSheet(
        "QComboBox {"
        "  background: white; border: 1px solid #E5E7EB; border-radius: 6px;"
        "  padding: 6px 12px; font-size: 13px; min-width: 80px;"
        "}"
        "QComboBox::drop-down { border: none; }"
        "QComboBox:hover { border-color: #3B82F6; }"
    );
    for (int y = 2024; y <= 2030; y++)
        m_yearCombo->addItem(QString::number(y));
    m_yearCombo->setCurrentText(QString::number(QDate::currentDate().year()));
    selRow->addWidget(m_yearCombo);

    QLabel* typeLabel = new QLabel("Transfer Type:");
    typeLabel->setStyleSheet("font-weight: 600; color: #374151;");
    selRow->addWidget(typeLabel);
    
    m_transferTypeCombo = new QComboBox();
    m_transferTypeCombo->addItems({"Execute All", "Execute RT"});
    m_transferTypeCombo->setStyleSheet(
        "QComboBox {"
        "  background: white; border: 1px solid #E5E7EB; border-radius: 6px;"
        "  padding: 6px 12px; font-size: 13px; min-width: 120px;"
        "}"
        "QComboBox::drop-down { border: none; }"
        "QComboBox:hover { border-color: #3B82F6; }"
    );
    selRow->addWidget(m_transferTypeCombo);
    selRow->addStretch();

    cardLayout->addLayout(selRow);
    
    // Month range selector (graphical)
    QLabel* monthRangeLabel = new QLabel("Select Month Range:");
    monthRangeLabel->setStyleSheet("font-weight: 600; color: #374151; margin-top: 10px;");
    cardLayout->addWidget(monthRangeLabel);
    
    // Create month checkboxes in a grid
    QGridLayout* monthGrid = new QGridLayout();
    monthGrid->setSpacing(12);  // Increased from 8 to 12
    
    QStringList months = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    
    m_monthCheckboxes.clear();
    for (int i = 0; i < 12; i++) {
        QCheckBox* cb = new QCheckBox(months[i]);
        cb->setStyleSheet(
            "QCheckBox {"
            "  font-size: 14px; color: #374151; padding: 6px;"
            "  font-weight: 600;"
            "}"
            "QCheckBox::indicator {"
            "  width: 20px; height: 20px; border-radius: 4px;"
            "  border: 2px solid #D1D5DB;"
            "}"
            "QCheckBox::indicator:checked {"
            "  background: #3B82F6; border-color: #3B82F6;"
            "}"
            "QCheckBox::indicator:hover {"
            "  border-color: #3B82F6;"
            "}"
        );
        monthGrid->addWidget(cb, i / 6, i % 6);
        m_monthCheckboxes.append(cb);
    }
    cardLayout->addLayout(monthGrid);
    
    // Quick select buttons
    QHBoxLayout* quickSelectLayout = new QHBoxLayout();
    quickSelectLayout->setSpacing(8);
    
    QPushButton* btnQ1 = new QPushButton("Q1");
    QPushButton* btnQ2 = new QPushButton("Q2");
    QPushButton* btnQ3 = new QPushButton("Q3");
    QPushButton* btnQ4 = new QPushButton("Q4");
    QPushButton* btnAll = new QPushButton("All");
    QPushButton* btnNone = new QPushButton("None");
    
    QString quickBtnStyle = 
        "QPushButton {"
        "  background: white; color: #374151; padding: 4px 12px;"
        "  border: 1px solid #D1D5DB; border-radius: 4px; font-size: 11px; font-weight: 600;"
        "}"
        "QPushButton:hover { background: #F3F4F6; border-color: #3B82F6; }"
        "QPushButton:pressed { background: #E5E7EB; }";
    
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
    
    quickSelectLayout->addWidget(new QLabel("Quick:"));
    quickSelectLayout->addWidget(btnQ1);
    quickSelectLayout->addWidget(btnQ2);
    quickSelectLayout->addWidget(btnQ3);
    quickSelectLayout->addWidget(btnQ4);
    quickSelectLayout->addWidget(btnAll);
    quickSelectLayout->addWidget(btnNone);
    quickSelectLayout->addStretch();
    
    cardLayout->addLayout(quickSelectLayout);
    
    // Add button
    QHBoxLayout* addBtnLayout = new QHBoxLayout();
    addBtnLayout->addStretch();

    m_addBtn = new QPushButton("+ Add Selected Months");
    m_addBtn->setStyleSheet(
        "QPushButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #3B82F6, stop:1 #2563EB);"
        "  color: white; font-weight: 600;"
        "  padding: 8px 20px; border-radius: 6px; font-size: 13px; border: none;"
        "}"
        "QPushButton:hover {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #2563EB, stop:1 #1D4ED8);"
        "}"
        "QPushButton:pressed { background: #1E40AF; }"
    );
    addBtnLayout->addWidget(m_addBtn);
    cardLayout->addLayout(addBtnLayout);
    rightLayout->addWidget(assignCard);

    // ── Assignments Table ──
    QFrame* tableCard = new QFrame();
    tableCard->setStyleSheet(
        "QFrame {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #FAFBFC, stop:1 #F5F7FA);"
        "  border-radius: 12px;"
        "  padding: 16px;"
        "}"
    );
    QVBoxLayout* tableCardLayout = new QVBoxLayout(tableCard);

    QHBoxLayout* tableHeader = new QHBoxLayout();
    QLabel* tableTitle = new QLabel("Assigned Periods");
    tableTitle->setStyleSheet("font-weight: 600; font-size: 14px; color: #374151;");
    tableHeader->addWidget(tableTitle);
    tableHeader->addStretch();

    // Move Up / Move Down buttons
    m_moveUpBtn = new QPushButton("▲ Up");
    m_moveDownBtn = new QPushButton("▼ Down");
    QString moveBtnStyle =
        "QPushButton {"
        "  background: white; color: #374151; font-weight: 600;"
        "  padding: 6px 12px; border-radius: 6px; font-size: 12px;"
        "  border: 1px solid #D1D5DB;"
        "}"
        "QPushButton:hover { background: #F3F4F6; border-color: #3B82F6; }"
        "QPushButton:pressed { background: #E5E7EB; }"
        "QPushButton:disabled { background: #F3F4F6; color: #9CA3AF; border-color: #E5E7EB; }";
    m_moveUpBtn->setStyleSheet(moveBtnStyle);
    m_moveDownBtn->setStyleSheet(moveBtnStyle);
    m_moveUpBtn->setEnabled(false);
    m_moveDownBtn->setEnabled(false);
    m_moveUpBtn->setToolTip("Move selected row up (RT rows move as a block)");
    m_moveDownBtn->setToolTip("Move selected row down (RT rows move as a block)");
    tableHeader->addWidget(m_moveUpBtn);
    tableHeader->addWidget(m_moveDownBtn);

    m_removeBtn = new QPushButton("✗ Remove");
    m_removeBtn->setStyleSheet(
        "QPushButton {"
        "  background: #FEF2F2; color: #DC2626; font-weight: 600;"
        "  padding: 6px 14px; border-radius: 6px; font-size: 12px; border: none;"
        "}"
        "QPushButton:hover { background: #FEE2E2; }"
        "QPushButton:pressed { background: #FECACA; }"
        "QPushButton:disabled { background: #F3F4F6; color: #9CA3AF; }"
    );
    m_removeBtn->setEnabled(false);
    tableHeader->addWidget(m_removeBtn);

    m_clearBtn = new QPushButton("✗ Clear All");
    m_clearBtn->setStyleSheet(
        "QPushButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #F9FAFB, stop:1 #F3F4F6);"
        "  color: #374151; font-weight: 600;"
        "  padding: 6px 14px; border-radius: 6px; font-size: 12px; border: none;"
        "}"
        "QPushButton:hover { background: #E5E7EB; }"
        "QPushButton:pressed { background: #D1D5DB; }"
    );
    tableHeader->addWidget(m_clearBtn);

    tableCardLayout->addLayout(tableHeader);

    m_table = new QTableWidget();
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels(
        {"Year", "Month", "Transfer Type", "Status"});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->setMinimumHeight(250);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setStyleSheet(
        "QTableWidget {"
        "  background: white; gridline-color: #E5E7EB;"
        "  border-radius: 8px; font-size: 13px;"
        "}"
        "QTableWidget::item { padding: 8px; }"
        "QTableWidget::item:alternate { background: #F9FAFB; }"
        "QHeaderView::section {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #F9FAFB, stop:1 #F3F4F6);"
        "  color: #374151; padding: 10px; font-weight: 600;"
        "  border: none; border-bottom: 2px solid #E5E7EB;"
        "}"
    );
    tableCardLayout->addWidget(m_table);

    rightLayout->addWidget(tableCard);

    // ── Execution Options ──
    QFrame* execCard = new QFrame();
    execCard->setStyleSheet(
        "QFrame {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #FAFBFC, stop:1 #F5F7FA);"
        "  border-radius: 12px;"
        "  padding: 16px;"
        "}"
    );
    QVBoxLayout* execLayout = new QVBoxLayout(execCard);

    QLabel* execTitle = new QLabel("Execution Options");
    execTitle->setStyleSheet("font-weight: 600; font-size: 14px; color: #374151;");
    execLayout->addWidget(execTitle);

    QHBoxLayout* orderRow = new QHBoxLayout();
    QLabel* orderLabel = new QLabel("Execution Order:");
    orderLabel->setStyleSheet("font-weight: 600; color: #374151;");
    orderRow->addWidget(orderLabel);
    orderRow->addSpacing(10);

    m_executeAllFirstRadio = new QRadioButton("Execute All first, then RT");
    m_executeAllFirstRadio->setChecked(true);
    m_executeAllFirstRadio->setStyleSheet("font-size: 13px; color: #374151;");
    orderRow->addWidget(m_executeAllFirstRadio);

    m_executeRTFirstRadio = new QRadioButton("Execute RT first, then Execute All");
    m_executeRTFirstRadio->setStyleSheet("font-size: 13px; color: #374151;");
    orderRow->addWidget(m_executeRTFirstRadio);
    orderRow->addStretch();

    execLayout->addLayout(orderRow);

    // Summary
    m_summaryLabel = new QLabel("No periods assigned yet.");
    m_summaryLabel->setStyleSheet(
        "color: #6B7280; font-style: italic; padding: 8px; font-size: 13px;");
    execLayout->addWidget(m_summaryLabel);

    // Phase status label (shows during execution)
    m_phaseStatusLabel = new QLabel("");
    m_phaseStatusLabel->setStyleSheet(
        "color: #3B82F6; font-weight: 600; padding: 8px; font-size: 14px; "
        "background: #EFF6FF; border-radius: 6px; border-left: 4px solid #3B82F6;");
    m_phaseStatusLabel->setVisible(false);
    execLayout->addWidget(m_phaseStatusLabel);

    // Progress
    m_progressBar = new QProgressBar();
    m_progressBar->setMaximumHeight(6);
    m_progressBar->setTextVisible(false);
    m_progressBar->setVisible(false);
    execLayout->addWidget(m_progressBar);

    rightLayout->addWidget(execCard);

    // ── Execute + Reset Buttons ──
    QHBoxLayout* bottomBtnLayout = new QHBoxLayout();
    
    m_executeBtn = new QPushButton("▶ Execute Hybrid Transfer");
    m_executeBtn->setMaximumWidth(350);
    m_executeBtn->setEnabled(false);
    m_executeBtn->setStyleSheet(
        "QPushButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #7C3AED, stop:1 #6D28D9);"
        "  color: white; font-weight: 600;"
        "  padding: 12px 24px; border-radius: 8px; font-size: 14px; border: none;"
        "}"
        "QPushButton:hover {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #6D28D9, stop:1 #5B21B6);"
        "}"
        "QPushButton:pressed { background: #5B21B6; }"
        "QPushButton:disabled { background: #E5E7EB; color: #9CA3AF; }"
    );
    bottomBtnLayout->addWidget(m_executeBtn);

    QPushButton* resetAllBtn = new QPushButton("↺ Reset All");
    resetAllBtn->setMaximumWidth(200);
    resetAllBtn->setStyleSheet(
        "QPushButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #FEF2F2, stop:1 #FEE2E2);"
        "  color: #DC2626; font-weight: 600;"
        "  padding: 12px 24px; border-radius: 8px; font-size: 14px; border: none;"
        "}"
        "QPushButton:hover {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #FEE2E2, stop:1 #FECACA);"
        "}"
        "QPushButton:pressed { background: #FECACA; }"
    );
    connect(resetAllBtn, &QPushButton::clicked, this, &HybridTransferTab::onResetAll);
    bottomBtnLayout->addWidget(resetAllBtn);
    bottomBtnLayout->addStretch();

    rightLayout->addLayout(bottomBtnLayout);
    rightLayout->addStretch();
    
    // Add right content to splitter
    m_splitter->addWidget(rightContent);
    
    // Set initial splitter sizes (sidebar 40%, content 60%)
    m_splitter->setSizes({400, 600});

    // ── Connections ──
    connect(m_addBtn, &QPushButton::clicked,
            this, &HybridTransferTab::onAddPeriod);
    connect(m_removeBtn, &QPushButton::clicked,
            this, &HybridTransferTab::onRemovePeriod);
    connect(m_clearBtn, &QPushButton::clicked,
            this, &HybridTransferTab::onClearAll);
    connect(m_executeBtn, &QPushButton::clicked,
            this, &HybridTransferTab::onExecute);
    connect(m_moveUpBtn, &QPushButton::clicked,
            this, &HybridTransferTab::onMoveUp);
    connect(m_moveDownBtn, &QPushButton::clicked,
            this, &HybridTransferTab::onMoveDown);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
        m_removeBtn->setEnabled(!m_table->selectedItems().isEmpty());
        updateMoveButtons();
    });
}

void HybridTransferTab::onAddPeriod()
{
    QString year = m_yearCombo->currentText();
    QString type = m_transferTypeCombo->currentText();
    
    // Get selected month indices
    QVector<int> selectedIndices;
    for (int i = 0; i < m_monthCheckboxes.size(); i++) {
        if (m_monthCheckboxes[i]->isChecked()) {
            selectedIndices.append(i);
        }
    }
    
    if (selectedIndices.isEmpty()) {
        QMessageBox::warning(this, "No Months Selected",
            "Please select at least one month.");
        return;
    }
    
    // Enforce consecutive months and minimum 2 months for Execute RT
    if (type == "Execute RT") {
        // Minimum 2 consecutive months required
        if (selectedIndices.size() < 2) {
            QMessageBox::warning(this, "Insufficient Months",
                "Execute RT requires at least 2 consecutive months.\n\n"
                "Please select at least 2 consecutive months (e.g., Jan-Feb or Feb-Mar-Apr).");
            return;
        }
        
        // Check if months are consecutive
        std::sort(selectedIndices.begin(), selectedIndices.end());
        bool consecutive = true;
        for (int i = 1; i < selectedIndices.size(); i++) {
            if (selectedIndices[i] != selectedIndices[i-1] + 1) {
                consecutive = false;
                break;
            }
        }
        
        if (!consecutive) {
            QMessageBox::warning(this, "Non-Consecutive Months",
                "Execute RT requires consecutive months (e.g., Jan-Feb-Mar).\n\n"
                "Selected months must be next to each other in sequence.\n"
                "Please adjust your selection.");
            return;
        }
    }
    
    // Convert indices to month names
    QStringList fullMonthNames = {"January", "February", "March", "April", "May", "June",
                                  "July", "August", "September", "October", "November", "December"};
    QStringList selectedMonths;
    QVector<QPair<QString, int>> selectedPeriods;
    
    for (int idx : selectedIndices) {
        QString month = fullMonthNames[idx];
        selectedMonths.append(month);
        selectedPeriods.append({month, year.toInt()});
    }
    
    // STEP 1: Load mapping cards into the sidebar — always stack, never clear
    // Both Execute All and Execute RT append cards for new months only
    for (const auto& period : selectedPeriods) {
        // Skip duplicates — don't add cards for months already assigned
        QString key = QString("%1_%2").arg(period.first).arg(period.second);
        if (!m_assignments.contains(key)) {
            m_mainWindow->populateFillAllMappingCards(m_mappingController,
                                                       period.first, period.second);
        }
    }
    
    // STEP 2: Add each selected month to assignments
    int addedCount = 0;
    int skippedCount = 0;
    QStringList skippedMonths;
    
    for (const QString& month : selectedMonths) {
        QString key = QString("%1_%2").arg(month, year);

        // Check for duplicate
        if (m_assignments.contains(key)) {
            skippedCount++;
            skippedMonths.append(month);
            continue;
        }

        m_assignments[key] = (type == "Execute All") ? "execute_all" : "execute_rt";
        m_orderedKeys.append(key);   // maintain user-defined execution order
        addedCount++;
    }
    
    if (addedCount > 0) {
        populateTable();
        updateSummary();
        updateExecuteButton();
        
        // Clear selections after adding
        clearAllMonths();
        
        // Show success message
        QString message;
        if (skippedCount == 0) {
            message = QString("✔ Added %1 month(s) to %2")
                .arg(addedCount)
                .arg(type);
            QMessageBox::information(this, "Success", message);
        } else {
            message = QString("Added %1 month(s), skipped %2 (already assigned):\n%3")
                .arg(addedCount)
                .arg(skippedCount)
                .arg(skippedMonths.join(", "));
            QMessageBox::information(this, "Partially Added", message);
        }
    } else {
        QString message = QString("All selected months already assigned:\n%1")
            .arg(skippedMonths.join(", "));
        QMessageBox::information(this, "Already Assigned", message);
    }
}

void HybridTransferTab::selectQuarter(int quarter)
{
    // Q1: 0,1,2  Q2: 3,4,5  Q3: 6,7,8  Q4: 9,10,11
    int start = (quarter - 1) * 3;
    int end = start + 3;
    
    for (int i = 0; i < m_monthCheckboxes.size(); i++) {
        m_monthCheckboxes[i]->setChecked(i >= start && i < end);
    }
}

void HybridTransferTab::selectAllMonths()
{
    for (QCheckBox* cb : m_monthCheckboxes) {
        cb->setChecked(true);
    }
}

void HybridTransferTab::clearAllMonths()
{
    for (QCheckBox* cb : m_monthCheckboxes) {
        cb->setChecked(false);
    }
}

void HybridTransferTab::onRemovePeriod()
{
    QList<QTableWidgetItem*> selected = m_table->selectedItems();
    QSet<int> rowsToRemove;
    for (QTableWidgetItem* item : selected) {
        rowsToRemove.insert(item->row());
    }

    // Build keys to remove from the ordered list
    QStringList keysToRemove;
    for (int row : rowsToRemove) {
        if (row < m_orderedKeys.size()) {
            keysToRemove.append(m_orderedKeys[row]);
        }
    }

    for (const QString& key : keysToRemove) {
        m_assignments.remove(key);
        m_orderedKeys.removeAll(key);
    }

    populateTable();
    updateSummary();
    updateExecuteButton();
}

void HybridTransferTab::onClearAll()
{
    m_assignments.clear();
    m_orderedKeys.clear();
    populateTable();
    updateSummary();
    updateExecuteButton();
}

void HybridTransferTab::onResetAll()
{
    // Confirm with user before resetting everything
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "Reset Hybrid Mode",
        "This will reset everything to the initial state:\n\n"
        "• Clear all assigned periods\n"
        "• Remove all mapping cards\n"
        "• Reset month selections\n"
        "• Reset execution options\n\n"
        "Are you sure?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (reply != QMessageBox::Yes)
        return;

    // 1. Clear all assignments and table
    m_assignments.clear();
    m_orderedKeys.clear();
    populateTable();

    // 2. Clear all mapping cards from the sidebar
    if (m_mappingController) {
        m_mappingController->clearAllMappings();
    }

    // 3. Reset month checkboxes
    clearAllMonths();

    // 4. Reset year to current
    m_yearCombo->setCurrentText(QString::number(QDate::currentDate().year()));

    // 5. Reset transfer type to first option
    m_transferTypeCombo->setCurrentIndex(0);

    // 6. Reset execution order to default
    m_executeAllFirstRadio->setChecked(true);

    // 7. Reset progress/status UI
    m_progressBar->setVisible(false);
    m_progressBar->setValue(0);
    m_phaseStatusLabel->setVisible(false);
    m_phaseStatusLabel->setText("");

    // 8. Update summary and execute button
    updateSummary();
    updateExecuteButton();
}

void HybridTransferTab::onExecute()
{
    // Build config from assignments
    m_config = HybridTransferConfig();
    m_config.executeAllFirst = m_executeAllFirstRadio->isChecked();

    // Collect which months/years are Execute All vs Execute RT
    // Use m_orderedKeys so the user-defined order is respected
    QSet<QString> executeAllKeys;
    QVector<QString> fallbackOrder;
    if (m_orderedKeys.isEmpty()) {
        for (const QString& k : m_assignments.keys())
            fallbackOrder.append(k);
    }
    const QVector<QString>& ordered = m_orderedKeys.isEmpty() ? fallbackOrder : m_orderedKeys;

    for (const QString& key : ordered) {
        if (!m_assignments.contains(key)) continue;
        QString type = m_assignments[key];

        int underscorePos = key.lastIndexOf('_');
        QString month = key.left(underscorePos);
        int year = key.mid(underscorePos + 1).toInt();

        QPair<QString, int> period = {month, year};

        if (type == "execute_all") {
            m_config.executeAllPeriods.append(period);
            executeAllKeys.insert(key);
        } else {
            m_config.executeRTPeriods.append(period);
        }
    }

    if (m_config.isEmpty()) {
        QMessageBox::information(this, "Nothing to Execute",
            "No periods assigned. Add periods first.");
        return;
    }

    // Option B: collect mapping items from the sidebar for Execute All months.
    // This lets HybridWorker bypass the async load+delay and transfer directly
    // using the cards the user has already loaded and checked.
    if (m_mappingController && !executeAllKeys.isEmpty()) {
        QVector<MappingItem> allItems = m_mappingController->items();
        for (int i = 0; i < allItems.size(); ++i) {
            MappingRow* row = m_mappingController->rowAt(i);
            if (!row || !row->isChecked())
                continue;
            const MappingItem& item = allItems[i];
            // Only include items that belong to an Execute All period
            QString key = QString("%1_%2").arg(item.entry.month).arg(item.year);
            if (key.isEmpty()) {
                // fallback: use item.month/year directly
                key = QString("%1_%2").arg(item.month).arg(item.year);
            }
            if (executeAllKeys.contains(key))
                m_config.executeAllItems.append(item);
        }
        qInfo() << "[HybridTab] Collected" << m_config.executeAllItems.size()
                << "checked mapping items for Execute All";
    }

    emit executeRequested(m_config);
}

void HybridTransferTab::populateTable()
{
    // Remember selected row(s) so we can restore selection after repopulate
    int selectedRow = -1;
    if (!m_table->selectedItems().isEmpty())
        selectedRow = m_table->selectedItems().first()->row();

    m_table->setRowCount(0);

    // Use m_orderedKeys to preserve user-defined drag/move order.
    // Fall back to chronological sort if m_orderedKeys is somehow out of sync.
    QVector<QString> displayOrder = m_orderedKeys;
    if (displayOrder.isEmpty() && !m_assignments.isEmpty()) {
        QStringList keys = m_assignments.keys();
        std::sort(keys.begin(), keys.end(),
            [](const QString& a, const QString& b) {
                static const QStringList months = {
                    "January", "February", "March", "April", "May", "June",
                    "July", "August", "September", "October", "November", "December"
                };
                int underA = a.lastIndexOf('_');
                int underB = b.lastIndexOf('_');
                int yearA = a.mid(underA + 1).toInt();
                int yearB = b.mid(underB + 1).toInt();
                if (yearA != yearB) return yearA < yearB;
                return months.indexOf(a.left(underA)) < months.indexOf(b.left(underB));
            });
        for (const QString& k : keys)
            displayOrder.append(k);
        m_orderedKeys = displayOrder; // sync
    }

    for (const QString& key : displayOrder) {
        if (!m_assignments.contains(key)) continue;
        int underscorePos = key.lastIndexOf('_');
        QString month = key.left(underscorePos);
        QString year  = key.mid(underscorePos + 1);
        QString type  = m_assignments[key];
        bool isRT     = (type == "execute_rt");

        int row = m_table->rowCount();
        m_table->insertRow(row);

        // Subtle amber left-border tint for RT rows to hint they're grouped
        QColor rowBg = isRT ? QColor("#FFFBEB") : QColor(Qt::transparent);

        auto makeItem = [&](const QString& text) {
            auto* item = new QTableWidgetItem(text);
            if (isRT) item->setBackground(rowBg);
            return item;
        };

        m_table->setItem(row, 0, makeItem(year));
        m_table->setItem(row, 1, makeItem(month));

        auto* typeItem = new QTableWidgetItem(isRT ? "Execute RT" : "Execute All");
        typeItem->setForeground(QColor(isRT ? "#D97706" : "#059669"));
        typeItem->setData(Qt::FontRole, QFont(typeItem->font().family(), -1, QFont::Bold));
        if (isRT) typeItem->setBackground(rowBg);
        m_table->setItem(row, 2, typeItem);

        auto* statusItem = makeItem("Pending");
        m_table->setItem(row, 3, statusItem);
    }

    // Restore selection
    if (selectedRow >= 0 && selectedRow < m_table->rowCount())
        m_table->selectRow(selectedRow);

    updateMoveButtons();
}

void HybridTransferTab::updateSummary()
{
    int allCount = 0;
    int rtCount = 0;
    for (const QString& type : m_assignments) {
        if (type == "execute_all") allCount++;
        else rtCount++;
    }

    if (m_assignments.isEmpty()) {
        m_summaryLabel->setText("No periods assigned yet.");
    } else {
        QString order = m_executeAllFirstRadio->isChecked()
                            ? "Execute All → Execute RT"
                            : "Execute RT → Execute All";
        m_summaryLabel->setText(
            QString("<b>%1</b> period(s) assigned: "
                    "<span style='color:#059669'>%2 Execute All</span>, "
                    "<span style='color:#D97706'>%3 Execute RT</span> "
                    "| Order: %4")
                .arg(m_assignments.size())
                .arg(allCount)
                .arg(rtCount)
                .arg(order));
    }
}

void HybridTransferTab::updateExecuteButton()
{
    m_executeBtn->setEnabled(!m_assignments.isEmpty());
}

void HybridTransferTab::onPhaseStarted(const QString& phaseName)
{
    m_phaseStatusLabel->setText(QString("⏳ Running: %1...").arg(phaseName));
    m_phaseStatusLabel->setStyleSheet(
        "color: #3B82F6; font-weight: 600; padding: 8px; font-size: 14px; "
        "background: #EFF6FF; border-radius: 6px; border-left: 4px solid #3B82F6;");
    m_phaseStatusLabel->setVisible(true);
    m_executeBtn->setEnabled(false);
}

void HybridTransferTab::onPhaseFinished(const QString& phaseName, bool success)
{
    QString icon = success ? "✓" : "✗";
    QString color = success ? "#059669" : "#DC2626";
    QString bgColor = success ? "#ECFDF5" : "#FEF2F2";
    
    m_phaseStatusLabel->setText(QString("%1 %2 %3")
        .arg(icon, phaseName, success ? "completed" : "failed"));
    m_phaseStatusLabel->setStyleSheet(
        QString("color: %1; font-weight: 600; padding: 8px; font-size: 14px; "
                "background: %2; border-radius: 6px; border-left: 4px solid %1;")
            .arg(color, bgColor));
}

void HybridTransferTab::onProgressUpdate(int percent, const QString& message)
{
    m_progressBar->setValue(percent);
    m_progressBar->setVisible(true);
    
    // Update phase status with progress message
    if (message.contains("Phase 1")) {
        m_phaseStatusLabel->setText(QString("⏳ Phase 1: %1").arg(message.split(":").last().trimmed()));
    } else if (message.contains("Phase 2")) {
        m_phaseStatusLabel->setText(QString("⏳ Phase 2: %1").arg(message.split(":").last().trimmed()));
    }
}

void HybridTransferTab::onAllFinished(bool success, const QString& summary)
{
    m_progressBar->setVisible(false);
    m_executeBtn->setEnabled(true);
    
    QString icon = success ? "✓" : "✗";
    QString color = success ? "#059669" : "#DC2626";
    QString bgColor = success ? "#ECFDF5" : "#FEF2F2";
    
    m_phaseStatusLabel->setText(QString("%1 Hybrid Transfer Complete").arg(icon));
    m_phaseStatusLabel->setStyleSheet(
        QString("color: %1; font-weight: 600; padding: 8px; font-size: 14px; "
                "background: %2; border-radius: 6px; border-left: 4px solid %1;")
            .arg(color, bgColor));
    
    // Show detailed summary in a message box
    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Hybrid Transfer Complete");
    msgBox.setText(success ? "Transfer completed successfully!" : "Transfer completed with errors");
    msgBox.setDetailedText(summary);
    msgBox.setIcon(success ? QMessageBox::Information : QMessageBox::Warning);
    msgBox.exec();
    
    // Hide status after 5 seconds
    QTimer::singleShot(5000, this, [this]() {
        m_phaseStatusLabel->setVisible(false);
    });
}

void HybridTransferTab::setupMappingsSidebar()
{
    // Create sidebar for mappings (left panel) - similar to MainWindow
    m_mappingsSidebar = new QFrame();
    m_mappingsSidebar->setStyleSheet(
        "QFrame {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #FAFBFC, stop:1 #F5F7FA);"
        "  border-right: none;"
        "  border-top-right-radius: 12px;"
        "  border-bottom-right-radius: 12px;"
        "}"
    );
    m_mappingsSidebar->setMinimumWidth(500);
    m_mappingsSidebar->setMaximumWidth(1200);
    
    QVBoxLayout* sidebarLayout = new QVBoxLayout(m_mappingsSidebar);
    sidebarLayout->setContentsMargins(20, 20, 20, 20);
    sidebarLayout->setSpacing(16);
    
    // Header with title and Select All/Deselect All buttons
    QHBoxLayout* sidebarHeaderLayout = new QHBoxLayout();
    QLabel* title = new QLabel("Row Mappings");
    title->setStyleSheet(
        "font-weight: 700; font-size: 16px; color: #1A1F36; letter-spacing: -0.3px;"
    );
    sidebarHeaderLayout->addWidget(title);
    sidebarHeaderLayout->addStretch();

    QString sidebarBtnStyle =
        "QPushButton {"
        "  background: white; color: #374151; padding: 4px 10px;"
        "  border: 1px solid #D1D5DB; border-radius: 4px; font-size: 11px; font-weight: 600;"
        "}"
        "QPushButton:hover { background: #F3F4F6; border-color: #3B82F6; }"
        "QPushButton:pressed { background: #E5E7EB; }";

    QPushButton* selectAllCardsBtn = new QPushButton("Select All");
    selectAllCardsBtn->setStyleSheet(sidebarBtnStyle);
    connect(selectAllCardsBtn, &QPushButton::clicked, this, [this]() {
        if (m_mappingController) m_mappingController->setAllChecked(true);
    });
    sidebarHeaderLayout->addWidget(selectAllCardsBtn);

    QPushButton* deselectAllCardsBtn = new QPushButton("Deselect All");
    deselectAllCardsBtn->setStyleSheet(sidebarBtnStyle);
    connect(deselectAllCardsBtn, &QPushButton::clicked, this, [this]() {
        if (m_mappingController) m_mappingController->setAllChecked(false);
    });
    sidebarHeaderLayout->addWidget(deselectAllCardsBtn);

    sidebarLayout->addLayout(sidebarHeaderLayout);

    // Separator line
    QFrame* separator = new QFrame();
    separator->setFrameShape(QFrame::HLine);
    separator->setStyleSheet(
        "background: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
        "  stop:0 rgba(0,0,0,0), stop:0.5 rgba(0,0,0,0.08), stop:1 rgba(0,0,0,0));"
        "border: none; height: 1px;"
    );
    sidebarLayout->addWidget(separator);

    // Mappings scroll area
    m_mappingsContainer = new QWidget();
    m_mappingsContainer->setStyleSheet("background: transparent;");
    m_mappingsLayout = new QVBoxLayout(m_mappingsContainer);
    m_mappingsLayout->setSpacing(12);
    m_mappingsLayout->setContentsMargins(0, 0, 0, 0);

    QScrollArea* scroll = new QScrollArea();
    scroll->setWidget(m_mappingsContainer);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(
        "QScrollArea { background: transparent; border: none; }"
        "QScrollBar:vertical {"
        "  background: transparent; width: 10px; border-radius: 5px;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: rgba(0, 0, 0, 0.15); border-radius: 5px; min-height: 30px;"
        "}"
        "QScrollBar::handle:vertical:hover { background: rgba(0, 0, 0, 0.25); }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
    );
    sidebarLayout->addWidget(scroll, 1);

    // No mappings label
    m_noMappingsLabel = new QLabel("No mappings loaded.\nSelect months and click 'Load Mappings'.");
    m_noMappingsLabel->setStyleSheet(
        "color: #8B92A7; font-style: italic; font-size: 12px;"
        "padding: 40px 20px; background: rgba(255, 255, 255, 0.5);"
        "border-radius: 12px; border: 2px dashed #D1D5DB;"
    );
    m_noMappingsLabel->setAlignment(Qt::AlignCenter);
    sidebarLayout->addWidget(m_noMappingsLabel);
    
    // Initialize mapping controller
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
    
    m_splitter->addWidget(m_mappingsSidebar);
}

// ─────────────────────────────────────────────────────────────────────────────
// rtBlockFor() — returns the contiguous run of RT-row indices that contains
// the given row index. For Execute All rows returns just { rowIndex }.
// ─────────────────────────────────────────────────────────────────────────────
QVector<int> HybridTransferTab::rtBlockFor(int rowIndex) const
{
    if (rowIndex < 0 || rowIndex >= m_orderedKeys.size())
        return {};

    const QString& key = m_orderedKeys[rowIndex];
    if (!m_assignments.contains(key))
        return { rowIndex };

    // Execute All rows are independent — block is just itself
    if (m_assignments[key] == "execute_all")
        return { rowIndex };

    // For RT rows, find the contiguous block they belong to.
    // Walk backwards to the first RT in this run, then forward to the last.
    int blockStart = rowIndex;
    while (blockStart > 0
           && m_assignments.value(m_orderedKeys[blockStart - 1]) == "execute_rt")
        --blockStart;

    int blockEnd = rowIndex;
    while (blockEnd + 1 < m_orderedKeys.size()
           && m_assignments.value(m_orderedKeys[blockEnd + 1]) == "execute_rt")
        ++blockEnd;

    QVector<int> block;
    for (int i = blockStart; i <= blockEnd; ++i)
        block.append(i);
    return block;
}

// ─────────────────────────────────────────────────────────────────────────────
// updateMoveButtons() — enable/disable ▲ Up / ▼ Down based on selection
// ─────────────────────────────────────────────────────────────────────────────
void HybridTransferTab::updateMoveButtons()
{
    if (m_table->selectedItems().isEmpty()) {
        m_moveUpBtn->setEnabled(false);
        m_moveDownBtn->setEnabled(false);
        return;
    }

    int row = m_table->selectedItems().first()->row();
    QVector<int> block = rtBlockFor(row);

    int blockTop    = block.isEmpty() ? row : block.first();
    int blockBottom = block.isEmpty() ? row : block.last();
    int total       = m_orderedKeys.size();

    m_moveUpBtn->setEnabled(blockTop > 0);
    m_moveDownBtn->setEnabled(blockBottom < total - 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// onMoveUp() — move the selected row (or RT block) one position up
// ─────────────────────────────────────────────────────────────────────────────
void HybridTransferTab::onMoveUp()
{
    if (m_table->selectedItems().isEmpty()) return;

    int row = m_table->selectedItems().first()->row();
    QVector<int> block = rtBlockFor(row);
    if (block.isEmpty()) return;

    int blockTop = block.first();
    if (blockTop <= 0) return;  // already at top

    // Move the whole block one position up by rotating the slice
    // [blockTop-1 .. blockTop .. blockBottom] → [blockTop .. blockBottom .. blockTop-1]
    // i.e. swap blockTop-1 with blockTop, then blockTop with blockTop+1, etc.
    int blockBottom = block.last();
    // Equivalent: remove element at blockTop-1 and insert after blockBottom
    QString displaced = m_orderedKeys[blockTop - 1];
    m_orderedKeys.removeAt(blockTop - 1);
    m_orderedKeys.insert(blockBottom, displaced);

    // Restore selection to the moved block (now one row higher)
    int newRow = row - 1;
    populateTable();
    m_table->selectRow(newRow);
}

// ─────────────────────────────────────────────────────────────────────────────
// onMoveDown() — move the selected row (or RT block) one position down
// ─────────────────────────────────────────────────────────────────────────────
void HybridTransferTab::onMoveDown()
{
    if (m_table->selectedItems().isEmpty()) return;

    int row = m_table->selectedItems().first()->row();
    QVector<int> block = rtBlockFor(row);
    if (block.isEmpty()) return;

    int blockBottom = block.last();
    if (blockBottom >= m_orderedKeys.size() - 1) return;  // already at bottom

    int blockTop = block.first();
    // Remove element at blockBottom+1 and insert before blockTop
    QString displaced = m_orderedKeys[blockBottom + 1];
    m_orderedKeys.removeAt(blockBottom + 1);
    m_orderedKeys.insert(blockTop, displaced);

    // Restore selection to the moved block (now one row lower)
    int newRow = row + 1;
    populateTable();
    m_table->selectRow(newRow);
}
