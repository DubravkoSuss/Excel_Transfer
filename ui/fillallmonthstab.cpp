#include "fillallmonthstab.h"
#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QHeaderView>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QScrollArea>
#include <QSplitter>
#include <QSet>
#include <QMessageBox>
#include "../core/mappingmodel.h"
#include "../core/mappingcontroller.h"
#include "../features/mappings/mappingrow.h"

FillAllMonthsTab::FillAllMonthsTab(MainWindow* mainWindow, QWidget* parent)
    : QWidget(parent), m_mainWindow(mainWindow)
{
    // ── Root: splitter — left sidebar (cards) + right panel ──
    QHBoxLayout* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(5);
    splitter->setStyleSheet(
        "QSplitter::handle { background: #E5E7EB; }"
        "QSplitter::handle:hover { background: #3B82F6; }"
    );
    rootLayout->addWidget(splitter);

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
    QLabel* cardsTitle = new QLabel("Active Mappings");
    cardsTitle->setStyleSheet(
        "font-weight: 700; font-size: 14px; color: #1F2937;"
    );
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

    connect(btnSelectAll,   &QPushButton::clicked, this, [this]() {
        if (m_mappingController) m_mappingController->setAllChecked(true);
    });
    connect(btnDeselectAll, &QPushButton::clicked, this, [this]() {
        if (m_mappingController) m_mappingController->setAllChecked(false);
    });

    QScrollArea* cardsScroll = new QScrollArea();
    cardsScroll->setWidgetResizable(true);
    cardsScroll->setStyleSheet(
        "QScrollArea { border: none; background: transparent; }"
    );

    m_cardsContainer = new QWidget();
    m_cardsLayout    = new QVBoxLayout(m_cardsContainer);
    m_cardsLayout->setContentsMargins(0, 0, 0, 0);
    m_cardsLayout->setSpacing(4);
    m_cardsLayout->addStretch();

    cardsScroll->setWidget(m_cardsContainer);
    sidebarLayout->addWidget(cardsScroll, 1);

    // Initialize model + controller
    m_mappingModel      = new MappingModel(this);
    m_mappingController = new MappingController(m_mappingModel, m_cardsContainer,
                                                m_cardsLayout, this);

    splitter->addWidget(sidebar);

    // ════════════════════════════════════════════
    // RIGHT PANEL — Controls + Table + Execute
    // ════════════════════════════════════════════
    QWidget* rightPanel = new QWidget();
    rightPanel->setMinimumWidth(400);
    QVBoxLayout* mainLayout = new QVBoxLayout(rightPanel);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    // ── Title ──
    QLabel* titleLabel = new QLabel("Fill All Months");
    titleLabel->setStyleSheet(
        "font-weight: 700; font-size: 18px; color: #1F2937; "
        "letter-spacing: -0.3px; margin-bottom: 4px;"
    );
    mainLayout->addWidget(titleLabel);

    QLabel* descLabel = new QLabel(
        "Automatically transfer data for all months from January to the selected target month."
    );
    descLabel->setStyleSheet("color: #6B7280; font-size: 13px; margin-bottom: 8px;");
    mainLayout->addWidget(descLabel);

    // ── Selection card ──
    QFrame* selectionCard = new QFrame();
    selectionCard->setStyleSheet(
        "QFrame {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #FAFBFC, stop:1 #F5F7FA);"
        "  border-radius: 12px; padding: 16px;"
        "}"
    );
    QVBoxLayout* cardLayout = new QVBoxLayout(selectionCard);
    cardLayout->setSpacing(12);

    QHBoxLayout* topRow = new QHBoxLayout();
    topRow->setSpacing(12);

    QLabel* yearLabel = new QLabel("Year:");
    yearLabel->setStyleSheet("font-weight: 600; color: #374151;");
    topRow->addWidget(yearLabel);

    m_yearCombo = new QComboBox();
    m_yearCombo->setStyleSheet(
        "QComboBox { background: white; border: 1px solid #E5E7EB; border-radius: 6px;"
        "  padding: 6px 12px; font-size: 13px; min-width: 80px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox:hover { border-color: #3B82F6; }"
    );
    for (int y = 2024; y <= 2030; y++)
        m_yearCombo->addItem(QString::number(y));
    m_yearCombo->setCurrentText("2025");
    topRow->addWidget(m_yearCombo);

    topRow->addSpacing(20);

    QLabel* monthLabel = new QLabel("Target Month:");
    monthLabel->setStyleSheet("font-weight: 600; color: #374151;");
    topRow->addWidget(monthLabel);

    m_monthCombo = new QComboBox();
    m_monthCombo->setStyleSheet(
        "QComboBox { background: white; border: 1px solid #E5E7EB; border-radius: 6px;"
        "  padding: 6px 12px; font-size: 13px; min-width: 120px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox:hover { border-color: #3B82F6; }"
    );
    m_monthCombo->addItems({"January", "February", "March", "April", "May", "June",
                            "July", "August", "September", "October", "November", "December"});
    topRow->addWidget(m_monthCombo);

    topRow->addSpacing(20);
    m_scanBtn = new QPushButton("⚙ Scan Files");
    m_scanBtn->setStyleSheet(
        "QPushButton { background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "    stop:0 #3B82F6, stop:1 #2563EB); color: white; font-weight: 600;"
        "  padding: 8px 20px; border-radius: 6px; font-size: 13px; border: none; }"
        "QPushButton:hover { background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "    stop:0 #2563EB, stop:1 #1D4ED8); }"
        "QPushButton:pressed { background: #1E40AF; }"
    );
    topRow->addWidget(m_scanBtn);
    topRow->addStretch();
    cardLayout->addLayout(topRow);
    mainLayout->addWidget(selectionCard);

    // ── Table ──
    m_table = new QTableWidget();
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({"Month","Type","File","Status"});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setAlternatingRowColors(true);
    m_table->setMinimumHeight(200);
    m_table->setStyleSheet(
        "QTableWidget { background: white; gridline-color: #E5E7EB;"
        "  border-radius: 8px; font-size: 13px; }"
        "QTableWidget::item { padding: 8px; }"
        "QHeaderView::section { background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "    stop:0 #F9FAFB, stop:1 #F3F4F6); color: #374151; padding: 10px;"
        "  font-weight: 600; border: none; border-bottom: 2px solid #E5E7EB; }"
        "QTableWidget::item:alternate { background: #F9FAFB; }"
    );
    mainLayout->addWidget(m_table);

    // ── Status ──
    m_statusLabel = new QLabel("Select year and target month, then click Scan Files.");
    m_statusLabel->setStyleSheet(
        "color: #6B7280; font-style: italic; padding: 8px; font-size: 13px;"
    );
    mainLayout->addWidget(m_statusLabel);

    // ── Execute button ──
    m_executeBtn = new QPushButton("▶ Execute Fill All Months");
    m_executeBtn->setMaximumWidth(300);
    m_executeBtn->setEnabled(false);
    m_executeBtn->setStyleSheet(
        "QPushButton { background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "    stop:0 #059669, stop:1 #047857); color: white; font-weight: 600;"
        "  padding: 12px 24px; border-radius: 8px; font-size: 14px; border: none; }"
        "QPushButton:hover { background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "    stop:0 #047857, stop:1 #065F46); }"
        "QPushButton:pressed { background: #064E3B; }"
        "QPushButton:disabled { background: #E5E7EB; color: #9CA3AF; }"
    );
    mainLayout->addWidget(m_executeBtn);
    mainLayout->addStretch();

    splitter->addWidget(rightPanel);
    splitter->setSizes({420, 800}); // initial sizes: sidebar 420px, right panel 800px
    splitter->setStretchFactor(0, 0); // sidebar doesn't auto-stretch
    splitter->setStretchFactor(1, 1); // right panel takes remaining space

    // ── Connections ──
    connect(m_scanBtn,    &QPushButton::clicked, this, &FillAllMonthsTab::onScan);
    connect(m_executeBtn, &QPushButton::clicked, this, &FillAllMonthsTab::onExecute);
}

void FillAllMonthsTab::onScan()
{
    if (!m_mainWindow) {
        m_statusLabel->setText("Scan failed: main window not available.");
        m_executeBtn->setEnabled(false);
        return;
    }

    int year        = m_yearCombo->currentText().toInt();
    int targetMonth = m_monthCombo->currentIndex() + 1;
    if (targetMonth < 1 || targetMonth > 12) {
        m_statusLabel->setText(QString("Scan failed: invalid target month index %1").arg(targetMonth));
        m_executeBtn->setEnabled(false);
        return;
    }

    m_scanResult = FillAllScanResult();
    m_scanResult.year        = year;
    m_scanResult.targetMonth = targetMonth;

    // Use MainWindow helpers
    QString basePath = QString("%1/%2")
        .arg(m_mainWindow->destFolder()).arg(year);
    QString mm = QString("%1").arg(targetMonth, 2, 10, QChar('0'));
    QString destFolder = QString("%1/%2/test").arg(basePath, mm);
    m_scanResult.destFolder   = destFolder;
    m_scanResult.destFilePath = m_mainWindow->findCostControlFile(destFolder);

    // ── Auto-create destination file if not found ──────────────────────────
    // Scan backwards from (targetMonth-1) down to January looking for the
    // last existing cost_control xlsm to use as a template for copying.
    bool fileWasCreated = false;
    QString createdFileName;
    if (!QFile::exists(m_scanResult.destFilePath)) {
        QString sourceTemplate;
        for (int sm = targetMonth - 1; sm >= 1; --sm) {
            QString smStr  = QString("%1").arg(sm, 2, 10, QChar('0'));
            QString smFolder = QString("%1/%2/test").arg(basePath, smStr);
            QString candidate = m_mainWindow->findCostControlFile(smFolder);
            if (QFile::exists(candidate)) {
                sourceTemplate = candidate;
                break;
            }
        }

        if (!sourceTemplate.isEmpty()) {
            // Build the correct destination file name: Cost Control ZAG {MM}_{YEAR}_working.xlsm
            QDir dir(destFolder);
            if (!dir.exists())
                dir.mkpath(destFolder);

            // Extract the base pattern from the source file name and replace month number
            QFileInfo srcInfo(sourceTemplate);
            QString srcName = srcInfo.fileName();
            // Replace the 2-digit month in the filename with the target month
            // Pattern: "Cost Control ZAG 03_2026_working.xlsm" → "Cost Control ZAG 03" part
            QString newName = srcName;
            // Replace the 2-digit month token before _YYYY in the filename.
            // e.g. "Cost Control ZAG 03_2026_working.xlsm" → "Cost Control ZAG 04_2026_working.xlsm"
            QString yearStr = QString::number(year);
            QString oldToken = "_" + yearStr;  // e.g. "_2026"
            int tokenPos = newName.indexOf(oldToken);
            if (tokenPos >= 2) {
                // Replace the 2 digits immediately before _YYYY with the target mm
                newName.replace(tokenPos - 2, 2, mm);
            }

            QString newPath = destFolder + "/" + newName;
            if (!QFile::exists(newPath)) {
                if (QFile::copy(sourceTemplate, newPath)) {
                    qInfo() << "[FILL_ALL] Created destination file:" << newPath
                            << "(copied from" << sourceTemplate << ")";
                    m_scanResult.destFilePath = newPath;
                    fileWasCreated = true;
                    createdFileName = QFileInfo(newPath).fileName();
                } else {
                    qWarning() << "[FILL_ALL] Failed to copy template to:" << newPath;
                }
            } else {
                m_scanResult.destFilePath = newPath;
            }
        } else {
            qWarning() << "[FILL_ALL] No template file found to create destination for month"
                       << mm << year;
        }
    }

    QStringList monthNames = {"January", "February", "March", "April", "May", "June",
                              "July", "August", "September", "October", "November", "December"};

    for (int m = 1; m <= targetMonth; m++) {
        QString monthName = monthNames[m - 1];
        QString mmStr = QString("%1").arg(m, 2, 10, QChar('0'));
        QString monthFolder = QString("%1/%2").arg(basePath, mmStr);

        // SAP monthly
        {
            FillAllFileEntry e;
            e.month = monthName;
            e.monthNumber = m;
            e.transferType = "sap";
            e.sourceFilePath = m_mainWindow->findSapFile(monthFolder, mmStr, year);
            e.found = QFile::exists(e.sourceFilePath);
            m_scanResult.entries.append(e);
        }

        // Budget/REFI — always reads FROM the target month's cost_control (same workbook as dest)
        {
            FillAllFileEntry e;
            e.month = monthName;
            e.monthNumber = m;
            e.transferType = "budget_refi";
            // Source is always the target month's cost_control (not the per-month folder),
            // because budget_refi copies data within the destination workbook itself.
            e.sourceFilePath = m_scanResult.destFilePath;
            e.found = QFile::exists(e.sourceFilePath);
            m_scanResult.entries.append(e);
        }

        // Traffic mott
        {
            FillAllFileEntry e;
            e.month = monthName;
            e.monthNumber = m;
            e.transferType = "traffic_mott";
            e.sourceFilePath = m_mainWindow->findTrafficFile(basePath, mmStr, year);
            e.found = QFile::exists(e.sourceFilePath);
            m_scanResult.entries.append(e);
        }

        // Staff
        {
            FillAllFileEntry e;
            e.month = monthName;
            e.monthNumber = m;
            e.transferType = "staff";
            e.sourceFilePath = m_mainWindow->findStaffFile(year);
            e.found = QFile::exists(e.sourceFilePath);
            m_scanResult.entries.append(e);
        }

        // PAX transfer
        {
            FillAllFileEntry e;
            e.month = monthName;
            e.monthNumber = m;
            e.transferType = "pax_transfer";
            e.sourceFilePath = m_mainWindow->findTrafficFile(basePath, mmStr, year);
            e.found = QFile::exists(e.sourceFilePath);
            m_scanResult.entries.append(e);
        }

        // SAP YTD — run for ALL months so that:
        //   1. Non-target months: writes monthly columns (G/W/AM...) for MZLZ rows 5-11
        //      (overwriting stale =+'TRAFFIC mott 2025'!$G$81 formulas)
        //   2. Target month: deferred to Phase 4 for final YTD JG column write
        {
            FillAllFileEntry e;
            e.month = monthName;
            e.monthNumber = m;
            e.transferType = "sap_ytd";
            e.sourceFilePath = m_mainWindow->findSapYtdFile(monthFolder, mmStr, year);
            e.found = QFile::exists(e.sourceFilePath);
            m_scanResult.entries.append(e);
        }
    }

    populateTable();
    m_executeBtn->setEnabled(QFile::exists(m_scanResult.destFilePath));

    // Populate mapping cards for ALL months Jan..targetMonth
    if (m_mappingController && m_mainWindow) {
        m_mappingController->clearAllMappings();
        const int year       = m_yearCombo->currentText().toInt();
        const int targetMonth = m_monthCombo->currentIndex() + 1; // 1-based
        static const QStringList monthNames = {
            "January","February","March","April","May","June",
            "July","August","September","October","November","December"
        };
        for (int m = 1; m <= targetMonth; ++m) {
            m_mainWindow->populateFillAllMappingCards(
                m_mappingController, monthNames[m - 1], year);
        }
    }

    QString statusText = QString("Scan complete: %1 found, %2 missing out of %3 total")
        .arg(m_scanResult.foundCount())
        .arg(m_scanResult.missingCount())
        .arg(m_scanResult.entries.size());

    if (fileWasCreated) {
        statusText += QString("\n⚠ Destination file not found — created new file: %1\n"
                              "   Execute will fill this new file with data.")
                          .arg(createdFileName);
        m_statusLabel->setStyleSheet("color: #D97706; font-style: normal; padding: 8px; "
                                     "font-weight: bold;");  // amber warning colour
    } else {
        m_statusLabel->setStyleSheet("color: #6B7280; font-style: italic; padding: 8px;");
    }

    m_statusLabel->setText(statusText);
}

void FillAllMonthsTab::onExecute()
{
    // ── Check if destination file is open ──
    if (!m_scanResult.destFilePath.isEmpty() && QFile::exists(m_scanResult.destFilePath)) {
        // Try to open the file exclusively to check if it's locked
        QFile testFile(m_scanResult.destFilePath);
        if (!testFile.open(QIODevice::ReadWrite)) {
            // File is locked (likely open in Excel)
            QMessageBox::warning(
                this,
                "File is Open",
                QString("The destination Excel file is currently open:\n\n%1\n\n"
                        "Please close the file in Excel and click Execute again.")
                    .arg(QFileInfo(m_scanResult.destFilePath).fileName())
            );
            return;
        }
        testFile.close();
    }
    
    // Build enabled types and copyFullSheet info from checked mapping cards
    QSet<QString> enabledTypes;
    // Map: transferType → {copyFullSheet, customSheetName, insertAfterSheet, sourceSheetName}
    struct SheetCopyInfo { bool copy; QString custom; QString insertAfter; QString srcSheet; };
    QMap<QString, SheetCopyInfo> copyInfoByType;
    bool hasCards = false;

    if (m_mappingController) {
        QVector<MappingItem> items = m_mappingController->items();
        for (int i = 0; i < items.size(); ++i) {
            MappingRow* row = m_mappingController->rowAt(i);
            if (row && row->isChecked()) {
                const QString& type = items[i].entry.sourceFileType;
                enabledTypes.insert(type);
                if (row->isCopyFullSheet() && !copyInfoByType.contains(type)) {
                    copyInfoByType[type] = {
                        true,
                        row->getCustomSheetName(),
                        row->getInsertAfterSheet(),
                        items[i].entry.sourceSheetTemplate
                    };
                }
                hasCards = true;
            }
        }
    }

    if (!hasCards) {
        emit executeRequested(m_scanResult);
        return;
    }

    // Filter scan entries and apply copyFullSheet info
    FillAllScanResult filtered = m_scanResult;
    filtered.entries.clear();
    for (auto e : m_scanResult.entries) {
        if (!enabledTypes.contains(e.transferType)) continue;
        if (copyInfoByType.contains(e.transferType)) {
            const auto& info = copyInfoByType[e.transferType];
            e.copyFullSheet    = info.copy;
            e.customSheetName  = info.custom;
            e.insertAfterSheet = info.insertAfter;
            e.sourceSheetName  = info.srcSheet;
        }
        filtered.entries.append(e);
    }

    emit executeRequested(filtered);
}

void FillAllMonthsTab::populateTable()
{
    m_table->setRowCount(0);
    for (const auto& e : m_scanResult.entries) {
        int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(e.month));
        m_table->setItem(row, 1, new QTableWidgetItem(e.transferType));
        m_table->setItem(row, 2, new QTableWidgetItem(QFileInfo(e.sourceFilePath).fileName()));
        auto* statusItem = new QTableWidgetItem(e.found ? "✅" : "❌");
        m_table->setItem(row, 3, statusItem);
        if (!e.found) {
            for (int c = 0; c < 4; c++)
                m_table->item(row, c)->setBackground(QColor(240, 240, 240));
        }
    }
}
