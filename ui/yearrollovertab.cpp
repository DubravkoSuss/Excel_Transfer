#include "yearrollovertab.h"
#include "mainwindow.h"
#include "../features/rollover/rolloverworker.h"

#include <QFileDialog>
#include <QGroupBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QDateTime>

YearRolloverTab::YearRolloverTab(MainWindow* mainWindow, QWidget* parent)
    : QWidget(parent), m_mainWindow(mainWindow)
{
    setupUI();
}

void YearRolloverTab::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(20);

    // 1. Description Group
    QGroupBox* descGroup = new QGroupBox("Prepare New Year (Rollover)", this);
    QVBoxLayout* descLayout = new QVBoxLayout(descGroup);
    QLabel* descLabel = new QLabel(
        "This tool safely prepares a completed Excel file for the following year.<br>"
        "It sweeps <b>12 base columns</b> and <b>12 cumulative columns</b> "
        "and deletes all raw data and data-pulling formulas (like VLOOKUP), "
        "while <b>preserving all structural math and SUM formulas</b>.", this);
    descLabel->setWordWrap(true);
    descLayout->addWidget(descLabel);

    // 2. Configuration Group
    QGroupBox* configGroup = new QGroupBox("Configuration", this);
    QFormLayout* formLayout = new QFormLayout(configGroup);
    formLayout->setSpacing(12);

    // Source file
    QHBoxLayout* sourceLayout = new QHBoxLayout();
    m_sourceFileEdit = new QLineEdit(this);
    m_sourceFileEdit->setReadOnly(true);
    m_sourceFileEdit->setPlaceholderText("Select the completed previous year's MZLZ Consolidated file...");
    m_sourceBtn = new QPushButton("Browse...", this);
    connect(m_sourceBtn, &QPushButton::clicked, this, &YearRolloverTab::onBrowseSource);
    sourceLayout->addWidget(m_sourceFileEdit);
    sourceLayout->addWidget(m_sourceBtn);
    formLayout->addRow("Source File:", sourceLayout);

    // Target Year
    m_yearPicker = new QSpinBox(this);
    m_yearPicker->setRange(2020, 2100);
    m_yearPicker->setValue(QDateTime::currentDateTime().date().year() + 1); // Default to next year
    formLayout->addRow("New Target Year:", m_yearPicker);

    // Dest file
    QHBoxLayout* destLayout = new QHBoxLayout();
    m_destFileEdit = new QLineEdit(this);
    m_destFileEdit->setReadOnly(true);
    m_destFileEdit->setPlaceholderText("Select where to save the newly prepared file...");
    m_destBtn = new QPushButton("Browse...", this);
    connect(m_destBtn, &QPushButton::clicked, this, &YearRolloverTab::onBrowseDest);
    destLayout->addWidget(m_destFileEdit);
    destLayout->addWidget(m_destBtn);
    formLayout->addRow("Save As:", destLayout);

    // Row boundaries
    QHBoxLayout* rowLayout = new QHBoxLayout();
    m_startRowPicker = new QSpinBox(this);
    m_startRowPicker->setRange(1, 10000);
    m_startRowPicker->setValue(5);
    
    m_endRowPicker = new QSpinBox(this);
    m_endRowPicker->setRange(1, 10000);
    m_endRowPicker->setValue(300);

    rowLayout->addWidget(new QLabel("From Row:", this));
    rowLayout->addWidget(m_startRowPicker);
    rowLayout->addWidget(new QLabel("To Row:", this));
    rowLayout->addWidget(m_endRowPicker);
    rowLayout->addStretch();
    formLayout->addRow("Scan Range:", rowLayout);

    // 3. Execution Group
    QHBoxLayout* execLayout = new QHBoxLayout();
    m_executeBtn = new QPushButton("Prepare Workbook", this);
    m_executeBtn->setMinimumHeight(40);
    m_executeBtn->setProperty("class", "primary-button");
    connect(m_executeBtn, &QPushButton::clicked, this, &YearRolloverTab::onExecute);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setVisible(false);
    
    m_statusLabel = new QLabel("Ready.", this);

    execLayout->addWidget(m_executeBtn);
    execLayout->addWidget(m_progressBar);

    // Assembly
    mainLayout->addWidget(descGroup);
    mainLayout->addWidget(configGroup);
    mainLayout->addLayout(execLayout);
    mainLayout->addWidget(m_statusLabel);
    mainLayout->addStretch();
}

void YearRolloverTab::onBrowseSource()
{
    QString path = QFileDialog::getOpenFileName(this, "Select Completed Workbook", m_mainWindow->destFolder(), "Excel Files (*.xlsx *.xls *.xlsm)");
    if (!path.isEmpty()) {
        m_sourceFileEdit->setText(path);
        
        // Auto-generate dest name
        QFileInfo fi(path);
        QString dir = fi.absolutePath();
        QString newName = QString("%1_Cost_Control_Prepared.xlsx").arg(m_yearPicker->value());
        m_destFileEdit->setText(dir + "/" + newName);
    }
}

void YearRolloverTab::onBrowseDest()
{
    QString path = QFileDialog::getSaveFileName(this, "Save Prepared Workbook As", m_destFileEdit->text(), "Excel Files (*.xlsx *.xls *.xlsm)");
    if (!path.isEmpty()) {
        m_destFileEdit->setText(path);
    }
}

void YearRolloverTab::onExecute()
{
    if (m_sourceFileEdit->text().isEmpty()) {
        QMessageBox::warning(this, "Missing File", "Please select a source file to prepare.");
        return;
    }
    if (m_destFileEdit->text().isEmpty()) {
        QMessageBox::warning(this, "Missing Destination", "Please select where to save the prepared file.");
        return;
    }
    
    if (m_worker) {
        m_worker->wait();
        m_worker->deleteLater();
    }

    RolloverConfig config;
    config.sourceFile = m_sourceFileEdit->text();
    config.destFile   = m_destFileEdit->text();
    config.targetYear = m_yearPicker->value();
    config.startRow   = m_startRowPicker->value();
    config.endRow     = m_endRowPicker->value();

    m_worker = new RolloverWorker(config, this);
    
    connect(m_worker, &RolloverWorker::progress, this, &YearRolloverTab::onWorkerProgress);
    connect(m_worker, &RolloverWorker::finished, this, &YearRolloverTab::onWorkerFinished);

    m_executeBtn->setEnabled(false);
    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);
    m_statusLabel->setText("Initializing rollover process...");

    m_worker->start();
}

void YearRolloverTab::onWorkerProgress(int current, int total, const QString& message)
{
    m_progressBar->setMaximum(total);
    m_progressBar->setValue(current);
    m_statusLabel->setText(message);
}

void YearRolloverTab::onWorkerFinished(bool success, const QString& message)
{
    m_executeBtn->setEnabled(true);
    m_progressBar->setVisible(false);
    
    if (success) {
        m_statusLabel->setText("Rollover completed successfully.");
        QMessageBox::information(this, "Success", message);
    } else {
        m_statusLabel->setText("Rollover failed.");
        QMessageBox::critical(this, "Error", message);
    }
    
    m_worker->deleteLater();
    m_worker = nullptr;
}
