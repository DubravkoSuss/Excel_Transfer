#include "individualtransferpanel.h"
#include "../../ui/sheetcellselectordialog.h"
#include "../../services/excelhandler.h"
#include <QMessageBox>
#include <QHeaderView>
#include <QFileDialog>
#include <QDebug>

IndividualTransferPanel::IndividualTransferPanel(QWidget *parent)
    : QWidget(parent)
{
    setupUI();
    resetToDefaults();
}

IndividualTransferPanel::~IndividualTransferPanel()
{
}

void IndividualTransferPanel::resetToDefaults()
{
    m_currentConfig = {
        "", "", "Sheet1", "A", 1,
        "Sheet1", "B", 1, 1, false
    };
    m_sourceFileEdit->clear();
    m_destFileEdit->clear();
    m_mappingData.clear();
    refreshMappingTable();
    updateTransferButtonState();
}

void IndividualTransferPanel::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    // Title
    QLabel* titleLabel = new QLabel("Individual Cell Transfer");
    titleLabel->setStyleSheet(
        "font-size: 18px; font-weight: 700; color: #1F2937; "
        "letter-spacing: -0.3px; margin-bottom: 4px;"
    );
    mainLayout->addWidget(titleLabel);

    QLabel* descLabel = new QLabel(
        "Select source and destination files, then use graphical selection to map cells."
    );
    descLabel->setStyleSheet("color: #6B7280; font-size: 13px; margin-bottom: 8px;");
    mainLayout->addWidget(descLabel);

    // Source File Group
    QGroupBox* sourceGroup = new QGroupBox("SOURCE FILE");
    sourceGroup->setStyleSheet(
        "QGroupBox {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #FAFBFC, stop:1 #F5F7FA);"
        "  border-radius: 12px; padding: 16px; padding-top: 28px;"
        "  font-weight: 600; color: #374151;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin; left: 12px; padding: 0 6px;"
        "  color: #374151; font-size: 12px; letter-spacing: 0.5px;"
        "}"
    );
    QHBoxLayout* srcLayout = new QHBoxLayout(sourceGroup);
    
    m_sourceFileEdit = new QLineEdit();
    m_sourceFileEdit->setPlaceholderText("No file selected");
    m_sourceFileEdit->setReadOnly(true);
    m_sourceFileEdit->setStyleSheet(
        "QLineEdit {"
        "  padding: 10px; background: white;"
        "  border: 1px solid #E5E7EB; border-radius: 6px; font-size: 13px;"
        "}"
    );
    srcLayout->addWidget(m_sourceFileEdit, 1);
    
    QPushButton* btnBrowseSrc = new QPushButton("⚙ Browse");
    btnBrowseSrc->setStyleSheet(
        "QPushButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #3B82F6, stop:1 #2563EB);"
        "  color: white; padding: 10px 20px;"
        "  border-radius: 6px; font-weight: 600; border: none;"
        "}"
        "QPushButton:hover {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #2563EB, stop:1 #1D4ED8);"
        "}"
        "QPushButton:pressed { background: #1E40AF; }"
    );
    btnBrowseSrc->setFixedHeight(42);
    connect(btnBrowseSrc, &QPushButton::clicked, this, &IndividualTransferPanel::onBrowseSourceClicked);
    srcLayout->addWidget(btnBrowseSrc);
    
    mainLayout->addWidget(sourceGroup);

    // Destination File Group
    QGroupBox* destGroup = new QGroupBox("DESTINATION FILE");
    destGroup->setStyleSheet(
        "QGroupBox {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #FAFBFC, stop:1 #F5F7FA);"
        "  border-radius: 12px; padding: 16px; padding-top: 28px;"
        "  font-weight: 600; color: #374151;"
        "}"
        "QGroupBox::title {"
        "  subcontrol-origin: margin; left: 12px; padding: 0 6px;"
        "  color: #374151; font-size: 12px; letter-spacing: 0.5px;"
        "}"
    );
    QHBoxLayout* destLayout = new QHBoxLayout(destGroup);
    
    m_destFileEdit = new QLineEdit();
    m_destFileEdit->setPlaceholderText("No file selected");
    m_destFileEdit->setReadOnly(true);
    m_destFileEdit->setStyleSheet(
        "QLineEdit {"
        "  padding: 10px; background: white;"
        "  border: 1px solid #E5E7EB; border-radius: 6px; font-size: 13px;"
        "}"
    );
    destLayout->addWidget(m_destFileEdit, 1);
    
    QPushButton* btnBrowseDest = new QPushButton("⚙ Browse");
    btnBrowseDest->setStyleSheet(
        "QPushButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #3B82F6, stop:1 #2563EB);"
        "  color: white; padding: 10px 20px;"
        "  border-radius: 6px; font-weight: 600; border: none;"
        "}"
        "QPushButton:hover {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #2563EB, stop:1 #1D4ED8);"
        "}"
        "QPushButton:pressed { background: #1E40AF; }"
    );
    btnBrowseDest->setFixedHeight(42);
    connect(btnBrowseDest, &QPushButton::clicked, this, &IndividualTransferPanel::onBrowseDestClicked);
    destLayout->addWidget(btnBrowseDest);
    
    mainLayout->addWidget(destGroup);

    // Graphical Selection Buttons
    m_btnSelectSource = new QPushButton("→ Select Source Cells (Graphical)");
    m_btnSelectSource->setStyleSheet(
        "QPushButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #3B82F6, stop:1 #2563EB);"
        "  color: white; padding: 10px 20px;"
        "  border-radius: 8px; font-weight: 600; font-size: 13px; border: none;"
        "}"
        "QPushButton:hover {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #2563EB, stop:1 #1D4ED8);"
        "}"
        "QPushButton:pressed { background: #1E40AF; }"
        "QPushButton:disabled { background: #E5E7EB; color: #9CA3AF; }"
    );
    m_btnSelectSource->setMaximumWidth(320);
    m_btnSelectSource->setFixedHeight(44);
    connect(m_btnSelectSource, &QPushButton::clicked, this, &IndividualTransferPanel::onSelectSourceClicked);
    mainLayout->addWidget(m_btnSelectSource);
    
    m_btnSelectDest = new QPushButton("→ Select Destination Cells (Graphical)");
    m_btnSelectDest->setStyleSheet(
        "QPushButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #3B82F6, stop:1 #2563EB);"
        "  color: white; padding: 10px 20px;"
        "  border-radius: 8px; font-weight: 600; font-size: 13px; border: none;"
        "}"
        "QPushButton:hover {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #2563EB, stop:1 #1D4ED8);"
        "}"
        "QPushButton:pressed { background: #1E40AF; }"
        "QPushButton:disabled { background: #E5E7EB; color: #9CA3AF; }"
    );
    m_btnSelectDest->setMaximumWidth(320);
    m_btnSelectDest->setFixedHeight(44);
    connect(m_btnSelectDest, &QPushButton::clicked, this, &IndividualTransferPanel::onSelectDestClicked);
    mainLayout->addWidget(m_btnSelectDest);

    // Mapping Table Section
    QLabel* mappingLabel = new QLabel("Transfer Mapping (editable):");
    mappingLabel->setStyleSheet(
        "font-weight: 600; font-size: 14px; margin-top: 8px; color: #374151;"
    );
    mainLayout->addWidget(mappingLabel);
    
    m_mappingTable = new QTableWidget();
    m_mappingTable->setColumnCount(4);
    m_mappingTable->setHorizontalHeaderLabels({"Source Cell", "Value", "→", "Destination Cell"});
    m_mappingTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_mappingTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_mappingTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_mappingTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_mappingTable->setColumnWidth(2, 50);
    m_mappingTable->setAlternatingRowColors(true);
    m_mappingTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_mappingTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_mappingTable->setStyleSheet(
        "QTableWidget {"
        "  gridline-color: #E5E7EB; background: white;"
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
    m_mappingTable->setMinimumHeight(250);
    mainLayout->addWidget(m_mappingTable);

    // Controls Row
    QHBoxLayout* controlsLayout = new QHBoxLayout();
    controlsLayout->setSpacing(10);
    
    m_mappingCounter = new QLabel("Mapped cells: 0");
    m_mappingCounter->setStyleSheet("font-weight: 600; color: #059669; font-size: 13px;");
    controlsLayout->addWidget(m_mappingCounter);
    
    controlsLayout->addStretch();
    
    QPushButton* btnAdd = new QPushButton("+ Add Row");
    btnAdd->setStyleSheet(
        "QPushButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #F9FAFB, stop:1 #F3F4F6);"
        "  color: #374151; padding: 8px 16px;"
        "  border-radius: 6px; font-weight: 600; border: none;"
        "}"
        "QPushButton:hover { background: #E5E7EB; }"
        "QPushButton:pressed { background: #D1D5DB; }"
    );
    connect(btnAdd, &QPushButton::clicked, this, &IndividualTransferPanel::onAddMappingRow);
    controlsLayout->addWidget(btnAdd);
    
    QPushButton* btnRemove = new QPushButton("✗ Remove");
    btnRemove->setStyleSheet(
        "QPushButton {"
        "  background: #FEF2F2; color: #DC2626; padding: 8px 16px;"
        "  border-radius: 6px; font-weight: 600; border: none;"
        "}"
        "QPushButton:hover { background: #FEE2E2; }"
        "QPushButton:pressed { background: #FECACA; }"
    );
    connect(btnRemove, &QPushButton::clicked, this, &IndividualTransferPanel::onRemoveSelectedMapping);
    controlsLayout->addWidget(btnRemove);
    
    QPushButton* btnClear = new QPushButton("✗ Clear All");
    btnClear->setStyleSheet(
        "QPushButton {"
        "  background: #FEF2F2; color: #DC2626; padding: 8px 16px;"
        "  border-radius: 6px; font-weight: 600; border: none;"
        "}"
        "QPushButton:hover { background: #FEE2E2; }"
        "QPushButton:pressed { background: #FECACA; }"
    );
    connect(btnClear, &QPushButton::clicked, this, &IndividualTransferPanel::onClearMappingTable);
    controlsLayout->addWidget(btnClear);
    
    mainLayout->addLayout(controlsLayout);

    // Copy Sheet Checkbox
    m_copySheetCheckbox = new QCheckBox("Copy entire sheet (ignores cell mappings)");
    m_copySheetCheckbox->setStyleSheet("font-size: 13px; padding: 8px; color: #374151;");
    mainLayout->addWidget(m_copySheetCheckbox);

    // Action Buttons
    QHBoxLayout* actionLayout = new QHBoxLayout();
    actionLayout->setSpacing(12);
    
    QPushButton* btnReset = new QPushButton("↺ Reset");
    btnReset->setStyleSheet(
        "QPushButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #F9FAFB, stop:1 #F3F4F6);"
        "  color: #374151; padding: 12px 24px;"
        "  border-radius: 8px; font-weight: 600; border: none;"
        "}"
        "QPushButton:hover { background: #E5E7EB; }"
        "QPushButton:pressed { background: #D1D5DB; }"
    );
    btnReset->setFixedHeight(48);
    connect(btnReset, &QPushButton::clicked, this, &IndividualTransferPanel::onResetClicked);
    actionLayout->addWidget(btnReset);
    
    actionLayout->addStretch();
    
    m_transferBtn = new QPushButton("▶ Execute Transfer");
    m_transferBtn->setStyleSheet(
        "QPushButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #059669, stop:1 #047857);"
        "  color: white; padding: 12px 32px;"
        "  border-radius: 8px; font-weight: 600; font-size: 14px; border: none;"
        "}"
        "QPushButton:hover {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #047857, stop:1 #065F46);"
        "}"
        "QPushButton:pressed { background: #064E3B; }"
        "QPushButton:disabled { background: #E5E7EB; color: #9CA3AF; }"
    );
    m_transferBtn->setFixedHeight(48);
    connect(m_transferBtn, &QPushButton::clicked, this, &IndividualTransferPanel::onTransferClicked);
    actionLayout->addWidget(m_transferBtn);
    
    mainLayout->addLayout(actionLayout);
    mainLayout->addStretch();
    
    updateTransferButtonState();
}

IndividualTransferPanel::TransferConfig IndividualTransferPanel::getTransferConfig() const
{
    return m_currentConfig;
}

void IndividualTransferPanel::setTransferConfig(const TransferConfig& config)
{
    m_currentConfig = config;
    m_sourceFileEdit->setText(config.sourceFile);
    m_destFileEdit->setText(config.destFile);
    updateTransferButtonState();
}

void IndividualTransferPanel::updateSourceSheets(const QStringList& sheets)
{
    // Can be used to populate a sheet selector if needed
    Q_UNUSED(sheets);
}

void IndividualTransferPanel::updateDestSheets(const QStringList& sheets)
{
    // Can be used to populate a sheet selector if needed
    Q_UNUSED(sheets);
}

QVector<IndividualTransferPanel::MappingEntry> IndividualTransferPanel::getMappingData() const
{
    QVector<MappingEntry> currentData = m_mappingData;
    for (int i = 0; i < m_mappingTable->rowCount() && i < currentData.size(); ++i) {
        if (auto* item = m_mappingTable->item(i, 0)) {
            currentData[i].sourceCell = item->text();
        }
        if (auto* item = m_mappingTable->item(i, 3)) {
            currentData[i].destCell = item->text();
        }
    }
    return currentData;
}

void IndividualTransferPanel::setMappingData(const QVector<MappingEntry>& data)
{
    m_mappingData = data;
    refreshMappingTable();
}

void IndividualTransferPanel::updateTransferButtonState()
{
    bool hasFiles = !m_sourceFileEdit->text().isEmpty() && 
                    !m_destFileEdit->text().isEmpty();
    bool hasMappings = !m_mappingData.isEmpty() || m_copySheetCheckbox->isChecked();
    
    bool enabled = hasFiles && hasMappings;
    m_transferBtn->setEnabled(enabled);
    
    if (!enabled) {
        m_transferBtn->setStyleSheet(
            "QPushButton {"
            "  background: #E5E7EB; color: #9CA3AF; padding: 12px 32px;"
            "  border-radius: 8px; font-weight: 600; font-size: 14px; border: none;"
            "}"
        );
    } else {
        m_transferBtn->setStyleSheet(
            "QPushButton {"
            "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
            "    stop:0 #059669, stop:1 #047857);"
            "  color: white; padding: 12px 32px;"
            "  border-radius: 8px; font-weight: 600; font-size: 14px; border: none;"
            "}"
            "QPushButton:hover {"
            "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
            "    stop:0 #047857, stop:1 #065F46);"
            "}"
            "QPushButton:pressed { background: #064E3B; }"
        );
    }
    
    // Enable/disable selection buttons
    m_btnSelectSource->setEnabled(!m_sourceFileEdit->text().isEmpty());
    m_btnSelectDest->setEnabled(!m_destFileEdit->text().isEmpty());
}

void IndividualTransferPanel::refreshMappingTable()
{
    m_mappingTable->setRowCount(m_mappingData.size());
    
    for (int i = 0; i < m_mappingData.size(); ++i) {
        const MappingEntry& entry = m_mappingData[i];
        
        // Source Cell
        QTableWidgetItem* srcItem = new QTableWidgetItem(entry.sourceCell);
        m_mappingTable->setItem(i, 0, srcItem);
        
        // Value
        QTableWidgetItem* valItem = new QTableWidgetItem(QString::number(entry.value, 'f', 2));
        m_mappingTable->setItem(i, 1, valItem);
        
        // Arrow
        QTableWidgetItem* arrowItem = new QTableWidgetItem("→");
        arrowItem->setTextAlignment(Qt::AlignCenter);
        arrowItem->setFlags(Qt::ItemIsEnabled);  // Not editable
        arrowItem->setBackground(QColor("#F8FAFC"));
        m_mappingTable->setItem(i, 2, arrowItem);
        
        // Destination Cell
        QTableWidgetItem* destItem = new QTableWidgetItem(entry.destCell);
        m_mappingTable->setItem(i, 3, destItem);
    }
    
    m_mappingCounter->setText(QString("Mapped cells: %1").arg(m_mappingData.size()));
    updateTransferButtonState();
}

void IndividualTransferPanel::onBrowseSourceClicked()
{
    emit browseSourceClicked();
}

void IndividualTransferPanel::onBrowseDestClicked()
{
    emit browseDestClicked();
}

void IndividualTransferPanel::onSelectSourceClicked()
{
    emit selectSourceCellClicked();
}

void IndividualTransferPanel::onSelectDestClicked()
{
    emit selectDestCellClicked();
}

void IndividualTransferPanel::onTransferClicked()
{
    m_currentConfig.sourceFile = m_sourceFileEdit->text();
    m_currentConfig.destFile = m_destFileEdit->text();
    emit transferClicked(m_currentConfig);
}

void IndividualTransferPanel::onResetClicked()
{
    resetToDefaults();
    emit resetClicked();
}

void IndividualTransferPanel::onAddMappingRow()
{
    MappingEntry entry;
    entry.sourceCell = "";
    entry.destCell = "";
    entry.value = 0.0;
    entry.sheetName = "";
    
    m_mappingData.append(entry);
    refreshMappingTable();
}

void IndividualTransferPanel::onRemoveSelectedMapping()
{
    QList<QTableWidgetItem*> selected = m_mappingTable->selectedItems();
    if (selected.isEmpty()) {
        return;
    }
    
    QSet<int> rows;
    for (QTableWidgetItem* item : selected) {
        rows.insert(item->row());
    }
    
    QList<int> rowList = rows.values();
    std::sort(rowList.begin(), rowList.end(), std::greater<int>());
    
    for (int row : rowList) {
        if (row >= 0 && row < m_mappingData.size()) {
            m_mappingData.removeAt(row);
        }
    }
    
    refreshMappingTable();
}

void IndividualTransferPanel::onClearMappingTable()
{
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "Clear All Mappings",
        "Are you sure you want to clear all cell mappings?",
        QMessageBox::Yes | QMessageBox::No
    );
    
    if (reply == QMessageBox::Yes) {
        m_mappingData.clear();
        refreshMappingTable();
    }
}
