#include "../core/mappingsmanager.h"
#include "mainwindow.h"
#include "../features/mappings/rowmapvalidatordialog.h"
#include "../features/periods/periodrow.h"
#include "../features/periods/yearcard.h"
#include "../core/periodcontroller.h"
#include "../core/periodmodel.h"
#include "../features/mappings/mappingrow.h"
#include "../features/transfer/individualtransferpanel.h"
#include "../features/transfer/transferworker.h"
#include "../features/transfer/loadworker.h"
#include "../features/transfer/rollingworker.h"
#include "../features/transfer/rollingtransferservice.h"
#include "individualtransfertab.h"
#include "fillallmonthstab.h"
#include <QRegularExpression>
#include <QDebug>
#include <QDateTime>
#include <QFileDialog>
#include <QCoreApplication>
#include <QDir>
#include <QInputDialog>
#include <QHeaderView>
#include <QProgressDialog>
#include <QTimer>

const QString MainWindow::PASSWORD = "finance2026";
const QString MainWindow::DEFAULT_SOURCE_FOLDER = "L:/Cost control/Cost Control/Cost control";
const QString MainWindow::DEFAULT_DEST_FOLDER   = "L:/Cost control/Cost Control/Cost control";
const QStringList MainWindow::MONTHS_LIST = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
const QMap<QString, QString> MainWindow::MONTH_TO_NUM = {{"January", "01"}, {"February", "02"}, {"March", "03"}, {"April", "04"}, {"May", "05"}, {"June", "06"}, {"July", "07"}, {"August", "08"}, {"September", "09"}, {"October", "10"}, {"November", "11"}, {"December", "12"}};
const QMap<int, QList<int>> MainWindow::QUARTERS = {{1, {0, 1, 2}}, {2, {3, 4, 5}}, {3, {6, 7, 8}}, {4, {9, 10, 11}}};
MainWindow* MainWindow::s_instance = nullptr;
const QVector<int> MainWindow::YEAR_RANGE = {2010, 2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018, 2019, 2020, 2021, 2022, 2023, 2024, 2025, 2026, 2027, 2028, 2029, 2030, 2031, 2032, 2033, 2034, 2035, 2036, 2037, 2038, 2039, 2040, 2041, 2042, 2043};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_isTransferRunning(false)
    , m_isTransferPaused(false)
    , m_transferTotalMappings(0)
    , m_transferSuccessfulMappings(0)
{
    s_instance = this;
    setupUI();

    m_mappingModel = new MappingModel(this);
    m_mappingController = new MappingController(m_mappingModel, m_centralWidget, m_costControlLayout, this);
    if (m_costControlContainer && m_costControlLayout) {
        m_mappingController->setParentWidget(m_costControlContainer);
        m_mappingController->setSectionLayouts(m_costControlLayout, nullptr, nullptr, nullptr, nullptr);
    }

    m_periodModel = new PeriodModel(this);
    for (auto it = m_yearCheckboxes.constBegin(); it != m_yearCheckboxes.constEnd(); ++it) {
        m_periodModel->addYear(it.key(), MONTHS_LIST);
    }
    m_periodController = new PeriodController(m_periodModel, this);
    m_periodController->setLoadingGuards(&m_isLoadingRT, &m_isLoadingPeriods);

    connect(m_periodController, &PeriodController::clearUI, this, &MainWindow::clearPeriodRows);
    connect(m_periodController, &PeriodController::periodsReady, this, &MainWindow::displayPeriodRows);
    connect(m_mappingController, &MappingController::rowCountChanged, this, [this](int count) {
        m_noMappingsLabel->setVisible(count == 0);
    });
    connect(m_mappingController, &MappingController::requestRun,          this, &MainWindow::onMappingRunClicked);
    connect(m_mappingController, &MappingController::requestRemove,       this, &MainWindow::onMappingRemoveClicked);
    connect(m_mappingController, &MappingController::requestEditRows,     this, &MainWindow::onMappingEditRowsClicked);
    connect(m_mappingController, &MappingController::requestExportRowMap, this, &MainWindow::onExportRowMapClicked);
    connect(m_mappingController, &MappingController::requestImportRowMap, this, &MainWindow::onImportRowMapClicked);
    connect(m_mappingController, &MappingController::rowCountChanged, this, [this](int) {
        updateExecuteAllButton();
    });
    connect(m_mappingController, &MappingController::rowChanged, this, [this]() {
        updateExecuteAllButton();
    });

    m_excelHandler = new ExcelHandler(this);
    m_mappingsManager = new MappingsManager(this);
    m_breakLinks = new BreakLinks(this);
    m_transferService = new TransferService(m_excelHandler, this);
    m_loadService = new LoadService(m_mappingsManager, this);
    m_mappingService = new MappingService(m_mappingsManager, this);
    m_rollingService = new RollingTransferService(m_excelHandler, m_transferService, m_mappingsManager, m_mappingService, this);

    m_sourceFolder = DEFAULT_SOURCE_FOLDER;
    m_destFolder = DEFAULT_DEST_FOLDER;

    updateStatusBar("Ready - Select years and months, then click Load");
    qInstallMessageHandler(MainWindow::handleQtMessage);
}

MainWindow::~MainWindow()
{
}


void MainWindow::setupUI()
{
    setWindowTitle("Excel Transfer Tool - Monthly Auto");
    resize(1100, 850);
    setMinimumSize(950, 620);

    QScrollArea* mainScroll = new QScrollArea();
    mainScroll->setWidgetResizable(true);
    mainScroll->setFrameShape(QFrame::NoFrame);

    m_centralWidget = new QWidget();
    mainScroll->setWidget(m_centralWidget);
    setCentralWidget(mainScroll);

    m_mainLayout = new QVBoxLayout(m_centralWidget);
    m_mainLayout->setContentsMargins(16, 16, 16, 16);
    m_mainLayout->setSpacing(16);

    createHeader();
    
    // Create tab widget for main content
    m_tabWidget = new QTabWidget();
    m_mainLayout->addWidget(m_tabWidget);
    
    // Create main transfer tab
    QWidget* mainTransferTab = new QWidget();
    QVBoxLayout* mainTransferLayout = new QVBoxLayout(mainTransferTab);
    mainTransferLayout->setContentsMargins(0, 0, 0, 0);
    mainTransferLayout->setSpacing(16);
    
    m_tabWidget->addTab(mainTransferTab, "Monthly Transfer");
    
    // Move existing content into main transfer tab
    QWidget* tempContainer = new QWidget();
    QVBoxLayout* tempLayout = new QVBoxLayout(tempContainer);
    tempLayout->setContentsMargins(16, 16, 16, 16);
    tempLayout->setSpacing(16);
    
    createMonthYearSelector();
    createFileInfoCard();
    createMappingsCard();
    
    // Move widgets from main layout to tab layout
    while (m_mainLayout->count() > 2) { // Keep header and tab widget
        QLayoutItem* item = m_mainLayout->takeAt(2);
        if (item->widget()) {
            tempLayout->addWidget(item->widget());
        }
        delete item;
    }
    
    mainTransferLayout->addWidget(tempContainer);
    
    // Create Fill All tab
    createFillAllTab();
    
    // Create Individual Transfer tab
    createIndividualTab();


    m_progressBar = new QProgressBar();
    m_progressBar->setMaximumHeight(6);
    m_progressBar->setTextVisible(false);
    m_progressBar->setVisible(false);
    m_mainLayout->addWidget(m_progressBar);

    m_statusBar = statusBar();
    m_statusLabel = new QLabel("Ready");
    m_statusBar->addWidget(m_statusLabel, 1);

    // Status sidebar (hidden by default)
    m_statusDock = new QDockWidget("Status Log", this);
    m_statusDock->setAllowedAreas(Qt::RightDockWidgetArea);
    m_statusDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    m_statusText = new QTextEdit();
    m_statusText->setReadOnly(true);
    m_statusText->setStyleSheet("background:#0b0b0b; color:#f5f5f5; font-family: Consolas, 'Courier New', monospace; font-size: 11px;");
    m_statusDock->setWidget(m_statusText);
    addDockWidget(Qt::RightDockWidgetArea, m_statusDock);
    m_statusDock->setVisible(false);
}

void MainWindow::createHeader()
{
    m_headerFrame = new QFrame();
    m_headerFrame->setObjectName("headerFrame");
    m_headerFrame->setStyleSheet("#headerFrame { background: #0F1B3A; }");

    QHBoxLayout* headerLayout = new QHBoxLayout(m_headerFrame);
    headerLayout->setContentsMargins(20, 20, 20, 20);

    QLabel* logoLabel = new QLabel();
    QPixmap pixmap(":/MZLZ.gif");
    if (!pixmap.isNull()) {
        QPixmap scaled = pixmap.scaled(80, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        logoLabel->setPixmap(scaled);
    } else {
        logoLabel->setText("MZLZ");
        logoLabel->setStyleSheet("font-size: 28px; font-weight: bold; color: white;");
    }
    headerLayout->addWidget(logoLabel);

    QVBoxLayout* titleLayout = new QVBoxLayout();
    m_titleLabel = new QLabel("EXCEL TRANSFER");
    m_titleLabel->setStyleSheet("font-size: 24px; font-weight: 700; color: white;");
    titleLayout->addWidget(m_titleLabel);

    m_subtitleLabel = new QLabel("Finance dept. MZLZ Zagreb");
    m_subtitleLabel->setStyleSheet("font-size: 13px; color: #A0B1D0;");
    titleLayout->addWidget(m_subtitleLabel);
    headerLayout->addLayout(titleLayout);

    headerLayout->addStretch();

    m_btnExecuteAll = new QPushButton("Execute All");
    m_btnExecuteAll->setStyleSheet("background: #059669; color: white; font-weight: 600; padding: 10px 20px; border-radius: 6px;");
    connect(m_btnExecuteAll, &QPushButton::clicked, this, &MainWindow::onExecuteAll);
    m_btnExecuteAll->setEnabled(false); // disabled until mappings are loaded and checked
    headerLayout->addWidget(m_btnExecuteAll);

    m_btnPause = new QPushButton("Pause");
    m_btnPause->setStyleSheet("background: #F3F4F6; color: #374151; padding: 10px 20px; border-radius: 6px;");
    m_btnPause->setEnabled(false);
    connect(m_btnPause, &QPushButton::clicked, this, &MainWindow::onPauseTransfer);
    headerLayout->addWidget(m_btnPause);

    m_btnStop = new QPushButton("Stop");
    m_btnStop->setStyleSheet("background: #DC2626; color: white; font-weight: 600; padding: 10px 20px; border-radius: 6px;");
    m_btnStop->setEnabled(false);
    connect(m_btnStop, &QPushButton::clicked, this, &MainWindow::onStopTransfer);
    headerLayout->addWidget(m_btnStop);

    m_btnRollingTransfer = new QPushButton("Execute RT");
    m_btnRollingTransfer->setStyleSheet(
        "background: #D97706; color: white; font-weight: 600; padding: 10px 20px; border-radius: 6px;"
    );
    m_btnRollingTransfer->setEnabled(false);
    m_btnRollingTransfer->setToolTip("Execute Rolling Transfer  ? run after Load RT");
    connect(m_btnRollingTransfer, &QPushButton::clicked, this, &MainWindow::onRollingTransfer);
    headerLayout->addWidget(m_btnRollingTransfer);

    m_btnExportSelectedMonths = new QPushButton("Export Selected Months");
    m_btnExportSelectedMonths->setStyleSheet(
        "background: #0D9488; color: white; font-weight: 600; "
        "padding: 10px 20px; border-radius: 6px;");
    connect(m_btnExportSelectedMonths, &QPushButton::clicked,
            this, &MainWindow::onExportSelectedMonths);
    headerLayout->addWidget(m_btnExportSelectedMonths);

    // Reset button  ? clears everything back to a fresh state
    QPushButton* btnReset = new QPushButton("Reset");
    btnReset->setStyleSheet(
        "QPushButton { background: #1E3A5F; color: #A0B1D0; font-weight: 600; "
        "padding: 10px 16px; border-radius: 6px; border: 1px solid #2D4E7A; }"
        "QPushButton:hover { background: #2D4E7A; color: white; }"
    );
    btnReset->setToolTip("Reset everything: clears cached files, mapping cards, and period rows");
    connect(btnReset, &QPushButton::clicked, this, &MainWindow::onResetAll);
    headerLayout->addWidget(btnReset);

    m_mainLayout->addWidget(m_headerFrame);
}

void MainWindow::createMonthYearSelector()
{
    QFrame* selectorCard = new QFrame();
    selectorCard->setStyleSheet("background: white; border-radius: 10px;");

    QVBoxLayout* selectorLayout = new QVBoxLayout(selectorCard);
    selectorLayout->setContentsMargins(16, 16, 16, 16);
    selectorLayout->setSpacing(10);

    QLabel* title = new QLabel("Select Years & Months");
    title->setStyleSheet("font-weight: 1000; font-size: 15px; color: #1E3A5F;");
    selectorLayout->addWidget(title);

    QLabel* hint = new QLabel("Check a year to expand  ? then select months or use Q1 ?Q4 buttons in each year");
    hint->setStyleSheet("font-size: 11px; color: #6B7280;");
    selectorLayout->addWidget(hint);

    QGroupBox* yearsGroup = new QGroupBox();
    yearsGroup->setStyleSheet(
        "QGroupBox { border: 1px solid #E5E7EB; border-radius: 8px; margin-top: 4px; padding-top: 4px; }"
    );
    QVBoxLayout* yearsMainLayout = new QVBoxLayout(yearsGroup);
    yearsMainLayout->setSpacing(4);

    // Create a scrollable area for years
    QScrollArea* yearsScroll = new QScrollArea();
    yearsScroll->setWidgetResizable(true);
    yearsScroll->setFrameShape(QFrame::NoFrame);
    yearsScroll->setMaximumHeight(400);
    
    QWidget* yearsContainer = new QWidget();
    QVBoxLayout* yearsContainerLayout = new QVBoxLayout(yearsContainer);
    yearsContainerLayout->setSpacing(2);
    yearsContainerLayout->setContentsMargins(0, 0, 0, 0);

    for (int year : YEAR_RANGE) {
        // Use YearCard  ? a proper QWidget that owns all its children safely
        YearCard* card = new YearCard(year, yearsContainer);

        // Wire year checkbox � period controller
        connect(card, &YearCard::yearChecked, this, [this](int yr, bool checked) {
            if (m_periodController) {
                m_periodController->onYearToggled(yr, checked);
                if (checked) m_periodController->generatePeriodRows();
            }
        });

        // Store a dummy QCheckBox pointer for compatibility with existing
        // code that calls m_yearCheckboxes[year]->setChecked(...)
        // We wrap it by storing the YearCard's internal checkbox via a shim.
        // Since we no longer use m_yearCheckboxes directly to build the UI,
        // just store nullptr  ? onResetAll and clearPeriodRows are already safe.
        m_yearCheckboxes[year] = nullptr;
        m_yearCards[year] = card;

        yearsContainerLayout->addWidget(card);
    }
    
    yearsScroll->setWidget(yearsContainer);
    yearsMainLayout->addWidget(yearsScroll);
    selectorLayout->addWidget(yearsGroup);

    // �� Period rows area (populated by displayPeriodRows when LOAD is clicked) �
    // m_monthYearSelector is the container widget; m_monthYearLayout is its layout.
    // clearPeriodRows() and displayPeriodRows() both use these.
    m_monthYearSelector = new QWidget(selectorCard);
    m_monthYearLayout   = new QVBoxLayout(m_monthYearSelector);
    m_monthYearLayout->setContentsMargins(0, 4, 0, 0);
    m_monthYearLayout->setSpacing(6);
    m_monthYearLayout->addStretch(); // keeps widgets pinned to top
    selectorLayout->addWidget(m_monthYearSelector);

    // All action buttons in one compact row  ? addStretch() keeps them left-aligned and natural width
    QHBoxLayout* actionBtnLayout = new QHBoxLayout();
    actionBtnLayout->setSpacing(8);
    actionBtnLayout->setContentsMargins(0, 4, 0, 0);

    static const QString loadStyle =
        "QPushButton { background: #7C3AED; color: white; font-weight: 600; "
        "  padding: 7px 16px; border-radius: 6px; font-size: 12px; }"
        "QPushButton:hover { background: #6D28D9; }";
    static const QString rtStyle =
        "QPushButton { background: #B45309; color: white; font-weight: 600; "
        "  padding: 7px 16px; border-radius: 6px; font-size: 12px; }"
        "QPushButton:hover { background: #92400E; }";
    static const QString clearStyle =
        "QPushButton { background: #FEF2F2; color: #DC2626; font-weight: 600; "
        "  padding: 7px 14px; border-radius: 6px; font-size: 12px; border: 1px solid #FECACA; }"
        "QPushButton:hover { background: #FEE2E2; }";

    m_btnLoad = new QPushButton("Load Periods");
    m_btnLoad->setStyleSheet(loadStyle);
    m_btnLoad->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(m_btnLoad, &QPushButton::clicked, this, &MainWindow::onLoadSelectedPeriods);
    actionBtnLayout->addWidget(m_btnLoad);

    m_btnLoadRT = new QPushButton("Load RT");
    m_btnLoadRT->setStyleSheet(rtStyle);
    m_btnLoadRT->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_btnLoadRT->setToolTip("Rolling Transfer");
    connect(m_btnLoadRT, &QPushButton::clicked, this, &MainWindow::onLoadRT);
    actionBtnLayout->addWidget(m_btnLoadRT);

    m_btnClear = new QPushButton("Clear");
    m_btnClear->setStyleSheet(clearStyle);
    m_btnClear->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_btnClear->setToolTip("Clear all period rows and month selections");
    connect(m_btnClear, &QPushButton::clicked, this, &MainWindow::onClearPeriodRows);
    actionBtnLayout->addWidget(m_btnClear);

    actionBtnLayout->addStretch(); // keeps buttons left-aligned at natural width
    selectorLayout->addLayout(actionBtnLayout);

    m_mainLayout->addWidget(selectorCard);
}

void MainWindow::createFileInfoCard()
{
    m_fileInfoCard = new QFrame();
    m_fileInfoCard->setStyleSheet("background: white; border-radius: 10px;");

    QVBoxLayout* fileLayout = new QVBoxLayout(m_fileInfoCard);
    fileLayout->setContentsMargins(16, 16, 16, 16);
    fileLayout->setSpacing(12);

    QLabel* title = new QLabel("Folder Settings");
    title->setStyleSheet("font-weight: 600; font-size: 15px;");
    fileLayout->addWidget(title);

    QLabel* pathLabel = new QLabel(QString("Base path: %1").arg(DEFAULT_DEST_FOLDER));
    pathLabel->setStyleSheet("color: #6B7280; font-size: 12px;");
    fileLayout->addWidget(pathLabel);

    QLabel* structureLabel = new QLabel("Structure: {base}/{year}/{month_num}/  |  SAP export monthly/  |  SAP YTD/  |  Traffic/  |  Staff/");
    structureLabel->setStyleSheet("color: #9CA3AF; font-size: 11px;");
    fileLayout->addWidget(structureLabel);

    m_mainLayout->addWidget(m_fileInfoCard);
}

void MainWindow::createMappingsCard()
{
    m_mappingsCard = new QFrame();
    m_mappingsCard->setStyleSheet("background: white; border-radius: 10px;");

    QVBoxLayout* mappingsCardLayout = new QVBoxLayout(m_mappingsCard);
    mappingsCardLayout->setContentsMargins(16, 16, 16, 16);
    mappingsCardLayout->setSpacing(12);

    QLabel* title = new QLabel("Active Mappings");
    title->setStyleSheet("font-weight: 600; font-size: 15px;");
    mappingsCardLayout->addWidget(title);

    // Select All / Deselect All toggle button
    m_btnSelectAll = new QPushButton("Select All");
    m_btnSelectAll->setStyleSheet(
        "QPushButton { background: #4F46E5; color: white; font-weight: 600; "
        "padding: 6px 14px; border-radius: 6px; font-size: 11px; }"
        "QPushButton:hover { background: #4338CA; }"
    );
    m_btnSelectAll->setMaximumWidth(120);
    m_allSelected = false;
    connect(m_btnSelectAll, &QPushButton::clicked, this, [this]() {
        m_allSelected = !m_allSelected;
        m_btnSelectAll->setText(m_allSelected ? "Deselect All" : "Select All");
        if (m_mappingController) {
            m_mappingController->setAllChecked(m_allSelected);
        }
    });
    mappingsCardLayout->addWidget(m_btnSelectAll);

    auto createSection = [this, mappingsCardLayout](const QString& label, QWidget*& container, QVBoxLayout*& layout) {
        QLabel* sectionTitle = new QLabel(label);
        sectionTitle->setStyleSheet("font-weight: 600; margin-top: 6px;");
        mappingsCardLayout->addWidget(sectionTitle);

        container = new QWidget();
        layout = new QVBoxLayout(container);
        layout->setSpacing(8);

        QScrollArea* scroll = new QScrollArea();
        scroll->setWidget(container);
        scroll->setWidgetResizable(true);
        scroll->setMinimumHeight(140);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        mappingsCardLayout->addWidget(scroll);
    };

    createSection("Cost Control", m_costControlContainer, m_costControlLayout);
    if (m_mappingController && m_costControlContainer && m_costControlLayout) {
        m_mappingController->setParentWidget(m_costControlContainer);
        m_mappingController->setSectionLayouts(m_costControlLayout, nullptr, nullptr, nullptr, nullptr);
    }

    m_noMappingsLabel = new QLabel("No mappings loaded. Select periods and click Load.");
    m_noMappingsLabel->setStyleSheet("color: #6B7280; font-style: italic; padding: 20px;");
    mappingsCardLayout->addWidget(m_noMappingsLabel);

    m_mainLayout->addWidget(m_mappingsCard);
}

void MainWindow::createIndividualTab()
{
    m_individualTransferTab = new IndividualTransferTab(this);
    m_tabWidget->addTab(m_individualTransferTab, "Individual Transfer");
}

void MainWindow::updateStatusBar(const QString& message)
{
    m_statusLabel->setText(message);
}

void MainWindow::showToast(const QString& message, ToastWidget::ToastType type, int duration)
{
    if (!m_toastWidget) {
        m_toastWidget = new ToastWidget(this);
    }
    m_toastWidget->showToast(message, type, duration);
}

// onGeneratePeriodRows() removed  ? period rows now auto-generate when year checkboxes are toggled.
// The connect() in createMonthYearSelector() calls m_periodController->generatePeriodRows() directly.

void MainWindow::onClearPeriodRows()
{
    // Remove period rows from the layout
    clearPeriodRows();

    // Collapse all year cards and remove their PeriodRows
    for (auto it = m_yearCards.constBegin(); it != m_yearCards.constEnd(); ++it) {
        YearCard* card = it.value();
        if (!card) continue;
        card->blockSignals(true);
        // Remove all period rows from the card
        for (PeriodRow* row : card->rows()) {
            card->removeRow(row);
            row->deleteLater();
        }
        card->setExpanded(false);
        card->blockSignals(false);
    }

    updateStatusBar("Period rows cleared");
}

void MainWindow::onResetAll()
{
    if (m_isTransferRunning) {
        showToast("Cannot reset while a transfer is running. Stop it first.", ToastWidget::Warning);
        return;
    }
    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Confirm Reset");
    msgBox.setText("Reset everything?");
    msgBox.setInformativeText(
        "This will:\n"
        "  \xe2\x80\xa2 Stop any running workers\n"
        "  \xe2\x80\xa2 Unload all cached Excel files from memory\n"
        "  \xe2\x80\xa2 Remove all mapping cards\n"
        "  \xe2\x80\xa2 Clear all selected months and year cards\n"
        "  \xe2\x80\xa2 Reset all buttons and progress\n\n"
        "This cannot be undone."
    );
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Cancel);
    QPushButton* yesBtn = qobject_cast<QPushButton*>(msgBox.button(QMessageBox::Yes));
    if (yesBtn) {
        yesBtn->setText("Reset Everything");
        yesBtn->setStyleSheet("background-color: #d9534f; color: white; padding: 6px 16px; border-radius: 4px;");
    }
    if (msgBox.exec() != QMessageBox::Yes) return;
    doReset();
}

void MainWindow::doReset()
{
    qInfo() << "[RESET] === START ===";

    // PHASE 0: Block all signals to prevent re-entrant chains
    this->blockSignals(true);
    safeDisconnectAll();

    // PHASE 1: Stop workers
    if (m_transferWorker) {
        m_transferWorker->disconnect();
        m_transferWorker->stop();
        if (!m_transferWorker->wait(3000)) { m_transferWorker->terminate(); m_transferWorker->wait(1000); }
        delete m_transferWorker; m_transferWorker = nullptr;
    }
    if (m_loadWorker) {
        m_loadWorker->disconnect();
        m_loadWorker->stop();
        if (!m_loadWorker->wait(3000)) { m_loadWorker->terminate(); m_loadWorker->wait(1000); }
        delete m_loadWorker; m_loadWorker = nullptr;
    }

    // PHASE 2: Close transfer dialog
    if (m_transferDialog) {
        m_transferDialog->disconnect();
        m_transferDialog->close();
        delete m_transferDialog; m_transferDialog = nullptr;
    }

    // PHASE 3: Unload all cached Excel workbooks
    if (m_excelHandler) m_excelHandler->unloadAll();

    // PHASE 4: Clear mapping cards
    if (m_mappingController) {
        m_mappingController->blockSignals(true);
        m_mappingController->clearAllMappings();
        m_mappingController->blockSignals(false);
    }

    // PHASE 5: Clear the fallback period rows layout
    safeClearLayout(m_monthYearLayout);

    // PHASE 6: Clear and collapse all year cards
    for (auto it = m_yearCards.begin(); it != m_yearCards.end(); ++it) {
        YearCard* card = it.value();
        if (!card) continue;
        card->blockSignals(true);
        card->clearRows();
        card->setExpanded(false);
        card->blockSignals(false);
    }

    // PHASE 7: Reset state + UI
    m_isTransferRunning = false;
    m_isTransferPaused = false;
    m_isTransferStopRequested = false;
    if (m_btnExecuteAll) { m_btnExecuteAll->setEnabled(false); m_btnExecuteAll->blockSignals(false); }
    if (m_btnPause) { m_btnPause->setEnabled(false); m_btnPause->setText("Pause"); m_btnPause->blockSignals(false); }
    if (m_btnStop) { m_btnStop->setEnabled(false); m_btnStop->blockSignals(false); }
    if (m_progressBar) { m_progressBar->setValue(0); m_progressBar->setVisible(false); }

    // PHASE 8: Unblock + reconnect
    this->blockSignals(false);
    reconnectSignals();

    // PHASE 9: Re-expand current year
    int currentYear = QDate::currentDate().year();
    if (m_yearCards.contains(currentYear) && m_yearCards[currentYear])
        m_yearCards[currentYear]->setExpanded(true);

    updateStatusBar("Reset complete \xe2\x80\x94 ready for a fresh transfer.");
    showToast("Reset complete", ToastWidget::Success);
    qInfo() << "[RESET] === COMPLETE ===";
}

void MainWindow::safeDisconnectAll()
{
    if (m_mappingController) {
        disconnect(m_mappingController, nullptr, this, nullptr);
        disconnect(this, nullptr, m_mappingController, nullptr);
    }
    if (m_periodController) {
        disconnect(m_periodController, nullptr, this, nullptr);
        disconnect(this, nullptr, m_periodController, nullptr);
    }
    for (auto it = m_yearCards.constBegin(); it != m_yearCards.constEnd(); ++it) {
        if (it.value()) disconnect(it.value(), nullptr, this, nullptr);
    }
}

void MainWindow::safeClearLayout(QLayout* layout)
{
    if (!layout) return;
    for (int i = 0; i < layout->count(); ++i) {
        QLayoutItem* item = layout->itemAt(i);
        if (item && item->widget()) {
            item->widget()->disconnect();
            item->widget()->blockSignals(true);
            item->widget()->hide();
        }
    }
    for (int i = layout->count() - 1; i >= 0; --i) {
        QLayoutItem* item = layout->itemAt(i);
        if (!item || item->spacerItem()) continue;
        layout->takeAt(i);
        if (item->widget()) delete item->widget();
        delete item;
    }
}

void MainWindow::reconnectSignals()
{
    if (m_periodController) {
        connect(m_periodController, &PeriodController::clearUI, this, &MainWindow::clearPeriodRows);
        connect(m_periodController, &PeriodController::periodsReady, this, &MainWindow::displayPeriodRows);
    }
    if (m_mappingController) {
        connect(m_mappingController, &MappingController::rowCountChanged, this, [this](int count) {
            if (m_noMappingsLabel) m_noMappingsLabel->setVisible(count == 0);
            updateExecuteAllButton();
        });
        connect(m_mappingController, &MappingController::requestRun, this, &MainWindow::onMappingRunClicked);
        connect(m_mappingController, &MappingController::requestRemove, this, &MainWindow::onMappingRemoveClicked);
        connect(m_mappingController, &MappingController::requestEditRows, this, &MainWindow::onMappingEditRowsClicked);
        connect(m_mappingController, &MappingController::requestExportRowMap, this, &MainWindow::onExportRowMapClicked);
        connect(m_mappingController, &MappingController::requestImportRowMap, this, &MainWindow::onImportRowMapClicked);
        connect(m_mappingController, &MappingController::rowChanged, this, [this]() { updateExecuteAllButton(); });
    }
    for (auto it = m_yearCards.constBegin(); it != m_yearCards.constEnd(); ++it) {
        YearCard* card = it.value();
        if (!card) continue;
        connect(card, &YearCard::yearChecked, this, [this](int yr, bool checked) {
            if (m_periodController) {
                m_periodController->onYearToggled(yr, checked);
                if (checked) m_periodController->generatePeriodRows();
            }
        });
    }
}


void MainWindow::clearPeriodRows()
{
    if (m_monthYearLayout) {
        // Remove all items except the trailing stretch (spacerItem)
        for (int i = m_monthYearLayout->count() - 1; i >= 0; --i) {
            QLayoutItem* item = m_monthYearLayout->itemAt(i);
            if (!item) continue;
            if (item->spacerItem()) continue; // keep the stretch
            m_monthYearLayout->takeAt(i);
            if (item->widget()) {
                item->widget()->blockSignals(true);
                item->widget()->hide();
                item->widget()->deleteLater();
            }
            delete item;
        }
    }

    // Uncheck month checkboxes safely  ? they may have been deleted above via deleteLater,
    // but since we're just setting checked state on QCheckBox widgets that are still alive
    // at this point (deleteLater defers to next event loop iteration), this is safe.
    for (auto it = m_yearMonthCheckboxes.constBegin(); it != m_yearMonthCheckboxes.constEnd(); ++it) {
        if (it.value()) {
            it.value()->blockSignals(true);
            it.value()->setChecked(false);
            it.value()->blockSignals(false);
        }
    }

    updateStatusBar("All period rows cleared");
}


bool MainWindow::verifyPassword(const QString& action)
{
    Q_UNUSED(action);
    return true;
}

void MainWindow::onLoadSelectedPeriods()
{
    if (m_isLoadingPeriods || m_isLoadingRT) {
        qInfo() << "onLoadSelectedPeriods BLOCKED (re-entrant)";
        return;
    }
    m_isLoadingPeriods = true;
    blockAllYearCardSignals(true);
    m_mappingController->blockSignals(true);
    // Disable RT while normal load is running  ? mutually exclusive
    m_btnLoadRT->setEnabled(false);
    m_btnRollingTransfer->setEnabled(false);
    m_rollingChain.clear();
    // Defer cleanup until AFTER queued signals drain
    QTimer::singleShot(0, this, [this]() {
        m_mappingController->blockSignals(false);
        blockAllYearCardSignals(false);
        m_isLoadingPeriods = false;
        m_btnLoadRT->setEnabled(true);  // re-enable RT after load completes
        updateExecuteAllButton();
        qInfo() << "Load signals unblocked";
    });
    qInfo() << "=== MODEL STATE AT LOAD ===";
    if (m_periodModel) {
        const QList<YearEntry> years = m_periodModel->years();
        for (const YearEntry& entry : years) {
            int selectedCount = 0;
            for (const MonthEntry& month : entry.months) {
                if (month.selected) {
                    selectedCount++;
                }
            }
            if (selectedCount > 0) {
                qInfo() << "Year" << entry.year << "selected months" << selectedCount;
            }
        }
    }

    QVector<QPair<QString, int>> periods = getSelectedPeriods();

    if (periods.isEmpty()) {
        showToast("Please select at least one month from a year.", ToastWidget::Warning);
        return;
    }

    if (!QDir(m_sourceFolder).exists()) {
        showToast(QString("Base folder not found: %1").arg(m_sourceFolder), ToastWidget::Error);
        return;
    }

    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);
    updateStatusBar("Loading...");
    showStatusSidebar();

    m_mappingController->clearAllMappings();

    QSet<int> yearsWithOldMappings;
    QSet<int> yearsWithNewMappings;

    for (const auto& period : periods) {
        int year = period.second;
        if (year <= 2025) {
            yearsWithOldMappings.insert(year);
        } else {
            yearsWithNewMappings.insert(year);
        }
    }

    const QString jsonBase = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../JSON");
    const QString mappingsOldPath = QString("%1/mappings_old.json").arg(jsonBase);
    const QString mappingsNewPath = QString("%1/mappings.json").arg(jsonBase);
    const QString sapYtdPath = QString("%1/mappings_sap_ytd.json").arg(jsonBase);
    const QString paxPath = QString("%1/pax.json").arg(jsonBase);
    const QString staffPath = QString("%1/staff.json").arg(jsonBase);
    const QString budgetRefiPath = QString("%1/mappings_budget_refi_prev_year.json").arg(jsonBase);
    const QString paxTransferPath = QString("%1/mappings_pax_transfer.json").arg(jsonBase);
    const QString trafficMottPath = QString("%1/Traffic_mott.json").arg(jsonBase);

    // --- Load all JSON files ---
    // Cost control (year-specific JSON)
    if (!yearsWithOldMappings.isEmpty()) {
        m_mappingsManager->loadMappings(mappingsOldPath);
    }
    if (!yearsWithNewMappings.isEmpty()) {
        m_mappingsManager->loadMappings(mappingsNewPath);
    }

    // SAP YTD applies to all years
    m_mappingsManager->loadSapYtdMappings(sapYtdPath);

    // PAX, Staff, Budget/REFI apply to all years
    m_mappingsManager->loadPaxMappings(paxPath);
    m_mappingsManager->loadStaffMappings(staffPath);
    m_mappingsManager->loadBudgetRefiMappings(budgetRefiPath);
    m_mappingsManager->loadPaxTransferMappings(paxTransferPath);
    m_mappingsManager->loadTrafficMottMappings(trafficMottPath);

    // --- Add mapping rows for each period ---
    for (const auto& period : periods) {
        const QString& month = period.first;
        int year = period.second;

        // 1. Cost control rows (from mappings_old.json or mappings.json)  ? exclude sap_ytd (added separately)
        for (MappingEntry entry : m_mappingsManager->getMappingsForMonthYear(month, year)) {
            if (entry.sourceFileType == "sap_ytd") continue;
            entry.sourceJson = (year <= 2025) ? mappingsOldPath : mappingsNewPath;
            if (entry.sourceFileType == "sap") {
                entry.sourcePath = m_excelHandler->findSAPFile(m_destFolder, month, year);
            } else {
                entry.sourcePath = findCostControlPath(month, year);
            }
            m_mappingController->addMappingRow(month, year, entry);
        }

        // 2. Budget / REFI rows (from mappings_budget_refi_prev_year.json)
        for (MappingEntry entry : m_mappingsManager->getDynamicMappingsForMonthYear(month, year)) {
            entry.sourceJson = budgetRefiPath;
            entry.sourcePath = findCostControlPath(month, year);
            m_mappingController->addMappingRow(month, year, entry);
        }

        // 3. PAX rows (from pax.json)  ? all years
        if (m_mappingService) {
            for (auto sel : m_mappingService->collectPaxMappings({{month, year}})) {
                sel.entry.sourceJson = paxPath;
                sel.entry.sourcePath = m_excelHandler->findPaxFile(m_destFolder, month, year);
                m_mappingController->addMappingRow(sel.month, sel.year, sel.entry);
            }
            // 4. Staff rows (from staff.json)  ? all years
            for (auto sel : m_mappingService->collectStaffMappings({{month, year}})) {
                sel.entry.sourceJson = staffPath;
                sel.entry.sourcePath = m_excelHandler->findStaffFile(m_destFolder, year);
                m_mappingController->addMappingRow(sel.month, sel.year, sel.entry);
            }
        }

        // 5. SAP YTD rows  ? from dedicated SAP YTD mappings (all years)
        if (m_mappingsManager) {
            for (MappingEntry entry : m_mappingsManager->getSapYtdMappingsForMonthYear(month, year)) {
                entry.sourceJson = sapYtdPath;
                entry.sourcePath = m_excelHandler->findSapYtdFile(m_destFolder, month, year);
                m_mappingController->addMappingRow(month, year, entry);
            }
        }

        // 6. PAX Transfer rows (Sheet1 � TRAFFIC mott sheet, no division)
        if (m_mappingsManager) {
            for (MappingEntry entry : m_mappingsManager->getPaxTransferMappingsForMonthYear(month, year)) {
                entry.sourceJson = paxTransferPath;
                entry.sourcePath = m_excelHandler->findPaxFile(m_destFolder, month, year);
                m_mappingController->addMappingRow(month, year, entry);
            }
        }

        // 7. Traffic mott rows (Sheet1 � TRAFFIC mott sheet in cost control year/month)
        if (m_mappingsManager) {
            for (MappingEntry entry : m_mappingsManager->getTrafficMottMappingsForMonthYear(month, year)) {
                entry.sourceJson = trafficMottPath;
                entry.sourcePath = m_excelHandler->findPaxFile(m_destFolder, month, year);
                m_mappingController->addMappingRow(month, year, entry);
            }
        }
    }


    // Check all destination files exist before proceeding
    QStringList missingFiles;
    for (const auto& period : periods) {
        QString path = findCostControlPath(period.first, period.second);
        if (path.isEmpty()) {
            missingFiles << QString("%1 %2").arg(period.first).arg(period.second);
        }
    }
    if (!missingFiles.isEmpty()) {
        m_progressBar->setVisible(false);
        const QString msg = QString("File(s) not found in test/ folder:\n%1\n\nCreate the files first.")
                                .arg(missingFiles.join("\n"));
        qWarning() << msg;
        qInfo() << msg;
        qInfo() << "About to update status bar";
        updateStatusBar(QString("Missing files: %1").arg(missingFiles.join(", ")));
        qInfo() << "Status bar updated, about to show toast";
        // Don't call showToast here  ? it may trigger re-entrant events
        // Just use the status bar and status log
        qInfo() << "Returning early due to missing files";
        return;
    }

    int mappingCount = m_mappingController->mappingCount();
    if (mappingCount == 0) {
        m_progressBar->setVisible(false);
        updateStatusBar("No mappings found for selected periods");
        return;
    }

    updateStatusBar(QString("Loaded %1 mapping(s)  ? pre-loading Excel files...").arg(mappingCount));

    // Pre-load all Excel files in background using LoadWorker
    if (m_loadWorker) {
        m_loadWorker->stop();
        m_loadWorker->wait();
        delete m_loadWorker;
        m_loadWorker = nullptr;
    }

    m_loadWorker = new LoadWorker(this);
    m_loadWorker->setPeriods(periods, m_sourceFolder, m_destFolder, m_mappingsManager, m_excelHandler);

    connect(m_loadWorker, &LoadWorker::progress, this, [this](int cur, int total, const QString& msg) {
        m_progressBar->setValue(total > 0 ? (cur * 100 / total) : 0);
        updateStatusBar(msg);
        qInfo() << "[LOAD]" << msg;
    });
    connect(m_loadWorker, &LoadWorker::finished, this, [this](const QVector<LoadResult>& results) {
        m_progressBar->setVisible(false);
        int ok = 0;
        for (const auto& r : results) if (r.success) ok++;
        const QString summary = QString("Ready  ? %1/%2 files pre-loaded, %3 mapping(s)")
                                    .arg(ok).arg(results.size()).arg(m_mappingController->mappingCount());
        updateStatusBar(summary);
        qInfo() << "[LOAD]" << summary;
        updateExecuteAllButton();
    });
    connect(m_loadWorker, &LoadWorker::finished, m_loadWorker, &QObject::deleteLater);
    connect(m_loadWorker, &LoadWorker::finished, this, [this]() { m_loadWorker = nullptr; });

    m_progressBar->setRange(0, 100);
    m_loadWorker->start();
}

void MainWindow::loadMappingsForPeriods()
{
    QVector<QPair<QString, int>> periods = getSelectedPeriods();
    if (periods.isEmpty()) {
        m_noMappingsLabel->setVisible(true);
        updateStatusBar("No periods selected.");
        return;
    }

    if (m_loadService) {
        LoadService::Result result = m_loadService->loadMappingsForPeriods(periods, m_destFolder);
        if (!result.success) {
            updateStatusBar(result.message);
        }
    }

    if (m_mappingService) {
        QVector<MappingService::MappingSelection> mappings = m_mappingService->collectMappings(periods);
        for (const auto& selection : mappings) {
            m_mappingController->addMappingRow(selection.month, selection.year, selection.entry);
        }

        QVector<MappingService::MappingSelection> paxMappings = m_mappingService->collectPaxMappings(periods);
        for (const auto& selection : paxMappings) {
            m_mappingController->addMappingRow(selection.month, selection.year, selection.entry);
        }

        QVector<MappingService::MappingSelection> staffMappings = m_mappingService->collectStaffMappings(periods);
        for (const auto& selection : staffMappings) {
            m_mappingController->addMappingRow(selection.month, selection.year, selection.entry);
        }

        QVector<MappingService::MappingSelection> sapYtdMappings = m_mappingService->collectSapYtdMappings(periods);
        for (const auto& selection : sapYtdMappings) {
            m_mappingController->addMappingRow(selection.month, selection.year, selection.entry);
        }
    }

    m_noMappingsLabel->setVisible(m_mappingController->mappingCount() == 0);
}

void MainWindow::onExecuteAll()
{
    // Prevent re-entrant calls (setVisible on statusDock can trigger signals during layout)
    if (m_isTransferRunning) {
        qInfo() << "onExecuteAll BLOCKED (re-entrant, transfer already running)";
        return;
    }
    qInfo() << "onExecuteAll [1] ENTERED mappings=" << (m_mappingController ? m_mappingController->mappingCount() : -1);
    if (m_mappingController->mappingCount() == 0) {
        showToast("No mappings to execute.", ToastWidget::Warning);
        return;
    }

    // Check if at least one mapping card is checked
    bool anyChecked = false;
    for (int i = 0; i < m_mappingController->mappingCount(); ++i) {
        MappingRow* row = m_mappingController->rowAt(i);
        if (row && row->isChecked()) {
            anyChecked = true;
            break;
        }
    }
    if (!anyChecked) {
        showToast("No mappings selected. Use ? Select All or check individual cards.", ToastWidget::Warning);
        return;
    }

    showStatusSidebar();
    qInfo() << "onExecuteAll [2] sidebar shown";

    m_isTransferRunning = true;
    m_isTransferPaused = false;
    m_isTransferStopRequested = false;
    m_transferSuccessfulMappings = 0;
    m_transferFailedMappings.clear();

    // Clear previous full-sheet overrides before starting a new transfer run
    qInfo() << "onExecuteAll [3] clearing overrides";
    if (m_excelHandler) {
        for (const auto& item : m_mappingController->items()) {
            QString destKey = QString("%1_%2_cost_control").arg(item.month).arg(item.year);
            m_excelHandler->resetOverrides(destKey);
        }
    }
    qInfo() << "onExecuteAll [4] overrides cleared";
    m_btnExecuteAll->setEnabled(false);
    m_btnPause->setEnabled(true);
    m_btnStop->setEnabled(true);
    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);

    if (m_transferDialog) {
        m_transferDialog->deleteLater();
        m_transferDialog = nullptr;
    }
    m_transferDialog = new QProgressDialog("Transferring data...", "Cancel", 0, 100, this);
    m_transferDialog->setWindowModality(Qt::NonModal);
    m_transferDialog->setWindowFlags(m_transferDialog->windowFlags() | Qt::Tool);
    m_transferDialog->setMinimumDuration(500);
    m_transferDialog->setAutoClose(false);
    m_transferDialog->setAutoReset(false);
    m_transferDialog->setValue(0);
    connect(m_transferDialog, &QProgressDialog::canceled, this, &MainWindow::onStopTransfer);
    qInfo() << "onExecuteAll [5] progress dialog created";

    if (m_transferWorker) {
        m_transferWorker->stop();
        m_transferWorker->wait();
        delete m_transferWorker;
        m_transferWorker = nullptr;
    }
    qInfo() << "onExecuteAll [6] old worker cleaned up";

    QVector<TransferMapping> transferMappings;
    QVector<MappingItem> items = m_mappingController->items();
    transferMappings.reserve(items.size());
    qInfo() << "onExecuteAll [7] building transfer list from" << items.size() << "items";
    for (int i = 0; i < items.size(); ++i) {
        const MappingItem& item = items[i];
        MappingRow* row = m_mappingController->rowAt(i);
        if (!row || !row->isChecked()) {
            continue; // only transfer selected cards
        }

        TransferMapping tm;
        tm.entry = item.entry;
        tm.year  = item.year;
        tm.month = item.month;
        tm.rowIndex = i;

        // Ensure latest UI state for copy-full-sheet option
        tm.entry.copyFullSheet = row->isCopyFullSheet();
        tm.entry.customSheetName = row->getCustomSheetName();
        tm.entry.insertAfterSheet = row->getInsertAfterSheet();

        tm.destPath = findCostControlPath(item.month, item.year);
        if (tm.destPath.isEmpty()) {
            m_transferFailedMappings.append(QString("%1 %2").arg(item.month).arg(item.year));
            continue;
        }
        transferMappings.append(tm);
    }

    qInfo() << "onExecuteAll [8] transfer list built, size=" << transferMappings.size();
    m_transferTotalMappings = transferMappings.size();
    if (transferMappings.isEmpty()) {
        m_isTransferRunning = false;
        m_btnExecuteAll->setEnabled(true);
        m_btnPause->setEnabled(false);
        m_btnStop->setEnabled(false);
        m_progressBar->setVisible(false);
        showToast("No selected mappings to execute.", ToastWidget::Warning);
        return;
    }

    // Check destination files are not locked/open
    // Skip check for files already loaded in cache  ? QZipReader may hold them open,
    // causing false positives. The save phase handles locking properly.
    qInfo() << "onExecuteAll [9] checking file locks";
    QSet<QString> checkedPaths;
    for (const TransferMapping& tm : transferMappings) {
        QString destPath = tm.destPath.isEmpty() ? findCostControlPath(tm.month, tm.year) : tm.destPath;
        if (checkedPaths.contains(destPath)) continue;
        checkedPaths.insert(destPath);

        if (!QFile::exists(destPath)) continue;

        // Check if file is locked by another process (e.g. open in Excel)
        // Use QFile::rename as a non-destructive exclusive lock test
        QFile destFile(destPath);
        bool locked = false;
        if (destFile.open(QIODevice::ReadWrite)) {
            destFile.close();
        } else {
            locked = true;
        }

        if (locked) {
            const QString summary = QString("Destination file is open or locked: %1").arg(destPath);
            updateStatusBar(summary);
            qWarning() << summary;
            m_isTransferRunning = false;
            m_btnExecuteAll->setEnabled(true);
            m_btnPause->setEnabled(false);
            m_btnStop->setEnabled(false);
            m_progressBar->setVisible(false);
            if (m_transferDialog) {
                m_transferDialog->reset();
                m_transferDialog->hide();
                m_transferDialog->deleteLater();
                m_transferDialog = nullptr;
            }
            showToast(QString("? File is open in Excel  ? please close it first:\n%1")
                          .arg(QFileInfo(destPath).fileName()), ToastWidget::Warning, 8000);
            return;
        }
    }
    qInfo() << "onExecuteAll [10] file locks OK, creating worker";
    m_transferWorker = new TransferWorker(this);
    m_transferWorker->setMappings(transferMappings, m_transferService, m_destFolder);

    connect(m_transferWorker, &TransferWorker::progress, this, &MainWindow::onTransferProgress);
    connect(m_transferWorker, &TransferWorker::rowDone, this, &MainWindow::onTransferRowDone);
    connect(m_transferWorker, &TransferWorker::finished, this, &MainWindow::onTransferFinished);

    qInfo() << "onExecuteAll [11] starting worker";
    m_transferWorker->start();
    qInfo() << "onExecuteAll [12] worker started";
}

void MainWindow::onTransferRowDone(int index, bool success, const QString& message)
{
    MappingRow* row = m_mappingController->rowAt(index);
    if (row) {
        row->setTransferStatus(success, message);
    }

    if (success) {
        m_transferSuccessfulMappings++;
    } else {
        QVector<MappingItem> items = m_mappingController->items();
        if (index >= 0 && index < items.size()) {
            const MappingItem& item = items[index];
            QString mappingName = QString("%1 %2").arg(item.month).arg(item.year);
            m_transferFailedMappings.append(mappingName);
        }
    }

    updateStatusBar(message);
}

void MainWindow::onTransferFinished(int totalCells, int executed, const QStringList& skipped)
{
    qInfo() << "onTransferFinished [1] ENTERED";
    const bool wasStopRequested = m_isTransferStopRequested;
    m_isTransferRunning = false;
    m_isTransferStopRequested = false;
    m_btnExecuteAll->setEnabled(true);
    m_btnPause->setEnabled(false);
    m_btnPause->setText("Pause");
    m_btnStop->setEnabled(false);
    m_isTransferPaused = false;
    m_progressBar->setVisible(false);

    // Close progress dialog safely  ? it may have been auto-closed or never shown
    if (m_transferDialog) {
        m_transferDialog->reset(); // safe even if never shown
        m_transferDialog->hide();
        m_transferDialog->deleteLater();
        m_transferDialog = nullptr;
    }
    qInfo() << "onTransferFinished [2] dialog closed";

    if (m_transferWorker) {
        disconnect(m_transferWorker, nullptr, this, nullptr);
        // Worker already finished (we're in its finished signal)  ? just schedule deletion
        m_transferWorker->deleteLater();
        m_transferWorker = nullptr;
    }
    qInfo() << "onTransferFinished [3] worker cleaned up";

    QString result = wasStopRequested
        ? QString("Transfer stopped safely after finishing the current sheet (%1 cells from %2 mappings)").arg(totalCells).arg(executed)
        : QString("Transferred %1 cells from %2 mappings").arg(totalCells).arg(executed);
    if (!skipped.isEmpty()) {
        result += QString(". Skipped: %1").arg(skipped.join(", "));
    }

    updateStatusBar(result);
    qInfo() << "onTransferFinished [4] status updated";
    if (!m_transferFailedMappings.isEmpty()) {
        const QString summary = "Some mappings failed to save. Check the log and ensure the destination file is not open.";
        updateStatusBar(summary);
        qWarning() << summary;
        showStatusSidebar();
    }

    QStringList summaryLines;
    summaryLines << "=== TRANSFER COMPLETE ===";
    summaryLines << QString("Total mappings: %1").arg(m_transferTotalMappings);
    summaryLines << QString("Successful mappings: %1").arg(m_transferSuccessfulMappings);
    summaryLines << QString("Failed mappings: %1").arg(m_transferFailedMappings.size());
    summaryLines << QString("Cells transferred: %1").arg(totalCells);
    summaryLines << QString("Mappings executed: %1").arg(executed);
    if (!skipped.isEmpty()) {
        summaryLines << QString("Skipped: %1").arg(skipped.join(", "));
    }
    if (!m_transferFailedMappings.isEmpty()) {
        summaryLines << QString("Failed list: %1").arg(m_transferFailedMappings.join(", "));
    }
    const QString summaryText = summaryLines.join("\n");
    qInfo().noquote() << summaryText;
    updateStatusBar(wasStopRequested
        ? QString("Transfer stopped safely after current sheet: %1 cells").arg(totalCells)
        : QString("Transfer complete: %1 cells").arg(totalCells));
    qInfo() << "onTransferFinished [5] RETURNING";
}

void MainWindow::onStopTransfer()
{
    if (!m_transferWorker || !m_isTransferRunning) {
        return;
    }

    m_isTransferStopRequested = true;
    m_isTransferPaused = false;
    m_transferWorker->stop();

    // Do NOT violently kill the worker thread. Let it finish the current mapping/sheet,
    // then it will stop cleanly at the next safe boundary.
    m_btnPause->setEnabled(false);
    m_btnPause->setText("Pause");
    m_btnStop->setEnabled(false);
    updateStatusBar("Stop requested  ? finishing current sheet, then stopping safely...");
}

void MainWindow::onPauseTransfer()
{
    if (!m_transferWorker || !m_isTransferRunning || m_isTransferStopRequested) {
        return;
    }

    m_isTransferPaused = !m_isTransferPaused;
    if (m_isTransferPaused) {
        m_transferWorker->pause();
        m_btnPause->setText("Resume");
        updateStatusBar("Pause requested  ? will pause after current sheet finishes...");
    } else {
        m_transferWorker->resume();
        m_btnPause->setText("Pause");
        updateStatusBar("Transfer resumed");
    }
}

void MainWindow::onLoadRT()
{
    if (m_isLoadingRT || m_isLoadingPeriods) {
        qInfo() << "onLoadRT BLOCKED (re-entrant)";
        return;
    }
    if (!m_rollingService) return;
    m_isLoadingRT = true;

    QVector<QPair<QString, int>> periods = getSelectedPeriods();
    if (periods.isEmpty()) {
        showToast("Select months for Rolling Transfer first.", ToastWidget::Warning);
        return;
    }

    // Build the chain
    m_rollingChain = m_rollingService->buildChain(m_destFolder, periods);

    // Validate: only the FIRST step's source must exist on disk.
    // Subsequent steps use the output of the previous step (created during execution).
    QStringList missing;
    if (!m_rollingChain.isEmpty()) {
        const RollingStep& first = m_rollingChain.first();
        if (first.inputPath.isEmpty() || !QFile::exists(first.inputPath)) {
            missing << QString("Previous month file not found:\n  %1")
                           .arg(first.inputPath.isEmpty() ? "path not found" : first.inputPath);
        }
    }
    if (!missing.isEmpty()) {
        m_rollingChain.clear();
        m_btnRollingTransfer->setEnabled(false);
        const QString msg = QString("Rolling Transfer cancelled  ? source file(s) not found:\n%1").arg(missing.join("\n"));
        qWarning() << msg;
        qInfo() << msg;
        updateStatusBar("RT Load failed  ? source file not found");
        return;
    }

    // Warn if destination files already exist (will be overwritten)
    QStringList existing;
    for (const RollingStep& s : m_rollingChain) {
        if (QFile::exists(s.outputPath))
            existing << QString("%1 %2").arg(s.month).arg(s.year);
    }
    if (!existing.isEmpty()) {
        qWarning() << "RT: destination files already exist and will be overwritten:" << existing;
        qInfo() << "RT WARNING: These files will be OVERWRITTEN:" << existing.join(", ");
        // Don't use showToast here  ? it can trigger re-entrant events
        updateStatusBar(QString("WARNING: Will overwrite: %1").arg(existing.join(", ")));
    }

    // Load mappings for selected periods so cards are visible in the UI
    m_mappingController->clearAllMappings();
    QSet<int> yearsWithOldMappings;
    QSet<int> yearsWithNewMappings;
    for (const auto& period : periods) {
        int year = period.second;
        if (year <= 2025) {
            yearsWithOldMappings.insert(year);
        } else {
            yearsWithNewMappings.insert(year);
        }
    }

    const QString jsonBase = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../JSON");
    const QString mappingsOldPath = QString("%1/mappings_old.json").arg(jsonBase);
    const QString mappingsNewPath = QString("%1/mappings.json").arg(jsonBase);
    const QString sapYtdPath = QString("%1/mappings_sap_ytd.json").arg(jsonBase);
    const QString paxPath = QString("%1/pax.json").arg(jsonBase);
    const QString staffPath = QString("%1/staff.json").arg(jsonBase);
    const QString budgetRefiPath = QString("%1/mappings_budget_refi_prev_year.json").arg(jsonBase);

    if (!yearsWithOldMappings.isEmpty()) {
        m_mappingsManager->loadMappings(mappingsOldPath);
    }
    if (!yearsWithNewMappings.isEmpty()) {
        m_mappingsManager->loadMappings(mappingsNewPath);
    }

    m_mappingsManager->loadSapYtdMappings(sapYtdPath);
    m_mappingsManager->loadPaxMappings(paxPath);
    m_mappingsManager->loadStaffMappings(staffPath);
    m_mappingsManager->loadBudgetRefiMappings(budgetRefiPath);

    for (const auto& period : periods) {
        const QString& month = period.first;
        int year = period.second;

        for (MappingEntry entry : m_mappingsManager->getMappingsForMonthYear(month, year)) {
            if (entry.sourceFileType == "sap_ytd") continue;
            entry.sourceJson = (year <= 2025) ? mappingsOldPath : mappingsNewPath;
            if (entry.sourceFileType == "sap") {
                entry.sourcePath = m_excelHandler->findSAPFile(m_destFolder, month, year);
            } else {
                entry.sourcePath = findCostControlPath(month, year);
            }
            m_mappingController->addMappingRow(month, year, entry);
        }

        for (MappingEntry entry : m_mappingsManager->getDynamicMappingsForMonthYear(month, year)) {
            entry.sourceJson = budgetRefiPath;
            entry.sourcePath = findCostControlPath(month, year);
            m_mappingController->addMappingRow(month, year, entry);
        }

        if (m_mappingService) {
            for (auto sel : m_mappingService->collectPaxMappings({{month, year}})) {
                sel.entry.sourceJson = paxPath;
                sel.entry.sourcePath = m_excelHandler->findPaxFile(m_destFolder, month, year);
                m_mappingController->addMappingRow(sel.month, sel.year, sel.entry);
            }
            for (auto sel : m_mappingService->collectStaffMappings({{month, year}})) {
                sel.entry.sourceJson = staffPath;
                sel.entry.sourcePath = m_excelHandler->findStaffFile(m_destFolder, year);
                m_mappingController->addMappingRow(sel.month, sel.year, sel.entry);
            }
        }

        if (m_mappingsManager) {
            for (MappingEntry entry : m_mappingsManager->getSapYtdMappingsForMonthYear(month, year)) {
                entry.sourceJson = sapYtdPath;
                entry.sourcePath = m_excelHandler->findSapYtdFile(m_destFolder, month, year);
                m_mappingController->addMappingRow(month, year, entry);
            }
        }
    }

    m_noMappingsLabel->setVisible(m_mappingController->mappingCount() == 0);

    // Disable normal Load while RT is armed  ? mutually exclusive
    m_btnLoad->setEnabled(false);
    m_btnExecuteAll->setEnabled(false);

    // Enable Execute RT button
    m_btnRollingTransfer->setEnabled(true);

    QStringList stepList;
    for (const auto& s : m_rollingChain)
        stepList << QString("%1 %2").arg(s.month).arg(s.year);
    updateStatusBar(QString("RT Ready: %1  ? click Execute RT to start").arg(stepList.join(" � ")));
    qInfo() << "RT chain built:" << stepList.join(" � ");

    m_isLoadingRT = false;
}

void MainWindow::onRollingTransfer()
{
    if (!m_rollingService) return;
    if (m_rollingChain.isEmpty()) {
        showToast("Click Load RT first.", ToastWidget::Warning);
        return;
    }

    // Show status sidebar safely (blockSignals prevents re-entrant events)
    if (m_statusDock) {
        m_statusDock->blockSignals(true);
        m_statusDock->setVisible(true);
        m_statusDock->blockSignals(false);
    }

    disconnect(m_rollingService, nullptr, this, nullptr);

    connect(m_rollingService, &RollingTransferService::chainProgress,
            this, [this](int current, int total, const QString& msg) {
        m_progressBar->setVisible(true);
        m_progressBar->setValue(total > 0 ? (current * 100 / total) : 0);
        updateStatusBar(msg);
    });

    connect(m_rollingService, &RollingTransferService::chainFinished,
            this, [this](const RollingResult& result) {
        m_progressBar->setVisible(false);
        if (m_rtDialog) {
            m_rtDialog->reset();
            m_rtDialog->hide();
            m_rtDialog->deleteLater();
            m_rtDialog = nullptr;
        }
        m_btnRollingTransfer->setEnabled(false);
        m_btnStop->setEnabled(false);
        m_btnLoad->setEnabled(true);       // re-enable normal Load
        m_btnLoadRT->setEnabled(true);     // re-enable Load RT
        m_rollingChain.clear();
        updateExecuteAllButton();           // re-enable Execute All if cards checked
        updateStatusBar(QString("RT complete: %1/%2 months, %3 cells")
            .arg(result.successfulMonths).arg(result.totalMonths).arg(result.totalCells));
        qInfo() << "=== RT COMPLETE ===" << result.successfulMonths << "/" << result.totalMonths
                 << "months," << result.totalCells << "cells";
        if (!result.errors.isEmpty())
            qInfo() << "RT Errors:" << result.errors;
    });

    // Check destination files for RT are not locked/open
    QSet<QString> checkedPaths;
    for (const RollingStep& step : m_rollingChain) {
        const QString destPath = step.outputPath;
        if (destPath.isEmpty() || checkedPaths.contains(destPath)) continue;
        checkedPaths.insert(destPath);
        QFile destFile(destPath);
        if (!destFile.open(QIODevice::ReadWrite)) {
            const QString summary = QString("Destination file is open or locked: %1").arg(destPath);
            updateStatusBar(summary);
            qWarning() << summary;
            showToast(QString("File locked: %1").arg(QFileInfo(destPath).fileName()), ToastWidget::Warning);
            return;
        }
        destFile.close();
    }

    m_btnExecuteAll->setEnabled(false);
    m_btnRollingTransfer->setEnabled(false);
    m_btnStop->setEnabled(true);
    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);

    if (m_rtDialog) {
        m_rtDialog->deleteLater();
        m_rtDialog = nullptr;
    }
    m_rtDialog = new QProgressDialog("Rolling Transfer running...", "Cancel", 0, 100, this);
    m_rtDialog->setWindowModality(Qt::NonModal);
    m_rtDialog->setWindowFlags(m_rtDialog->windowFlags() | Qt::Tool);
    m_rtDialog->setMinimumDuration(300);
    m_rtDialog->setAutoClose(false);
    m_rtDialog->setAutoReset(false);
    m_rtDialog->setValue(0);
    connect(m_rtDialog, &QProgressDialog::canceled, this, &MainWindow::onStopTransfer);

    const QString jsonBase = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../JSON");

    // Build allEntriesByMonth from checked cards (or all items if none checked)
    QVector<MappingItem> checkedItems = m_mappingController->checkedItems();
    QVector<MappingItem> allItems = checkedItems.isEmpty()
        ? m_mappingController->items()
        : checkedItems;

    QMap<QString, QVector<MappingEntry>> allEntriesByMonth;
    for (const MappingItem& item : allItems) {
        const QString key = QString("%1_%2").arg(item.month).arg(item.year);
        allEntriesByMonth[key].append(item.entry);
    }

    // If no cards were loaded (RT clicked without Load first), force-load all mapping types
    if (allEntriesByMonth.isEmpty() && m_mappingsManager) {
        qInfo() << "RT: no cards loaded  ? force-loading all mappings for chain periods";
        QVector<QPair<QString, int>> chainPeriods;
        QSet<int> yearsWithOldMappings;
        QSet<int> yearsWithNewMappings;
        for (const RollingStep& s : m_rollingChain) {
            chainPeriods.append({s.month, s.year});
            if (s.year <= 2025) {
                yearsWithOldMappings.insert(s.year);
            } else {
                yearsWithNewMappings.insert(s.year);
            }
        }

        const QString mappingsOldPath = QString("%1/mappings_old.json").arg(jsonBase);
        const QString mappingsNewPath = QString("%1/mappings.json").arg(jsonBase);
        const QString sapYtdPath = QString("%1/mappings_sap_ytd.json").arg(jsonBase);
        const QString paxPath = QString("%1/pax.json").arg(jsonBase);
        const QString staffPath = QString("%1/staff.json").arg(jsonBase);
        const QString budgetRefiPath = QString("%1/mappings_budget_refi_prev_year.json").arg(jsonBase);
        const QString trafficMottPath = QString("%1/Traffic_mott.json").arg(jsonBase);

        if (!yearsWithOldMappings.isEmpty()) {
            m_mappingsManager->loadMappings(mappingsOldPath);
        }
        if (!yearsWithNewMappings.isEmpty()) {
            m_mappingsManager->loadMappings(mappingsNewPath);
        }

        m_mappingsManager->loadSapYtdMappings(sapYtdPath);
        m_mappingsManager->loadPaxMappings(paxPath);
        m_mappingsManager->loadStaffMappings(staffPath);
        m_mappingsManager->loadBudgetRefiMappings(budgetRefiPath);
        m_mappingsManager->loadTrafficMottMappings(trafficMottPath);

        for (const auto& period : chainPeriods) {
            const QString& month = period.first;
            int year = period.second;
            const QString key = QString("%1_%2").arg(month).arg(year);

            for (MappingEntry entry : m_mappingsManager->getMappingsForMonthYear(month, year)) {
                if (entry.sourceFileType == "sap_ytd") {
                    continue;
                }
                allEntriesByMonth[key].append(entry);
            }

            for (MappingEntry entry : m_mappingsManager->getDynamicMappingsForMonthYear(month, year)) {
                allEntriesByMonth[key].append(entry);
            }

            if (m_mappingService) {
                for (const auto& sel : m_mappingService->collectPaxMappings({{month, year}}))
                    allEntriesByMonth[key].append(sel.entry);
                for (const auto& sel : m_mappingService->collectStaffMappings({{month, year}}))
                    allEntriesByMonth[key].append(sel.entry);
            }

            for (MappingEntry entry : m_mappingsManager->getTrafficMottMappingsForMonthYear(month, year)) {
                entry.sourceJson = trafficMottPath;
                entry.sourcePath = m_excelHandler->findPaxFile(m_destFolder, month, year);
                allEntriesByMonth[key].append(entry);
            }
        }

        qInfo() << "RT: force-loaded entries:" << [&]() {
            QStringList info;
            for (auto it = allEntriesByMonth.constBegin(); it != allEntriesByMonth.constEnd(); ++it)
                info << QString("%1(%2)").arg(it.key()).arg(it.value().size());
            return info.join(", ");
        }();
    }

    qInfo() << "RT: using entries for months:" << allEntriesByMonth.keys();
    RollingWorker* worker = new RollingWorker(m_rollingService, m_rollingChain, m_destFolder, jsonBase, allEntriesByMonth, this);
    connect(worker, &RollingWorker::finished, this, [this, worker](const RollingResult&) {
        worker->deleteLater();
    });
    worker->start();
}



void MainWindow::onYearCheckboxChanged(Qt::CheckState state)
{
    Q_UNUSED(state);
    updateStatusBar("Year selection changed. Click Generate to create period rows.");
}

void MainWindow::displayPeriodRows(const QList<YearEntry>& periods)
{
    // Use the EXISTING YearCard in m_yearCards  ? no duplicate cards created.
    // The year card is already visible in the selector; we just add a PeriodRow
    // to it so the month checkboxes appear inside the dropdown.
    for (const YearEntry& entry : periods) {
        YearCard* card = m_yearCards.value(entry.year, nullptr);
        if (!card) {
            // Fallback: year not in selector (shouldn't happen), create one
            card = new YearCard(entry.year, m_monthYearSelector);
            m_yearCards[entry.year] = card;
            m_monthYearLayout->addWidget(card);
        }

        // Remove any existing PeriodRows for this year first
        for (PeriodRow* existing : card->rows()) {
            card->removeRow(existing);
            existing->deleteLater();
        }

        // Add fresh PeriodRow with current month selections
        PeriodRow* row = new PeriodRow(entry.year, card);
        for (const MonthEntry& month : entry.months) {
            row->setMonthChecked(month.name, month.selected);
        }
        connect(row, &PeriodRow::monthSelected, this, [this](int year, const QString& month, bool checked) {
            if (m_isLoadingPeriods || m_isLoadingRT) {
                qInfo() << "onMonthToggled BLOCKED during load" << month << year;
                return;
            }
            if (m_periodController) {
                m_periodController->onMonthToggled(year, month, checked);
            }
        });
        card->addRow(row);

        // Auto-expand the card so months are visible
        card->setExpanded(true);
    }
}

void MainWindow::onMonthCheckboxChanged(int row, int col, int year, const QString& month)
{
    if (m_isLoadingPeriods || m_isLoadingRT) {
        qInfo() << "onMonthCheckboxChanged BLOCKED during load";
        return;
    }
    Q_UNUSED(row);
    Q_UNUSED(col);
    Q_UNUSED(year);
    Q_UNUSED(month);
}

void MainWindow::onMappingRunClicked(int index)
{
    MappingRow* row = m_mappingController->rowAt(index);
    if (!row) return;
    MappingEntry entry = row->getMapping();
    entry.copyFullSheet = row->isCopyFullSheet();
    entry.customSheetName = row->getCustomSheetName();
    entry.insertAfterSheet = row->getInsertAfterSheet();
    int cells = transferData(entry, row->getYear());

    updateStatusBar(QString("Transferred %1 cells").arg(cells));

    if (cells > 0) {
        showToast(QString("Transferred %1 cells successfully.").arg(cells), ToastWidget::Success);
    } else {
        showToast("No cells were transferred.", ToastWidget::Error);
    }
}

void MainWindow::onMappingRemoveClicked(int index)
{
    m_mappingController->removeMappingRow(index);
}

void MainWindow::onMappingEditRowsClicked(int index)
{
    MappingRow* row = m_mappingController->rowAt(index);
    if (!row) return;

    EditRowsDialog dialog(this,
        row->getSourceRows(),
        row->getDestRows(),
        row->getMapping().sourceSheetTemplate,
        row->getMapping().sourceColumn,
        row->getMapping().destSheet,
        row->getMapping().destColumn);

    if (dialog.exec() == QDialog::Accepted) {
        MappingEntry entry = row->getMapping();
        entry.sourceRows = dialog.getSourceRows();
        entry.destRows = dialog.getDestRows();
        entry.copyFullSheet = row->isCopyFullSheet();
        entry.customSheetName = row->getCustomSheetName();
        entry.insertAfterSheet = row->getInsertAfterSheet();

        row->setSourceRows(entry.sourceRows);
        row->setDestRows(entry.destRows);

        updateStatusBar("Mapping updated");
    }
}

void MainWindow::onExportRowMapClicked(int index)
{
    MappingRow* row = m_mappingController->rowAt(index);
    if (!row) return;

    MappingEntry entry = row->getMapping();
    entry.copyFullSheet = row->isCopyFullSheet();
    entry.customSheetName = row->getCustomSheetName();
    entry.insertAfterSheet = row->getInsertAfterSheet();

    QString fileName = QFileDialog::getSaveFileName(this,
        tr("Export RowMap JSON"),
        QString("rowmap_%1_%2.json").arg(entry.month).arg(row->getYear()),
        tr("JSON Files (*.json);;All Files (*)"));

    if (fileName.isEmpty()) return;

    QJsonObject jsonObj;
    for (auto it = entry.rowMap.constBegin(); it != entry.rowMap.constEnd(); ++it) {
        int destRow = it.key();
        const QVector<int>& srcRows = it.value();
        if (srcRows.size() == 1) {
            jsonObj[QString::number(destRow)] = srcRows.first();
        } else {
            QJsonArray arr;
            for (int src : srcRows) {
                arr.append(src);
            }
            jsonObj[QString::number(destRow)] = arr;
        }
    }

    QJsonDocument doc(jsonObj);
    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << doc.toJson(QJsonDocument::Indented);
        file.close();
        showToast("RowMap exported successfully", ToastWidget::Success);
    } else {
        const QString summary = QString("RowMap export failed: %1").arg(file.errorString());
        updateStatusBar(summary);
        qWarning() << summary;
        showStatusSidebar();
    }
}

void MainWindow::onImportRowMapClicked(int index)
{
    MappingRow* row = m_mappingController->rowAt(index);
    if (!row) return;

    QString fileName = QFileDialog::getOpenFileName(this,
        tr("Import RowMap JSON"),
        QString(),
        tr("JSON Files (*.json);;All Files (*)"));

    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString summary = QString("RowMap import failed: %1").arg(file.errorString());
        updateStatusBar(summary);
        qWarning() << summary;
        showStatusSidebar();
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    RowMapValidatorDialog dialog(this);
    dialog.setJsonText(QString::fromUtf8(data));

    if (dialog.exec() == QDialog::Accepted && dialog.isValid()) {
        MappingEntry entry = row->getMapping();

        QJsonObject jsonObj = dialog.getValidatedJson();
        entry.rowMap.clear();

        for (auto it = jsonObj.constBegin(); it != jsonObj.constEnd(); ++it) {
            int destRow = it.key().toInt();
            QJsonValue value = it.value();
            QVector<int> srcRows;

            if (value.isDouble()) {
                srcRows.append(value.toInt());
            } else if (value.isArray()) {
                QJsonArray arr = value.toArray();
                for (int i = 0; i < arr.size(); ++i) {
                    srcRows.append(arr[i].toInt());
                }
            }

            entry.rowMap[destRow] = srcRows;
        }

        showToast("RowMap imported successfully", ToastWidget::Success);
    }
}

void MainWindow::onTransferProgress(int current, int total, const QString& message)
{
    int percent = total > 0 ? (current * 100) / total : 0;
    m_progressBar->setValue(percent);
    if (m_transferDialog) {
        m_transferDialog->setValue(percent);
        m_transferDialog->setLabelText(message);
    }
    updateStatusBar(message);
}


void MainWindow::onTransferError(const QString& error)
{
    const QString summary = QString("Transfer error: %1").arg(error);
    updateStatusBar(summary);
    qWarning() << summary;
    showStatusSidebar();
}

void MainWindow::blockAllYearCardSignals(bool block)
{
    // Block all YearCard and their child checkboxes to prevent
    // layout-triggered signals during mapping load
    const QList<YearCard*> cards = m_monthYearSelector->findChildren<YearCard*>();
    for (YearCard* card : cards) {
        card->blockSignals(block);
        const QList<QCheckBox*> checkboxes = card->findChildren<QCheckBox*>();
        for (QCheckBox* cb : checkboxes)
            cb->blockSignals(block);
    }
    // Also block period rows
    const QList<PeriodRow*> rows = m_monthYearSelector->findChildren<PeriodRow*>();
    for (PeriodRow* row : rows) {
        row->blockSignals(block);
        const QList<QCheckBox*> checkboxes = row->findChildren<QCheckBox*>();
        for (QCheckBox* cb : checkboxes)
            cb->blockSignals(block);
    }
}

void MainWindow::updateExecuteAllButton()
{
    if (!m_btnExecuteAll || !m_mappingController || m_isTransferRunning) return;
    bool anyChecked = false;
    for (int i = 0; i < m_mappingController->mappingCount(); ++i) {
        MappingRow* row = m_mappingController->rowAt(i);
        if (row && row->isChecked()) {
            anyChecked = true;
            break;
        }
    }
    m_btnExecuteAll->setEnabled(anyChecked);
    m_btnExecuteAll->setToolTip(anyChecked ? "" : "Select at least one mapping card to enable");
}

void MainWindow::showStatusSidebar()
{
    if (!m_statusDock) return;
    // Block signals to prevent cascading checkbox/toggle events during dock show
    m_statusDock->blockSignals(true);
    m_statusDock->setVisible(true);
    m_statusDock->blockSignals(false);
}

void MainWindow::appendStatusLine(const QString& line)
{
    if (!m_statusText) return;
    m_statusText->append(line);
    m_statusText->verticalScrollBar()->setValue(m_statusText->verticalScrollBar()->maximum());
}

void MainWindow::handleQtMessage(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    Q_UNUSED(context);

    QString prefix;
    switch (type) {
        case QtDebugMsg:    prefix = "[STATUS]"; break;
        case QtInfoMsg:     prefix = "[INFO]";  break;
        case QtWarningMsg:  prefix = "[WARN]";  break;
        case QtCriticalMsg: prefix = "[ERROR]"; break;
        case QtFatalMsg:    prefix = "[FATAL]"; break;
    }
    const QString line = prefix + " " + msg;

    // Write to crash-safe log file immediately (unbuffered)  ? survives app crash
    static QFile s_logFile(QDir::tempPath() + "/exceltransfer_crash.log");
    static bool s_logOpen = s_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    if (s_logOpen) {
        QTextStream stream(&s_logFile);
        stream << line << "\n";
        s_logFile.flush(); // flush immediately so last line is visible after crash
    }

    // Also update UI sidebar on main thread
    MainWindow* inst = MainWindow::s_instance;
    if (!inst) return;
    QMetaObject::invokeMethod(inst, [inst, line]() {
        inst->appendStatusLine(line);
    }, Qt::QueuedConnection);
}

void MainWindow::onLoadProgress(int current, int total, const QString& message)
{
    m_progressBar->setValue((current * 100) / total);
    updateStatusBar(message);
}

void MainWindow::onLoadComplete(bool success, const QString& message)
{
    const QString summary = success
        ? "Load complete"
        : QString("Load failed: %1").arg(message);
    updateStatusBar(summary);
    qInfo() << summary;
    updateExecuteAllButton();
}

void MainWindow::onLoadError(const QString& error)
{
    const QString summary = QString("Load error: %1").arg(error);
    updateStatusBar(summary);
    qWarning() << summary;
}

void MainWindow::onBreakLinksComplete(const BreakLinks::Result& result)
{
    if (result.success) {
        qInfo() << "External links broken successfully";
    } else {
        qWarning() << "Failed to break external links:" << result.error;
    }
}

void MainWindow::registerYearMonthCheckbox(const QPair<int, QString>& key, QCheckBox* cb)
{
    registerYearMonthCheckboxInternal(key, cb);
}

void MainWindow::registerYearMonthCheckboxInternal(const QPair<int, QString>& key, QCheckBox* cb)
{
    if (!cb) return;
    m_yearMonthCheckboxes[key] = cb;
    connect(cb, &QCheckBox::toggled, this, [this, key](bool checked) {
        if (m_periodController) {
            m_periodController->onMonthToggled(key.first, key.second, checked);
        }
    });
}

QVector<QPair<QString, int>> MainWindow::getSelectedPeriods() const
{
    if (!m_periodModel) {
        return {};
    }
    QVector<QPair<QString, int>> periods;
    const QList<YearEntry> years = m_periodModel->generateSelectedPeriods();
    for (const YearEntry& entry : years) {
        for (const MonthEntry& month : entry.months) {
            periods.append(qMakePair(month.name, entry.year));
        }
    }
    return periods;
}

int MainWindow::transferData(const MappingEntry& entry, int year)
{
    if (!m_transferService) {
        return 0;
    }
    TransferService::Result result = m_transferService->transferEntry(entry, year,
                                                                      QString("%1_%2_cost_control").arg(entry.month).arg(year),
                                                                      findCostControlPath(entry.month, year),
                                                                      m_destFolder);
    return result.cellsTransferred;
}

QString MainWindow::findMonthNum(const QString& monthName) const
{
    return MONTH_TO_NUM.value(monthName, "01");
}

QString MainWindow::findCostControlPath(const QString& month, int year) const
{
    QString monthNum = findMonthNum(month);
    QString basePath = m_destFolder;

    QString costControlPath = m_excelHandler->findCostControlFile(basePath, month, year);
    return costControlPath;
}

QString MainWindow::findSAPPath(const QString& month, int year) const
{
    QString monthNum = findMonthNum(month);
    QString basePath = m_destFolder;

    QString sapPath = QString("%1/%2/%3/SAP export monthly").arg(basePath).arg(year).arg(monthNum);
    return sapPath;
}

int MainWindow::handleSapYtdTransfer(const MappingEntry& entry, int year, const QString& destKey, const QString& destFilePath)
{
    Q_UNUSED(destKey);
    Q_UNUSED(destFilePath);
    QString srcKey = QString("%1_%2_sap_ytd").arg(entry.month).arg(year);
    if (!m_loadedWorkbooks.contains(srcKey)) {
        return 0;
    }

    const QString sourceSheet = entry.sourceSheetTemplate.isEmpty() ? "Report" : entry.sourceSheetTemplate;
    const QString destSheet = entry.destSheet.isEmpty() ? "MZLZ Consolidated" : entry.destSheet;
    const QString sourceColumn = entry.sourceColumn.isEmpty() ? "C" : entry.sourceColumn;
    const QString destColumn = entry.destColumn.isEmpty() ? "JG" : entry.destColumn;

    int handled = 0;

    for (auto it = entry.rowMap.constBegin(); it != entry.rowMap.constEnd(); ++it) {
        int destRow = it.key();
        const QVector<int>& srcRows = it.value();
        double total = 0.0;
        bool hasValue = false;

        if (srcRows.size() == 1 && srcRows.first() == 0) {
            total = 0.0;
            hasValue = true;
        } else {
            for (int srcRow : srcRows) {
                QVariant value = m_excelHandler->getCellValue(srcKey, sourceSheet, srcRow,
                                                             m_excelHandler->letterToColumn(sourceColumn));
                if (value.canConvert<double>()) {
                    total += value.toDouble();
                    hasValue = true;
                }
            }
        }

        if (!hasValue) {
            continue;
        }
        total /= 1000.0;
        m_excelHandler->setCellValue(destKey, destSheet, destRow,
                                     m_excelHandler->letterToColumn(destColumn), total);
        handled++;
    }

    struct ExtraSource {
        int destRow;
        QString sheetName;
        QString cellRef;
        double divisor;
    };

    const QVector<ExtraSource> extraSources = {
        {5, "TRAFFIC mott 2025", "Q19", 1000.0},
        {7, "TRAFFIC mott 2025", "Q34", 1000.0},
        {8, "TRAFFIC mott 2025", "Q81", 0.0},
        {9, "TRAFFIC mott 2025", "Q87", 0.0},
        {10, "TRAFFIC mott 2025", "Q160", 0.0},
        {11, "TRAFFIC mott 2025", "Q134", 0.0},
        {16, "Staff report", "AC34", 0.0}
    };

    QRegularExpression cellRefPattern("^([A-Za-z]+)(\\d+)$");
    for (const ExtraSource& extra : extraSources) {
        QRegularExpressionMatch match = cellRefPattern.match(extra.cellRef);
        if (!match.hasMatch()) {
            continue;
        }
        QString colLetters = match.captured(1).toUpper();
        int rowNumber = match.captured(2).toInt();
        QVariant raw = m_excelHandler->getCellValue(destKey, extra.sheetName,
                                                    rowNumber,
                                                    m_excelHandler->letterToColumn(colLetters));
        double value = 0.0;
        if (raw.canConvert<double>()) {
            value = raw.toDouble();
        }
        if (extra.divisor != 0.0) {
            value /= extra.divisor;
        }
        m_excelHandler->setCellValue(destKey, destSheet, extra.destRow,
                                     m_excelHandler->letterToColumn(destColumn), value);
        handled++;
    }

    return handled;
}

int MainWindow::handleYtdTransfer(const MappingEntry& entry, int year, const QString& destKey, const QString& destFilePath)
{
    Q_UNUSED(year);

    const QMap<QString, QPair<QString, QString>> monthToYtdCol = {
        {"January", {"IP", ""}}, {"February", {"IQ", "IP"}}, {"March", {"IR", "IQ"}},
        {"April", {"IS", "IR"}}, {"May", {"IT", "IS"}}, {"June", {"IU", "IT"}},
        {"July", {"IV", "IU"}}, {"August", {"IW", "IV"}}, {"September", {"IX", "IW"}},
        {"October", {"IY", "IX"}}, {"November", {"IZ", "IY"}}, {"December", {"", ""}}
    };

    const QMap<QString, QString> monthToDestCol = {
        {"January", "G"}, {"February", "W"}, {"March", "AM"}, {"April", "BD"},
        {"May", "BW"}, {"June", "CP"}, {"July", "DJ"}, {"August", "EF"},
        {"September", "FB"}, {"October", "FY"}, {"November", "GX"}, {"December", "HW"}
    };

    auto ytdCols = monthToYtdCol.value(entry.month, {"", ""});
    QString ytdCol = ytdCols.first;
    QString prevCol = ytdCols.second;
    if (ytdCol.isEmpty()) {
        return 0;
    }

    QString currCol = monthToDestCol.value(entry.month, "G");
    const QVector<int> destRows = entry.destRows;

    int handled = 0;
    for (int destRow : destRows) {
        double currVal = 0.0;
        double prevVal = 0.0;
        QVariant currRaw = m_excelHandler->getCellValue(destKey, entry.destSheet, destRow,
                                                        m_excelHandler->letterToColumn(currCol));
        if (currRaw.canConvert<double>()) {
            currVal = currRaw.toDouble();
        }
        if (!prevCol.isEmpty()) {
            QVariant prevRaw = m_excelHandler->getCellValue(destKey, entry.destSheet, destRow,
                                                            m_excelHandler->letterToColumn(prevCol));
            if (prevRaw.canConvert<double>()) {
                prevVal = prevRaw.toDouble();
            }
        }
        double ytdVal = currVal + prevVal;
        m_excelHandler->setCellValue(destKey, entry.destSheet, destRow,
                                     m_excelHandler->letterToColumn(ytdCol), ytdVal);
        handled++;
    }

    return handled;
}

void MainWindow::onExportSelectedMonths()
{
    QVector<QPair<QString, int>> periods = getSelectedPeriods();
    if (periods.isEmpty()) {
        showToast("Please select at least one month.", ToastWidget::Warning);
        return;
    }

    if (!m_mappingService || !m_transferService) {
        showToast("Mapping service unavailable.", ToastWidget::Error);
        return;
    }

    QVector<MappingService::MappingSelection> selections = m_mappingService->collectMappings(periods);
    QVector<MappingService::MappingSelection> paxSelections = m_mappingService->collectPaxMappings(periods);
    QVector<MappingService::MappingSelection> staffSelections = m_mappingService->collectStaffMappings(periods);
    QVector<MappingService::MappingSelection> sapYtdSelections = m_mappingService->collectSapYtdMappings(periods);

    QVector<MappingService::MappingSelection> allSelections = selections;
    allSelections.append(paxSelections);
    allSelections.append(staffSelections);
    allSelections.append(sapYtdSelections);

    if (allSelections.isEmpty()) {
        showToast("No mappings found for selected periods.", ToastWidget::Warning);
        return;
    }

    int totalCells = 0;
    for (const auto& selection : allSelections) {
        const QString destKey = QString("%1_%2_cost_control").arg(selection.month).arg(selection.year);
        const QString destPath = findCostControlPath(selection.month, selection.year);
        TransferService::Result result = m_transferService->transferEntry(selection.entry,
                                                                          selection.year,
                                                                          destKey,
                                                                          destPath,
                                                                          m_destFolder);
        totalCells += result.cellsTransferred;
    }

    showToast(QString("Exported %1 cells").arg(totalCells), ToastWidget::Success);
    updateStatusBar(QString("Exported %1 cells").arg(totalCells));
}

void MainWindow::createFillAllTab()
{
    m_fillAllMonthsTab = new FillAllMonthsTab(this);
    m_tabWidget->addTab(m_fillAllMonthsTab, "Fill All Months");
    
    connect(m_fillAllMonthsTab, &FillAllMonthsTab::executeRequested,
            this, &MainWindow::onFillAllExecute);
}

void MainWindow::onFillAllExecute(const FillAllScanResult& result)
{
    // Implementation stays in MainWindow since it needs access to services
    // Use result parameter instead of m_fillAllScanResult
    qInfo() << "[FILL ALL] Starting execution for year" << result.year << "target month" << result.targetMonth;
    
    // TODO: Implement the actual fill all execution logic here
    // This will use m_excelHandler, m_transferService, etc.
    
    showToast("Fill All execution not yet implemented", ToastWidget::Info);
}

QString MainWindow::findCostControlFile(const QString& monthFolder)
{
    // e.g. L:/.../2025/08/Cost Control ZAG 08_2025_working.xlsm
    // or scan for any .xlsm in the folder
    QDir dir(monthFolder);
    QStringList filters = {"Cost Control*.xlsm"};
    QStringList files = dir.entryList(filters, QDir::Files);
    if (!files.isEmpty())
        return dir.absoluteFilePath(files.first());
    return QString();
}

QString MainWindow::findSapFile(const QString& monthFolder, const QString& mm, int year)
{
    // e.g. L:/.../2025/05/SAP export monthly/05_2025.xlsx
    QString sapFolder = QString("%1/SAP export monthly").arg(monthFolder);
    QString fileName = QString("%1_%2.xlsx").arg(mm).arg(year);
    QString path = QString("%1/%2").arg(sapFolder, fileName);
    return path;
}

QString MainWindow::findSapYtdFile(const QString& monthFolder, const QString& mm, int year)
{
    // e.g. L:/.../2025/05/SAP export monthly/YTD_05_2025.xlsm
    QString sapFolder = QString("%1/SAP export monthly").arg(monthFolder);
    QString fileName = QString("YTD_%1_%2.xlsm").arg(mm).arg(year);
    return QString("%1/%2").arg(sapFolder, fileName);
}

QString MainWindow::findTrafficFile(const QString& basePath, const QString& mm, int year)
{
    // e.g. L:/.../2025/05/Traffic/TRAFFIC mott 2025.xlsx
    // Traffic file is the SAME file for all months
    QString path = QString("%1/%2/Traffic/TRAFFIC mott %3.xlsx").arg(basePath, mm).arg(year);
    return path;
}

QString MainWindow::findStaffFile(int year)
{
    // e.g. L:/.../Staff/Staff_2025.xlsx
    return QString("%1/Staff/Staff_%2.xlsx").arg(m_destFolder).arg(year);
}

void MainWindow::showFillAllResults(const FillAllResult& result)
{
    QString msg = QString(
        "Fill All Months Complete\n\n"
        "Total transfers: %1\n"
        "Successful: %2\n"
        "Failed/Skipped: %3\n"
        "Cells transferred: %4\n")
        .arg(result.totalTransfers)
        .arg(result.successCount)
        .arg(result.failCount)
        .arg(result.cellsTransferred);
    
    if (!result.warnings.isEmpty()) {
        msg += "\n?? Missing files:\n";
        for (const auto& w : result.warnings)
            msg += "   ? " + w + "\n";
    }
    
    if (!result.errors.isEmpty()) {
        msg += "\n? Errors:\n";
        for (const auto& e : result.errors)
            msg += "   ? " + e + "\n";
    }
    
    QMessageBox::information(this, "Fill All Months", msg);
}

void MainWindow::onIndividualBrowseSource()
{
    QString path = QFileDialog::getOpenFileName(
        this,
        "Select Source Excel File",
        m_sourceFolder,
        "Excel Files (*.xlsx *.xlsm *.xls);;All Files (*)"
    );

    auto panel = m_individualTransferTab ? m_individualTransferTab->panel() : nullptr;
    if (!path.isEmpty() && panel) {
        IndividualTransferPanel::TransferConfig config = panel->getTransferConfig();
        config.sourceFile = path;
        panel->setTransferConfig(config);

        // Load sheet names from source file
        QString tempKey = "temp_individual_source";
        if (m_excelHandler->loadWorkbook(path, tempKey)) {
            QStringList sheets = m_excelHandler->getSheetNames(tempKey);
            // Update source sheet combo in the panel
            // Note: This requires adding a method to IndividualTransferPanel to update sheet list
            m_excelHandler->unloadWorkbook(tempKey);
        }
    }
}

void MainWindow::onIndividualBrowseDest()
{
    QString path = QFileDialog::getOpenFileName(
        this,
        "Select Destination Excel File",
        m_destFolder,
        "Excel Files (*.xlsx *.xlsm *.xls);;All Files (*)"
    );

    auto panel = m_individualTransferTab ? m_individualTransferTab->panel() : nullptr;
    if (!path.isEmpty() && panel) {
        IndividualTransferPanel::TransferConfig config = panel->getTransferConfig();
        config.destFile = path;
        panel->setTransferConfig(config);

        // Load sheet names from destination file
        QString tempKey = "temp_individual_dest";
        if (m_excelHandler->loadWorkbook(path, tempKey)) {
            QStringList sheets = m_excelHandler->getSheetNames(tempKey);
            // Update dest sheet combo in the panel
            m_excelHandler->unloadWorkbook(tempKey);
        }
    }
}

void MainWindow::onIndividualSelectSourceCell()
{
    auto panel = m_individualTransferTab ? m_individualTransferTab->panel() : nullptr;
    if (!panel) return;

    IndividualTransferPanel::TransferConfig config = panel->getTransferConfig();
    if (config.sourceFile.isEmpty()) {
        showToast("Please select a source file first", ToastWidget::Warning);
        return;
    }

    bool ok = false;
    QString sheet = QInputDialog::getText(this, "Source Sheet", "Sheet name:",
                                          QLineEdit::Normal, config.sourceSheet, &ok);
    if (!ok || sheet.trimmed().isEmpty()) return;

    QString column = QInputDialog::getText(this, "Source Column", "Column letter:",
                                           QLineEdit::Normal, config.sourceColumn, &ok);
    if (!ok || column.trimmed().isEmpty()) return;

    int row = QInputDialog::getInt(this, "Source Row", "Row number:",
                                   config.sourceRow > 0 ? config.sourceRow : 1,
                                   1, 1000000, 1, &ok);
    if (!ok) return;

    config.sourceSheet = sheet.trimmed();
    config.sourceColumn = column.trimmed().toUpper();
    config.sourceRow = row;
    m_individualPanel->setTransferConfig(config);
}

void MainWindow::onIndividualSelectDestCell()
{
    auto panel = m_individualTransferTab ? m_individualTransferTab->panel() : nullptr;
    if (!panel) return;

    IndividualTransferPanel::TransferConfig config = panel->getTransferConfig();
    if (config.destFile.isEmpty()) {
        showToast("Please select a destination file first", ToastWidget::Warning);
        return;
    }

    bool ok = false;
    QString sheet = QInputDialog::getText(this, "Destination Sheet", "Sheet name:",
                                          QLineEdit::Normal, config.destSheet, &ok);
    if (!ok || sheet.trimmed().isEmpty()) return;

    QString column = QInputDialog::getText(this, "Destination Column", "Column letter:",
                                           QLineEdit::Normal, config.destColumn, &ok);
    if (!ok || column.trimmed().isEmpty()) return;

    int row = QInputDialog::getInt(this, "Destination Row", "Row number:",
                                   config.destRow > 0 ? config.destRow : 1,
                                   1, 1000000, 1, &ok);
    if (!ok) return;

    config.destSheet = sheet.trimmed();
    config.destColumn = column.trimmed().toUpper();
    config.destRow = row;
    m_individualPanel->setTransferConfig(config);
}

void MainWindow::onIndividualTransfer(const IndividualTransferPanel::TransferConfig& config)
{
    if (config.sourceFile.isEmpty() || config.destFile.isEmpty()) {
        showToast("Please select both source and destination files", ToastWidget::Warning);
        return;
    }
    
    try {
        // Load source file
        QString srcKey = "individual_src";
        if (!m_excelHandler->loadWorkbook(config.sourceFile, srcKey)) {
            showToast("Failed to load source file", ToastWidget::Error);
            return;
        }
        
        // Load destination file
        // ExcelHandler marks save targets by key suffix _cost_control.
        QString destKey = "individual_dest_cost_control";
        if (!m_excelHandler->loadWorkbook(config.destFile, destKey)) {
            m_excelHandler->unloadWorkbook(srcKey);
            showToast("Failed to load destination file", ToastWidget::Error);
            return;
        }
        
        // Perform transfer
        int cellsTransferred = 0;
        int srcCol = m_excelHandler->letterToColumn(config.sourceColumn);
        int destCol = m_excelHandler->letterToColumn(config.destColumn);
        
        for (int i = 0; i < config.cellCount; i++) {
            QVariant value = m_excelHandler->getCellValue(
                srcKey, config.sourceSheet, config.sourceRow + i, srcCol
            );
            
            if (value.isValid()) {
                // Apply division if needed
                if (config.divideBy1000 && value.canConvert<double>()) {
                    double numValue = value.toDouble() / 1000.0;
                    value = numValue;
                }
                
                bool success = m_excelHandler->setCellValue(
                    destKey, config.destSheet, config.destRow + i, destCol, value
                );
                
                if (success) {
                    cellsTransferred++;
                }
            }
        }
        
        // Save destination file
        bool saved = m_excelHandler->saveWorkbook(destKey, config.destFile);
        
        // Cleanup
        m_excelHandler->unloadWorkbook(srcKey);
        m_excelHandler->unloadWorkbook(destKey);
        
        if (saved) {
            showToast(
                QString("Transfer complete: %1 cells transferred").arg(cellsTransferred),
                ToastWidget::Success
            );
            QMessageBox::information(
                this,
                "Transfer Complete",
                QString("Successfully transferred %1 cells\n\nFile saved: %2")
                    .arg(cellsTransferred)
                    .arg(QFileInfo(config.destFile).fileName())
            );
        } else {
            showToast("Transfer completed but save failed", ToastWidget::Error);
        }
        
    } catch (const std::exception& e) {
        showToast(QString("Transfer failed: %1").arg(e.what()), ToastWidget::Error);
    }
}


