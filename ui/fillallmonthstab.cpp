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

FillAllMonthsTab::FillAllMonthsTab(MainWindow* mainWindow, QWidget* parent)
    : QWidget(parent), m_mainWindow(mainWindow)
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(16);

    // ── Title ──
    QLabel* titleLabel = new QLabel("Fill All Months");
    titleLabel->setStyleSheet("font-weight: 600; font-size: 18px; color: #1F2937;");
    mainLayout->addWidget(titleLabel);

    QLabel* descLabel = new QLabel(
        "Automatically transfer data for all months from January "
        "to the selected target month.");
    descLabel->setStyleSheet("color: #6B7280; font-size: 13px; margin-bottom: 8px;");
    mainLayout->addWidget(descLabel);

    // ── Selection card ──
    QFrame* selectionCard = new QFrame();
    selectionCard->setStyleSheet(
        "background: white; border-radius: 10px; padding: 16px;");
    QVBoxLayout* cardLayout = new QVBoxLayout(selectionCard);

    QHBoxLayout* topRow = new QHBoxLayout();
    topRow->setSpacing(12);

    topRow->addWidget(new QLabel("Year:"));
    m_yearCombo = new QComboBox();
    for (int y = 2024; y <= 2030; y++)
        m_yearCombo->addItem(QString::number(y));
    m_yearCombo->setCurrentText("2025");
    topRow->addWidget(m_yearCombo);

    topRow->addSpacing(20);
    topRow->addWidget(new QLabel("Target Month:"));
    m_monthCombo = new QComboBox();
    m_monthCombo->addItems({"January", "February", "March", "April", "May", "June",
                            "July", "August", "September", "October", "November", "December"});
    topRow->addWidget(m_monthCombo);

    topRow->addSpacing(20);
    m_scanBtn = new QPushButton("Scan Files");
    m_scanBtn->setStyleSheet(
        "background: #7C3AED; color: white; font-weight: 600; "
        "padding: 8px 24px; border-radius: 6px; font-size: 13px;");
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
    m_table->setMinimumHeight(300);
    mainLayout->addWidget(m_table);

    // ── Status ──
    m_statusLabel = new QLabel("Select year and target month, then click Scan Files.");
    m_statusLabel->setStyleSheet("color: #6B7280; font-style: italic; padding: 8px;");
    mainLayout->addWidget(m_statusLabel);

    // ── Execute button ──
    m_executeBtn = new QPushButton("Execute Fill All Months");
    m_executeBtn->setEnabled(false);
    m_executeBtn->setStyleSheet(
        "QPushButton { background: #059669; color: white; font-weight: 600; "
        "padding: 12px 24px; border-radius: 6px; font-size: 14px; }"
        "QPushButton:disabled { background: #D1D5DB; color: #9CA3AF; }");
    mainLayout->addWidget(m_executeBtn);
    mainLayout->addStretch();

    // ── Connections ──
    connect(m_scanBtn,    &QPushButton::clicked, this, &FillAllMonthsTab::onScan);
    connect(m_executeBtn, &QPushButton::clicked, this, &FillAllMonthsTab::onExecute);
}

void FillAllMonthsTab::onScan()
{
    int year        = m_yearCombo->currentText().toInt();
    int targetMonth = m_monthCombo->currentIndex() + 1;

    m_scanResult = FillAllScanResult();
    m_scanResult.year        = year;
    m_scanResult.targetMonth = targetMonth;

    // Use MainWindow helpers
    QString basePath = QString("%1/%2")
        .arg(m_mainWindow->destFolder()).arg(year);
    QString mm = QString("%1").arg(targetMonth, 2, 10, QChar('0'));
    QString destFolder = QString("%1/%2").arg(basePath, mm);
    m_scanResult.destFolder   = destFolder;
    m_scanResult.destFilePath = m_mainWindow->findCostControlFile(destFolder);

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

        // Budget/REFI
        {
            FillAllFileEntry e;
            e.month = monthName;
            e.monthNumber = m;
            e.transferType = "budget_refi";
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

        // SAP YTD (target month only)
        if (m == targetMonth) {
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
    m_statusLabel->setText(
        QString("Scan complete: %1 found, %2 missing out of %3 total")
            .arg(m_scanResult.foundCount())
            .arg(m_scanResult.missingCount())
            .arg(m_scanResult.entries.size()));
}

void FillAllMonthsTab::onExecute()
{
    emit executeRequested(m_scanResult);
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
