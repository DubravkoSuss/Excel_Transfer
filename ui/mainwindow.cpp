#include "../core/mappingsmanager.h"
#include "../core/mappingcontroller.h"
#include "mainwindow.h"
#include "../features/mappings/rowmapvalidatordialog.h"
#include "../features/periods/periodrow.h"
#include "../features/periods/yearcard.h"
#include "../core/periodcontroller.h"
#include "../core/periodmodel.h"
#include "../features/mappings/mappingrow.h"
#include "../features/transfer/individualtransferpanel.h"
#include "sheetcellselectordialog.h"
#include "ignorerowsdialog.h"
#include "../features/transfer/transferworker.h"
#include "../features/transfer/loadworker.h"
#include "../features/transfer/rollingworker.h"
#include "../features/transfer/rollingtransferservice.h"
#include "individualtransfertab.h"
#include "fillallmonthstab.h"
#include "hybridtransfertab.h"
#include "comparatortab.h"
#include "excelsearchtab.h"
#include "yearrollovertab.h"
#include "customtabbar.h"
#include "../features/transfer/fillallworker.h"
#include "../features/transfer/hybridworker.h"
#include <QRegularExpression>
#include <QDebug>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDir>
#include <QInputDialog>
#include <QHeaderView>
#include <QSettings>
#include <QProgressDialog>
#include <QTimer>
#include <QScreen>
#include <QSplitter>
#include <QGuiApplication>
#include <QMessageBox>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

const QString MainWindow::PASSWORD = "finance2026";
const QString MainWindow::DEFAULT_SOURCE_FOLDER = "L:/Cost control/Cost Control/Cost control";
const QString MainWindow::DEFAULT_DEST_FOLDER   = "L:/Cost control/Cost Control/Cost control";
const QStringList MainWindow::MONTHS_LIST = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
const QMap<QString, QString> MainWindow::MONTH_TO_NUM = {{"January", "01"}, {"February", "02"}, {"March", "03"}, {"April", "04"}, {"May", "05"}, {"June", "06"}, {"July", "07"}, {"August", "08"}, {"September", "09"}, {"October", "10"}, {"November", "11"}, {"December", "12"}};
const QMap<int, QList<int>> MainWindow::QUARTERS = {{1, {0, 1, 2}}, {2, {3, 4, 5}}, {3, {6, 7, 8}}, {4, {9, 10, 11}}};
MainWindow* MainWindow::s_instance = nullptr;
const QVector<int> MainWindow::YEAR_RANGE = {2010, 2011, 2012, 2013, 2014, 2015, 2016, 2017, 2018, 2019, 2020, 2021, 2022, 2023, 2024, 2025, 2026, 2027, 2028, 2029, 2030, 2031, 2032, 2033, 2034, 2035, 2036, 2037, 2038, 2039, 2040, 2041, 2042, 2043};

namespace {
bool promptForValidWorkbook(QWidget* parent,
                            const QString& usageLabel,
                            QString& filePath)
{
    while (true) {
        QString validationError;
        if (ExcelHandler::validateWorkbookFile(filePath, &validationError)) {
            return true;
        }

        QMessageBox box(parent);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle("Invalid Excel File");
        box.setText(
            QString("%1\n\nFile:\n%2\n\nProblem:\n%3")
                .arg(usageLabel, filePath, validationError));
        box.setInformativeText("Choose a replacement .xlsx/.xlsm file or cancel execution.");

        QPushButton* browseBtn = box.addButton("Browse Replacement...", QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();

        if (box.clickedButton() != browseBtn) {
            return false;
        }

        const QString replacement = QFileDialog::getOpenFileName(
            parent,
            "Select Replacement Excel File",
            QFileInfo(filePath).absolutePath(),
            "Excel Files (*.xlsx *.xlsm *.xls);;All Files (*)");

        if (replacement.isEmpty()) {
            return false;
        }
        filePath = replacement;
    }
}

bool resolveWorkbookPath(QWidget* parent,
                         const QString& usageLabel,
                         const QString& originalPath,
                         QMap<QString, QString>& resolvedMap,
                         QString* resolvedOut)
{
    if (originalPath.isEmpty()) {
        if (resolvedOut) resolvedOut->clear();
        return true;
    }

    if (resolvedMap.contains(originalPath)) {
        if (resolvedOut) *resolvedOut = resolvedMap.value(originalPath);
        return true;
    }

    QString resolved = originalPath;
    if (QFile::exists(resolved)) {
        if (!promptForValidWorkbook(parent, usageLabel, resolved)) {
            return false;
        }
    }

    resolvedMap.insert(originalPath, resolved);
    if (resolvedOut) *resolvedOut = resolved;
    return true;
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_isTransferRunning(false)
    , m_isTransferPaused(false)
    , m_transferTotalMappings(0)
    , m_transferSuccessfulMappings(0)
{
    s_instance = this;

    // Initialize debounce timer for generatePeriodRows — shared across all year cards
    m_generatePeriodTimer = new QTimer(this);
    m_generatePeriodTimer->setSingleShot(true);
    m_generatePeriodTimer->setInterval(200); // 200ms settle time
    connect(m_generatePeriodTimer, &QTimer::timeout, this, [this]() {
        if (m_periodController) m_periodController->generatePeriodRows();
    });

    // Safety-net timeout: if a background operation runs > 10 minutes, force-reset busy flags
    m_busyTimeout = new QTimer(this);
    m_busyTimeout->setSingleShot(true);
    connect(m_busyTimeout, &QTimer::timeout, this, [this]() {
        if (isBusy()) {
            qWarning() << "[GUARD] TIMEOUT — operation exceeded 10 minutes, force-resetting busy flags";
            m_isTransferRunning  = false;
            m_isLoadingPeriods   = false;
            m_isLoadingRT        = false;
            m_fillAllRunning     = false;
            statusBar()->showMessage("⚠ Operation timed out — UI unlocked", 6000);
        }
    });

    setupUI();

    // Center window on primary monitor
    QScreen* screen = QGuiApplication::primaryScreen();
    if (screen) {
        QRect sg = screen->availableGeometry();
        move(sg.center() - rect().center());
    }
    
    // Customize Windows title bar color
#ifdef Q_OS_WIN
    customizeWindowsTitleBar();
#endif

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
    connect(m_mappingController, &MappingController::requestIgnoreRows, this, &MainWindow::onIgnoreRows);
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

#ifdef Q_OS_WIN
void MainWindow::customizeWindowsTitleBar()
{
    // Get the window handle
    HWND hwnd = (HWND)winId();
    
    // Set light mode for title bar
    BOOL useDarkMode = FALSE;
    DwmSetWindowAttribute(hwnd, 20, &useDarkMode, sizeof(useDarkMode)); // DWMWA_USE_IMMERSIVE_DARK_MODE = 20
    
    // Set title bar color to match main window background (light grey #F3F4F6)
    // Convert hex color to COLORREF (BGR format)
    COLORREF titleBarColor = RGB(0xF6, 0xF4, 0xF3); // BGR: 0xF3F4F6
    DwmSetWindowAttribute(hwnd, 35, &titleBarColor, sizeof(titleBarColor)); // DWMWA_CAPTION_COLOR = 35
    
    // Set title text color to dark grey/black
    COLORREF textColor = RGB(0x1F, 0x29, 0x37); // Dark grey for readability
    DwmSetWindowAttribute(hwnd, 36, &textColor, sizeof(textColor)); // DWMWA_TEXT_COLOR = 36
    
    // Set border color to match background (removes black edge)
    COLORREF borderColor = RGB(0xF6, 0xF4, 0xF3); // Same as title bar
    DwmSetWindowAttribute(hwnd, 34, &borderColor, sizeof(borderColor)); // DWMWA_BORDER_COLOR = 34
}
#endif

void MainWindow::setupUI()
{
    setWindowTitle("Excel Transfer Tool - Monthly Auto");
    resize(1450, 1150);
    setMinimumSize(950, 620);

    m_centralWidget = new QWidget();
    setCentralWidget(m_centralWidget);

    m_mainLayout = new QVBoxLayout(m_centralWidget);
    m_mainLayout->setContentsMargins(16, 16, 16, 16);
    m_mainLayout->setSpacing(16);

    createHeader();
    
    // Create tab widget for main content with custom tab bar
    m_tabWidget = new CustomTabWidget();
    m_mainLayout->addWidget(m_tabWidget);
    
    // Connect to tab moved signal to save order
    connect(m_tabWidget->tabBar(), &QTabBar::tabMoved, this, &MainWindow::onTabMoved);
    
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
    createMappingsCard();  // Creates sidebar but doesn't add it yet
    
    // Create modern Windows 11-style toggle button for sidebar
    m_btnToggleMappings = new QPushButton();
    m_btnToggleMappings->setStyleSheet(
        "QPushButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #FFFFFF, stop:1 #F5F5F5);"
        "  border: 1px solid #E0E0E0;"
        "  border-radius: 4px;"
        "  padding: 6px;"
        "  min-width: 32px;"
        "  min-height: 32px;"
        "  max-width: 32px;"
        "  max-height: 32px;"
        "}"
        "QPushButton:hover {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #F0F0F0, stop:1 #E8E8E8);"
        "  border: 1px solid #D0D0D0;"
        "}"
        "QPushButton:pressed {"
        "  background: #E0E0E0;"
        "  border: 1px solid #C0C0C0;"
        "}"
    );
    

    m_btnToggleMappings->setText("🔀");
    m_btnToggleMappings->setFont(QFont("Segoe UI", 8));  //  Reduced from 16 to 12
    m_btnToggleMappings->setToolTip("Hide mappings sidebar");
    m_btnToggleMappings->setCursor(Qt::PointingHandCursor);
    
    connect(m_btnToggleMappings, &QPushButton::clicked, this, [this]() {
        bool isVisible = m_mappingsSidebar->isVisible();
        m_mappingsSidebar->setVisible(!isVisible);
        
        // Update button appearance
        if (isVisible) {
            // Sidebar is now hidden - show arrow pointing right
            m_btnToggleMappings->setText("⏩");
            m_btnToggleMappings->setToolTip("Show mappings sidebar");
        } else {
            // Sidebar is now visible - show hamburger menu
            m_btnToggleMappings->setText("⏪");
            m_btnToggleMappings->setToolTip("Hide mappings sidebar");
        }
    });
    
    // Create main content area (will be on the right side)
    QWidget* mainContentArea = new QWidget();
    QVBoxLayout* mainContentLayout = new QVBoxLayout(mainContentArea);
    mainContentLayout->setContentsMargins(0, 0, 0, 0);
    mainContentLayout->setSpacing(0);
    
    // Use splitter to make top/bottom sections resizable by drag.
    m_mainSectionsSplitter = new QSplitter(Qt::Vertical);
    m_mainSectionsSplitter->setChildrenCollapsible(false);
    m_mainSectionsSplitter->setHandleWidth(8);
    
    // Move month/year selector and file info to main content splitter
    while (m_mainLayout->count() > 2) { // Keep header and tab widget
        QLayoutItem* item = m_mainLayout->takeAt(2);
        if (item->widget()) {
            m_mainSectionsSplitter->addWidget(item->widget());
        }
        delete item;
    }
    
    // Set initial sizes - give more space to month/year selector
    QList<int> sizes;
    sizes << 350 << 150;  // MonthYearSelector, FileInfo
    m_mainSectionsSplitter->setSizes(sizes);
    
    mainContentLayout->addWidget(m_mainSectionsSplitter);
    
    // Create container for toggle button + main content
    QWidget* rightSideContainer = new QWidget();
    QHBoxLayout* rightSideLayout = new QHBoxLayout(rightSideContainer);
    rightSideLayout->setContentsMargins(8, 16, 0, 0);  // Reduced top margin to 16px (closer to cards)
    rightSideLayout->setSpacing(8);
    rightSideLayout->addWidget(m_btnToggleMappings, 0, Qt::AlignTop);  // Align to top
    rightSideLayout->addWidget(mainContentArea);
    
    // Create horizontal splitter: sidebar on left, main content on right
    m_horizontalSplitter = new QSplitter(Qt::Horizontal);
    m_horizontalSplitter->setChildrenCollapsible(false);
    m_horizontalSplitter->setHandleWidth(1);
    m_horizontalSplitter->addWidget(m_mappingsSidebar);     // Left: mappings sidebar
    m_horizontalSplitter->addWidget(rightSideContainer);    // Right: toggle button + main content
    
    // Set initial sizes: 350px for sidebar, rest for main content
    m_horizontalSplitter->setStretchFactor(0, 0);  // Sidebar: fixed-ish
    m_horizontalSplitter->setStretchFactor(1, 1);  // Main content: stretches
    QList<int> hSizes;
    hSizes << 350 << 950;
    m_horizontalSplitter->setSizes(hSizes);
    
    tempLayout->addWidget(m_horizontalSplitter);
    
    mainTransferLayout->addWidget(tempContainer);
    
    // Create Fill All tab
    createFillAllTab();
    
    // Create Hybrid Transfer tab
    createHybridTab();
    
    // Create Individual Transfer tab
    createIndividualTab();

    // Create Comparator tab
    createComparatorTab();

    // Create Excel Search tab
    createExcelSearchTab();

    // Create Rollover tab
    createYearRolloverTab();

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
    
    // Restore saved tab order
    restoreTabOrder();
}

void MainWindow::saveTabOrder()
{
    QSettings settings("ExcelTransfer", "TabOrder");
    QStringList tabOrder;
    
    for (int i = 0; i < m_tabWidget->count(); i++) {
        tabOrder.append(m_tabWidget->tabText(i));
    }
    
    settings.setValue("order", tabOrder);
    qInfo() << "[TabOrder] Saved:" << tabOrder;
}

void MainWindow::restoreTabOrder()
{
    QSettings settings("ExcelTransfer", "TabOrder");
    QStringList savedOrder = settings.value("order").toStringList();
    
    if (savedOrder.isEmpty()) {
        qInfo() << "[TabOrder] No saved order found, using default";
        return;
    }
    
    qInfo() << "[TabOrder] Restoring:" << savedOrder;
    
    // Create a map of tab text to index
    QMap<QString, int> tabIndices;
    for (int i = 0; i < m_tabWidget->count(); i++) {
        tabIndices[m_tabWidget->tabText(i)] = i;
    }
    
    // Reorder tabs based on saved order
    for (int targetPos = 0; targetPos < savedOrder.size(); targetPos++) {
        QString tabName = savedOrder[targetPos];
        
        if (!tabIndices.contains(tabName)) {
            continue; // Tab doesn't exist anymore
        }
        
        // Find current position of this tab
        int currentPos = -1;
        for (int i = 0; i < m_tabWidget->count(); i++) {
            if (m_tabWidget->tabText(i) == tabName) {
                currentPos = i;
                break;
            }
        }
        
        // Move tab to target position
        if (currentPos != -1 && currentPos != targetPos) {
            m_tabWidget->tabBar()->moveTab(currentPos, targetPos);
        }
    }
}

void MainWindow::onTabMoved(int from, int to)
{
    Q_UNUSED(from);
    Q_UNUSED(to);
    saveTabOrder();
}

void MainWindow::createHeader()
{
    m_headerFrame = new QFrame();
    m_headerFrame->setObjectName("headerFrame");
    m_headerFrame->setStyleSheet(
        "#headerFrame { "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #0F1B3A, stop:0.85 #0F1B3A, stop:1 rgba(15, 27, 58, 0));"
        "  border: none;"
        "}"
    );

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


    // Reset button clears everything back to a fresh state
    QPushButton* btnReset = new QPushButton("Reset");
    btnReset->setStyleSheet(
        "QPushButton { background: #1E3A5F; color: #A0B1D0; font-weight: 600; "
        "padding: 10px 16px; border-radius: 6px; border: 1px solid #2D4E7A; }"
        "QPushButton:hover { background: #2D4E7A; color: white; }"
    );
    btnReset->setToolTip("Reset everything: clears cached files, mapping cards, and period rows");
    connect(btnReset, &QPushButton::clicked, this, &MainWindow::onResetAll);
    headerLayout->addWidget(btnReset);

    // Update Row Mappings button — scans Excel files by label, rewrites JSON rowMaps
    m_btnUpdateMappings = new QPushButton("Update Mappings");
    m_btnUpdateMappings->setStyleSheet(
        "QPushButton {"
        "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #14B8A6,stop:1 #0D9488);"
        "  color: white; font-weight: 600; font-size: 13px;"
        "  padding: 10px 18px; border-radius: 6px; border: none; }"
        "QPushButton:hover { background: #0D9488; }"
        "QPushButton:pressed { background: #0F766E; }"
        "QPushButton:disabled { background: #E5E7EB; color: #9CA3AF; }");
    m_btnUpdateMappings->setToolTip(
        "Scan source + destination Excel files by text label in column B,\n"
        "find current row numbers, rewrite JSON mapping files.\n\n"
        "First run: bootstraps label_definitions.json from current mappings.\n"
        "Subsequent: uses saved labels to find rows in new files.");
    connect(m_btnUpdateMappings, &QPushButton::clicked, this, [this]() {
        if (m_hybridTransferTab) {
            m_hybridTransferTab->onUpdateMappings();
        }
    });
    headerLayout->addWidget(m_btnUpdateMappings);

    // Log toggle button — shows/hides the status log side panel
    m_btnToggleLog = new QPushButton("Log");
    m_btnToggleLog->setCheckable(true);
    m_btnToggleLog->setChecked(false);
    m_btnToggleLog->setStyleSheet(
        "QPushButton { background: #1E3A5F; color: #A0B1D0; font-weight: 600; "
        "padding: 10px 14px; border-radius: 6px; border: 1px solid #2D4E7A; }"
        "QPushButton:checked { background: #2D4E7A; color: white; border-color: #4B7FC4; }"
        "QPushButton:hover { background: #2D4E7A; color: white; }"
    );
    m_btnToggleLog->setToolTip("Show/hide the status log panel");
    connect(m_btnToggleLog, &QPushButton::toggled, this, [this](bool checked) {
        if (m_statusDock) m_statusDock->setVisible(checked);
    });
    headerLayout->addWidget(m_btnToggleLog);

    m_mainLayout->addWidget(m_headerFrame);
}

void MainWindow::createMonthYearSelector()
{
    // Premium design card
    QFrame* selectorCard = new QFrame();
    selectorCard->setStyleSheet(
        "QFrame {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #FAFBFC, stop:1 #F5F7FA);"
        "  border-radius: 12px;"
        "  border: none;"
        "}"
    );
    selectorCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    QVBoxLayout* selectorLayout = new QVBoxLayout(selectorCard);
    selectorLayout->setContentsMargins(20, 20, 20, 20);
    selectorLayout->setSpacing(6);

    // Premium title styling
    QLabel* title = new QLabel("Select Years & Months");
    title->setStyleSheet(
        "font-weight: 700; "
        "font-size: 16px; "
        "color: #1A1F36; "
        "letter-spacing: -0.3px;"
    );
    selectorLayout->addWidget(title);

    // Premium hint styling
    QLabel* hint = new QLabel("Check a year to expand then select months or use Q1 Q4 buttons in each year");
    hint->setStyleSheet(
        "font-size: 11px; "
        "color: #8B92A7; "
        "margin-bottom: 4px;"
    );
    selectorLayout->addWidget(hint);

    // Premium years group box - no border for cleaner look
    QGroupBox* yearsGroup = new QGroupBox();
    yearsGroup->setStyleSheet(
        "QGroupBox {"
        "  border: none;"
        "  background: transparent;"
        "  margin-top: 0px;"
        "  padding-top: 0px;"
        "}"
    );
    QVBoxLayout* yearsMainLayout = new QVBoxLayout(yearsGroup);
    yearsMainLayout->setSpacing(2);
    yearsMainLayout->setContentsMargins(0, 0, 0, 0);

    // Create a scrollable area for years
    QScrollArea* yearsScroll = new QScrollArea();
    yearsScroll->setWidgetResizable(true);
    yearsScroll->setFrameShape(QFrame::NoFrame);
    yearsScroll->setStyleSheet(
        "QScrollArea {"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QScrollBar:vertical {"
        "  background: transparent;"
        "  width: 10px;"
        "  border-radius: 5px;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: rgba(0, 0, 0, 0.15);"
        "  border-radius: 5px;"
        "  min-height: 30px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "  background: rgba(0, 0, 0, 0.25);"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "  height: 0px;"
        "}"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
        "  background: transparent;"
        "}"
    );
    
    QWidget* yearsContainer = new QWidget();
    yearsContainer->setStyleSheet("background: transparent;");
    QVBoxLayout* yearsContainerLayout = new QVBoxLayout(yearsContainer);
    yearsContainerLayout->setSpacing(2);
    yearsContainerLayout->setContentsMargins(0, 0, 0, 0);

    for (int year : YEAR_RANGE) {
        // Use YearCard a proper QWidget that owns all its children safely
        YearCard* card = new YearCard(year, yearsContainer);

        // Wire year checkbox period controller
        // Use a debounce timer so generatePeriodRows() only fires once
        // even if multiple year checkboxes are toggled in rapid succession
        connect(card, &YearCard::yearChecked, this, [this](int yr, bool checked) {
            if (m_periodController) {
                m_periodController->onYearToggled(yr, checked);
                if (checked && m_generatePeriodTimer)
                    m_generatePeriodTimer->start();
            }
        });

        // Wire create month files mode
        connect(card, &YearCard::createMonthFilesRequested,
                this, &MainWindow::onCreateMonthFilesMode);

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

    // Add buttons inside the years group (so they move with the splitter)
    QHBoxLayout* actionBtnLayout = new QHBoxLayout();
    actionBtnLayout->setSpacing(8);
    actionBtnLayout->setContentsMargins(8, 8, 8, 8);

    // Premium button styles with gradients and shadows
    static const QString loadStyle =
        "QPushButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #8B5CF6, stop:1 #7C3AED);"
        "  color: white;"
        "  font-weight: 600;"
        "  padding: 8px 18px;"
        "  border-radius: 8px;"
        "  font-size: 12px;"
        "  border: none;"
        "}"
        "QPushButton:hover {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #7C3AED, stop:1 #6D28D9);"
        "}"
        "QPushButton:pressed {"
        "  background: #6D28D9;"
        "}";
    
    static const QString rtStyle =
        "QPushButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #D97706, stop:1 #B45309);"
        "  color: white;"
        "  font-weight: 600;"
        "  padding: 8px 18px;"
        "  border-radius: 8px;"
        "  font-size: 12px;"
        "  border: none;"
        "}"
        "QPushButton:hover {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #B45309, stop:1 #92400E);"
        "}"
        "QPushButton:pressed {"
        "  background: #92400E;"
        "}";
    
    static const QString clearStyle =
        "QPushButton {"
        "  background: rgba(254, 242, 242, 0.8);"
        "  color: #DC2626;"
        "  font-weight: 600;"
        "  padding: 8px 16px;"
        "  border-radius: 8px;"
        "  font-size: 12px;"
        "  border: 1px solid #FECACA;"
        "}"
        "QPushButton:hover {"
        "  background: #FEE2E2;"
        "  border: 1px solid #FCA5A5;"
        "}"
        "QPushButton:pressed {"
        "  background: #FECACA;"
        "}";

    m_btnLoad = new QPushButton("Load Periods");
    m_btnLoad->setStyleSheet(loadStyle);
    m_btnLoad->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_btnLoad->setCursor(Qt::PointingHandCursor);
    connect(m_btnLoad, &QPushButton::clicked, this, &MainWindow::onLoadSelectedPeriods);
    actionBtnLayout->addWidget(m_btnLoad);

    m_btnLoadRT = new QPushButton("Load RT");
    m_btnLoadRT->setStyleSheet(rtStyle);
    m_btnLoadRT->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_btnLoadRT->setCursor(Qt::PointingHandCursor);
    m_btnLoadRT->setToolTip("Rolling Transfer");
    connect(m_btnLoadRT, &QPushButton::clicked, this, &MainWindow::onLoadRT);
    actionBtnLayout->addWidget(m_btnLoadRT);

    m_btnClear = new QPushButton("Clear");
    m_btnClear->setStyleSheet(clearStyle);
    m_btnClear->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_btnClear->setCursor(Qt::PointingHandCursor);
    m_btnClear->setToolTip("Clear all period rows and month selections");
    connect(m_btnClear, &QPushButton::clicked, this, &MainWindow::onClearPeriodRows);
    actionBtnLayout->addWidget(m_btnClear);

    actionBtnLayout->addStretch(); // keeps buttons left-aligned at natural width
    yearsMainLayout->addLayout(actionBtnLayout);

    // �� Period rows area (populated by displayPeriodRows when LOAD is clicked) �
    // m_monthYearSelector is the container widget; m_monthYearLayout is its layout.
    // clearPeriodRows() and displayPeriodRows() both use these.
    m_monthYearSelector = new QWidget();
    m_monthYearSelector->setStyleSheet("background: transparent;");
    m_monthYearLayout   = new QVBoxLayout(m_monthYearSelector);
    m_monthYearLayout->setContentsMargins(0, 0, 0, 0);
    m_monthYearLayout->setSpacing(4);
    m_monthYearLayout->addStretch(); // keeps widgets pinned to top
    yearsGroup->setMinimumHeight(250);
    m_monthYearSelector->setMinimumHeight(220);

    m_selectorSectionsSplitter = new QSplitter(Qt::Vertical, selectorCard);
    m_selectorSectionsSplitter->setChildrenCollapsible(false);
    m_selectorSectionsSplitter->setHandleWidth(4);
    m_selectorSectionsSplitter->addWidget(yearsGroup);
    m_selectorSectionsSplitter->addWidget(m_monthYearSelector);
    m_selectorSectionsSplitter->setStretchFactor(0, 3);
    m_selectorSectionsSplitter->setStretchFactor(1, 2);
    m_selectorSectionsSplitter->setSizes({320, 200});
    selectorLayout->addWidget(m_selectorSectionsSplitter, 1);

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
    // Create sidebar for mappings (collapsible left panel) - Premium design
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
    
    // Header with title - Premium styling
    QLabel* title = new QLabel("Active Mappings");
    title->setStyleSheet(
        "font-weight: 700; "
        "font-size: 16px; "
        "color: #1A1F36; "
        "letter-spacing: -0.3px;"
    );
    sidebarLayout->addWidget(title);

    // Select All / Deselect All toggle button - Premium design
    m_btnSelectAll = new QPushButton("Select All");
    m_btnSelectAll->setStyleSheet(
        "QPushButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #6366F1, stop:1 #4F46E5);"
        "  color: white;"
        "  font-weight: 600;"
        "  padding: 8px 16px;"
        "  border-radius: 8px;"
        "  font-size: 11px;"
        "  border: none;"
        "}"
        "QPushButton:hover {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
        "    stop:0 #5558E3, stop:1 #4338CA);"
        "}"
        "QPushButton:pressed {"
        "  background: #4338CA;"
        "}"
    );
    m_btnSelectAll->setMaximumWidth(120);
    m_btnSelectAll->setCursor(Qt::PointingHandCursor);
    m_allSelected = false;
    connect(m_btnSelectAll, &QPushButton::clicked, this, [this]() {
        m_allSelected = !m_allSelected;
        m_btnSelectAll->setText(m_allSelected ? "Deselect All" : "Select All");
        if (m_mappingController) {
            m_mappingController->setAllChecked(m_allSelected);
        }
    });
    sidebarLayout->addWidget(m_btnSelectAll);

    // Separator line
    QFrame* separator = new QFrame();
    separator->setFrameShape(QFrame::HLine);
    separator->setStyleSheet(
        "background: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
        "  stop:0 rgba(0,0,0,0), stop:0.5 rgba(0,0,0,0.08), stop:1 rgba(0,0,0,0));"
        "border: none;"
        "height: 1px;"
    );
    sidebarLayout->addWidget(separator);

    // Mappings scroll area - Premium styling
    m_costControlContainer = new QWidget();
    m_costControlContainer->setStyleSheet("background: transparent;");
    m_costControlLayout = new QVBoxLayout(m_costControlContainer);
    m_costControlLayout->setSpacing(12);
    m_costControlLayout->setContentsMargins(0, 0, 0, 0);

    QScrollArea* scroll = new QScrollArea();
    scroll->setWidget(m_costControlContainer);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(
        "QScrollArea {"
        "  background: transparent;"
        "  border: none;"
        "}"
        "QScrollBar:vertical {"
        "  background: transparent;"
        "  width: 10px;"
        "  border-radius: 5px;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: rgba(0, 0, 0, 0.15);"
        "  border-radius: 5px;"
        "  min-height: 30px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "  background: rgba(0, 0, 0, 0.25);"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "  height: 0px;"
        "}"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
        "  background: transparent;"
        "}"
    );
    sidebarLayout->addWidget(scroll, 1);

    // No mappings label - Premium styling
    m_noMappingsLabel = new QLabel("No mappings loaded.\nSelect periods and click Load.");
    m_noMappingsLabel->setStyleSheet(
        "color: #8B92A7;"
        "font-style: italic;"
        "font-size: 12px;"
        "padding: 40px 20px;"
        "background: rgba(255, 255, 255, 0.5);"
        "border-radius: 12px;"
        "border: 2px dashed #D1D5DB;"
    );
    m_noMappingsLabel->setAlignment(Qt::AlignCenter);
    sidebarLayout->addWidget(m_noMappingsLabel);
    
    // Store reference for later use (don't add to main layout yet)
    m_mappingsCard = m_mappingsSidebar;
}

void MainWindow::createIndividualTab()
{
    m_individualTransferTab = new IndividualTransferTab(this);
    m_tabWidget->addTab(m_individualTransferTab, "Individual Transfer");
}

void MainWindow::createHybridTab()
{
    m_hybridTransferTab = new HybridTransferTab(this);
    m_tabWidget->addTab(m_hybridTransferTab, "Hybrid Transfer");
    
    connect(m_hybridTransferTab, &HybridTransferTab::executeRequested,
            this, &MainWindow::onHybridExecute);
}

void MainWindow::onHybridExecute(const HybridTransferConfig& config)
{
    if (guardBusy("HybridTransfer")) return;

    if (m_hybridWorker) {
        m_hybridWorker->stop();
        m_hybridWorker->deleteLater();
        m_hybridWorker = nullptr;
    }

    m_hybridWorker = new HybridWorker(this, this);

    // Create hybrid progress dialog
    auto* hybridDialog = new QProgressDialog(
        "Hybrid Transfer starting...", QString(), 0, 100, this);
    hybridDialog->setWindowTitle("Hybrid Transfer Progress");
    hybridDialog->setWindowModality(Qt::NonModal);
    hybridDialog->setWindowFlags(hybridDialog->windowFlags() | Qt::WindowStaysOnTopHint);
    hybridDialog->setMinimumDuration(0);
    hybridDialog->setAutoClose(false);
    hybridDialog->setAutoReset(false);
    hybridDialog->setCancelButton(nullptr);
    hybridDialog->setMinimumWidth(420);
    hybridDialog->setValue(0);
    hybridDialog->show();

    connect(m_hybridWorker, &HybridWorker::phaseStarted,
        this, [this, hybridDialog](const QString& phase) {
            updateStatusBar(QString("Hybrid: Starting %1...").arg(phase));
            qInfo() << "[Hybrid] Phase started:" << phase;
            if (hybridDialog) {
                hybridDialog->setLabelText(QString("Phase: %1\nStarting...").arg(phase));
            }
            if (m_hybridTransferTab) {
                m_hybridTransferTab->onPhaseStarted(phase);
            }
        });

    connect(m_hybridWorker, &HybridWorker::phaseFinished,
        this, [this, hybridDialog](const QString& phase, bool success) {
            QString status = success ? "completed" : "FAILED";
            updateStatusBar(QString("Hybrid: %1 %2").arg(phase, status));
            qInfo() << "[Hybrid] Phase finished:" << phase << status;
            if (hybridDialog) {
                hybridDialog->setLabelText(
                    QString("Phase: %1 — %2").arg(phase, status));
            }
            if (m_hybridTransferTab) {
                m_hybridTransferTab->onPhaseFinished(phase, success);
            }
        });

    connect(m_hybridWorker, &HybridWorker::progressUpdate,
        this, [this, hybridDialog](int percent, const QString& msg) {
            m_progressBar->setVisible(true);
            m_progressBar->setValue(percent);
            updateStatusBar(msg);
            if (hybridDialog) {
                hybridDialog->setValue(percent);
                hybridDialog->setLabelText(msg);
            }
            if (m_hybridTransferTab) {
                m_hybridTransferTab->onProgressUpdate(percent, msg);
            }
        });

    connect(m_hybridWorker, &HybridWorker::allFinished,
        this, [this, hybridDialog](bool success, const QString& summary) {
            m_progressBar->setVisible(false);
            updateStatusBar(success
                ? "Hybrid transfer completed successfully"
                : "Hybrid transfer completed with errors");
            qInfo().noquote() << "[Hybrid]" << summary;

            if (hybridDialog) {
                hybridDialog->setValue(100);
                hybridDialog->setLabelText(success
                    ? "Hybrid transfer completed successfully!"
                    : "Hybrid transfer completed with errors.");
                hybridDialog->close();
                hybridDialog->deleteLater();
            }

            showToast(success
                ? "Hybrid transfer complete!"
                : "Hybrid transfer finished with errors",
                success ? ToastWidget::Success : ToastWidget::Warning,
                5000);

            if (m_hybridTransferTab) {
                m_hybridTransferTab->onAllFinished(success, summary);
            }

            m_hybridWorker->deleteLater();
            m_hybridWorker = nullptr;
        });

    m_hybridWorker->execute(config);
}

void MainWindow::updateStatusBar(const QString& message)
{
    m_statusLabel->setText(message);
}

void MainWindow::showToast(const QString& message, ToastWidget::ToastType type, int duration)
{
    if (!m_toastWidget) {
        m_toastWidget = new ToastWidget(this);
        connect(m_toastWidget, &QObject::destroyed, this, [this]() {
            m_toastWidget = nullptr;
        });
    }
    if (m_toastWidget) {
        m_toastWidget->showToast(message, type, duration);
    }
}

// onGeneratePeriodRows() removed  ? period rows now auto-generate when year checkboxes are toggled.
// The connect() in createMonthYearSelector() calls m_periodController->generatePeriodRows() directly.

bool MainWindow::guardBusy(const QString& action)
{
    if (isBusy()) {
        qDebug() << "[GUARD] BLOCKED action:" << action << "(busy)";
        statusBar()->showMessage("⏳ Please wait — operation in progress...", 2000);
        return true;  // caller should return immediately
    }
    return false;
}

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

void MainWindow::clearAllSelections()
{
    // Deselect all months in all year cards
    for (auto it = m_yearCards.begin(); it != m_yearCards.end(); ++it) {
        YearCard* card = it.value();
        if (!card) continue;
        card->blockSignals(true);
        card->deselectAllMonths();
        card->blockSignals(false);
    }

    // Also clear the period model
    if (m_periodModel) {
        for (const YearEntry& entry : m_periodModel->years()) {
            for (int m = 0; m < 12; m++) {
                if (m < MONTHS_LIST.size()) {
                    m_periodModel->setMonthSelected(entry.year, MONTHS_LIST[m], false);
                }
            }
        }
    }
}

void MainWindow::selectPeriod(const QString& month, int year)
{
    // Find the month index
    int monthIdx = MONTHS_LIST.indexOf(month);
    if (monthIdx < 0) return;

    // Select in year card
    YearCard* card = m_yearCards.value(year, nullptr);
    if (card) {
        card->blockSignals(true);
        card->setExpanded(true);
        card->selectMonth(monthIdx);
        card->blockSignals(false);
    }

    // Select in period model
    if (m_periodModel) {
        m_periodModel->setMonthSelected(year, month, true);
    }
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
                if (checked && m_generatePeriodTimer)
                    m_generatePeriodTimer->start();
            }
        });
        connect(card, &YearCard::createMonthFilesRequested,
                this, &MainWindow::onCreateMonthFilesMode);
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
    if (guardBusy("LoadSelectedPeriods")) return;
    // If create files mode is active, redirect to file creation instead of loading
    if (m_createFilesMode && m_createFilesYear > 0) {
        onCreateMonthFile();
        return;
    }

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
    // Status log is only shown when user clicks the Log toggle button

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
        const QString summary = QString("Ready %1/%2 files pre-loaded, %3 mapping(s)")
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

    // Safety guard: too many periods = stack overflow when building mapping rows
    // Each period = 9 mappings. Max safe = 5 periods = 45 mappings.
    if (periods.size() > 5) {
        QMessageBox::warning(this, "Too Many Periods Selected",
            QString("You have selected %1 months. Maximum is 5 months at a time.\n\nPlease deselect some months and try again.")
                .arg(periods.size()),
            QMessageBox::Ok);
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
    if (guardBusy("ExecuteAll")) return;
    if (m_isTransferRunning) {
        qInfo() << "onExecuteAll BLOCKED (re-entrant, transfer already running)";
        return;
    }
    // Guard: check selected periods count FIRST before doing anything else
    {
        QVector<QPair<QString,int>> periods = getSelectedPeriods();
        if (periods.size() > 5) {
            QMessageBox::warning(this, "Too Many Periods Selected",
                QString("You have selected %1 months. Maximum is 5 at a time.\n\nPlease deselect some months and try again.")
                    .arg(periods.size()),
                QMessageBox::Ok);
            return;
        }
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
        showToast("No mapping cards selected. Please check at least one mapping card to proceed.", ToastWidget::Warning);
        return;
    }

    // Log panel stays hidden — user controls via Log toggle button
    qInfo() << "onExecuteAll [2] sidebar shown";

    m_isTransferRunning = true;
    m_isTransferPaused = false;
    m_isTransferStopRequested = false;
    m_transferSuccessfulMappings = 0;
    if (m_busyTimeout) m_busyTimeout->start(10 * 60 * 1000); // 10-minute safety net
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

        // TODO: TECHNICAL DEBT - These widget syncs are a workaround for model staleness.
        // Long-term fix: Connect widget change signals to syncRowToModel() so model
        // is always current, then remove these patches and trust model as single source of truth.
        tm.entry.copyFullSheet = row->isCopyFullSheet();
        tm.entry.customSheetName = row->getCustomSheetName();
        tm.entry.insertAfterSheet = row->getInsertAfterSheet();
        tm.entry.ignoredDestRows = row->ignoredRows();

        tm.destPath = findCostControlPath(item.month, item.year);
        if (tm.destPath.isEmpty()) {
            m_transferFailedMappings.append(QString("%1 %2").arg(item.month).arg(item.year));
            continue;
        }
        transferMappings.append(tm);
    }

    qInfo() << "onExecuteAll [8] transfer list built, size=" << transferMappings.size();
    m_transferTotalMappings = transferMappings.size();
    auto abortExecutionSetup = [this]() {
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
    };

    // Safety guard: block execution if too many mappings selected at once.
    // Each mapping loads Excel files and runs transfers — too many causes stack overflow.
    // Normal use: 1 month = 9 mappings. Max safe: ~5 months = 45 mappings.
    if (transferMappings.size() > 45) {
        QMessageBox::warning(
            this, "Too Many Mappings Selected",
            QString("You have selected %1 mappings (%2 months × 9 mappings each).\n\n"
                    "The maximum safe limit is 45 mappings (5 months).\n\n"
                    "Please deselect some months and try again.")
                .arg(transferMappings.size())
                .arg(transferMappings.size() / 9),
            QMessageBox::Ok
        );
        m_isTransferRunning = false;
        m_btnExecuteAll->setEnabled(true);
        m_btnPause->setEnabled(false);
        m_btnStop->setEnabled(false);
        m_progressBar->setVisible(false);
        if (m_transferDialog) { m_transferDialog->close(); m_transferDialog->deleteLater(); m_transferDialog = nullptr; }
        return;
    }

    if (transferMappings.isEmpty()) {
        abortExecutionSetup();
        showToast("No selected mappings to execute. Check if the file exists in the destination folder.", ToastWidget::Warning);
        return;
    }

    // Preflight workbook validation with replacement option for corrupt/non-ZIP files.
    qInfo() << "onExecuteAll [8b] validating workbook files";
    QMap<QString, QString> resolvedPaths;
    for (TransferMapping& tm : transferMappings) {
        QString resolvedDest;
        const QString destLabel = QString("Destination workbook for %1 %2")
                                      .arg(tm.month)
                                      .arg(tm.year);
        if (!resolveWorkbookPath(this, destLabel, tm.destPath, resolvedPaths, &resolvedDest)) {
            abortExecutionSetup();
            showToast("Transfer cancelled by user.", ToastWidget::Warning, 4000);
            return;
        }
        tm.destPath = resolvedDest;

        if (!tm.entry.sourcePath.trimmed().isEmpty()) {
            QString resolvedSource;
            const QString sourceLabel = QString("Source workbook for %1 %2 (%3)")
                                            .arg(tm.month)
                                            .arg(tm.year)
                                            .arg(tm.entry.sourceFileType);
            if (!resolveWorkbookPath(this, sourceLabel,
                                     tm.entry.sourcePath, resolvedPaths, &resolvedSource)) {
                abortExecutionSetup();
                showToast("Transfer cancelled by user.", ToastWidget::Warning, 4000);
                return;
            }
            tm.entry.sourcePath = resolvedSource;
        }
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
            showToast(QString("File is open in Excel, please close it first:\n%1")
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

// ─────────────────────────────────────────────────────────────────────────────
// executeAllWithItems  –  Option B for HybridWorker: skip load step, transfer
// directly using pre-collected mapping items from the Hybrid tab sidebar.
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::executeAllWithItems(const QVector<MappingItem>& items)
{
    if (m_isTransferRunning) {
        qInfo() << "executeAllWithItems BLOCKED (transfer already running)";
        return;
    }
    if (items.isEmpty()) {
        qWarning() << "executeAllWithItems: no items provided";
        emit transferFinished(false, "No mapping items to execute.");
        return;
    }

    m_isTransferRunning = true;
    m_isTransferStopRequested = false;
    m_transferSuccessfulMappings = 0;
    m_transferFailedMappings.clear();

    // Clear full-sheet overrides
    if (m_excelHandler) {
        for (const MappingItem& item : items) {
            const QString destKey = QString("%1_%2_cost_control")
                                        .arg(item.month).arg(item.year);
            m_excelHandler->resetOverrides(destKey);
        }
    }

    // Build TransferMapping list from provided items
    QVector<TransferMapping> transferMappings;
    transferMappings.reserve(items.size());
    for (int i = 0; i < items.size(); ++i) {
        const MappingItem& item = items[i];
        TransferMapping tm;
        tm.entry    = item.entry;
        tm.year     = item.year;
        tm.month    = item.month.isEmpty() ? item.entry.month : item.month;
        tm.rowIndex = i;
        tm.destPath = findCostControlPath(tm.month, tm.year);
        if (tm.destPath.isEmpty()) {
            qWarning() << "executeAllWithItems: no dest path for" << tm.month << tm.year;
        }
        transferMappings.append(tm);
    }

    auto abortSetup = [this](const QString& reason) {
        m_isTransferRunning = false;
        emit transferFinished(false, reason);
    };

    if (transferMappings.isEmpty()) {
        abortSetup("No transfer mappings could be built.");
        return;
    }

    // Path resolution (same as onExecuteAll)
    QMap<QString, QString> resolvedPaths;
    for (TransferMapping& tm : transferMappings) {
        QString resolvedDest;
        if (tm.destPath.isEmpty()) {
            // keep empty — TransferWorker will skip
            continue;
        }
        const QString destLabel = QString("Destination workbook for %1 %2")
                                      .arg(tm.month).arg(tm.year);
        if (!resolveWorkbookPath(this, destLabel, tm.destPath, resolvedPaths, &resolvedDest)) {
            abortSetup("Transfer cancelled by user.");
            return;
        }
        tm.destPath = resolvedDest;

        if (!tm.entry.sourcePath.trimmed().isEmpty()) {
            QString resolvedSource;
            const QString sourceLabel = QString("Source workbook for %1 %2 (%3)")
                                            .arg(tm.month).arg(tm.year)
                                            .arg(tm.entry.sourceFileType);
            if (!resolveWorkbookPath(this, sourceLabel,
                                     tm.entry.sourcePath, resolvedPaths, &resolvedSource)) {
                abortSetup("Transfer cancelled by user.");
                return;
            }
            tm.entry.sourcePath = resolvedSource;
        }
    }

    // Clean up previous worker
    if (m_transferWorker) {
        m_transferWorker->stop();
        if (!m_transferWorker->wait(3000)) {
            m_transferWorker->terminate();
            m_transferWorker->wait();
        }
        delete m_transferWorker;
        m_transferWorker = nullptr;
    }

    m_transferTotalMappings = transferMappings.size();
    m_transferWorker = new TransferWorker(this);
    m_transferWorker->setMappings(transferMappings, m_transferService, m_destFolder);

    connect(m_transferWorker, &TransferWorker::progress,
            this, &MainWindow::onTransferProgress);
    connect(m_transferWorker, &TransferWorker::rowDone,
            this, &MainWindow::onTransferRowDone);
    connect(m_transferWorker, &TransferWorker::finished,
            this, &MainWindow::onTransferFinished);

    qInfo() << "executeAllWithItems: starting worker with" << transferMappings.size() << "mappings";
    m_transferWorker->start();
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
    if (m_busyTimeout) m_busyTimeout->stop();
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
        // Log panel stays hidden — user controls via Log toggle button
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
    
    // Emit signal for HybridWorker
    bool success = m_transferFailedMappings.isEmpty();
    emit transferFinished(success,
        QString("%1 cells, %2 executed").arg(totalCells).arg(executed));
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

void MainWindow::loadRTForHybrid(const QVector<QPair<QString,int>>& periods)
{
    // Called from hybrid worker. Resets busy flags, injects periods directly
    // (bypasses getSelectedPeriods() which reads UI checkboxes that may not
    // yet be updated when this runs).
    m_isTransferRunning = false;
    m_isLoadingPeriods  = false;
    m_isLoadingRT       = false;
    m_fillAllRunning    = false;

    if (guardBusy("LoadRT")) return;
    if (!m_rollingService) return;

    m_isLoadingRT = true;
    if (m_busyTimeout) m_busyTimeout->start(10 * 60 * 1000);

    if (periods.isEmpty()) {
        showToast("No RT periods provided.", ToastWidget::Warning);
        m_isLoadingRT = false;
        return;
    }

    m_rollingChain = m_rollingService->buildChain(m_destFolder, periods);

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
        const QString msg = QString("Rolling Transfer cancelled — source file(s) not found:\n%1").arg(missing.join("\n"));
        qWarning() << msg;
        updateStatusBar("RT Load failed — source file not found");
        m_isLoadingRT = false;
        return;
    }

    m_btnRollingTransfer->setEnabled(true);

    // ── Load mapping cards so onRollingTransfer() sees checked items ──
    m_mappingController->clearAllMappings();

    QSet<int> yearsWithOldMappings;
    QSet<int> yearsWithNewMappings;
    for (const auto& period : periods) {
        int year = period.second;
        if (year <= 2025) yearsWithOldMappings.insert(year);
        else              yearsWithNewMappings.insert(year);
    }

    const QString jsonBase = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../JSON");
    const QString mappingsOldPath   = QString("%1/mappings_old.json").arg(jsonBase);
    const QString mappingsNewPath   = QString("%1/mappings.json").arg(jsonBase);
    const QString sapYtdPath        = QString("%1/mappings_sap_ytd.json").arg(jsonBase);
    const QString paxPath           = QString("%1/pax.json").arg(jsonBase);
    const QString staffPath         = QString("%1/staff.json").arg(jsonBase);
    const QString budgetRefiPath    = QString("%1/mappings_budget_refi_prev_year.json").arg(jsonBase);
    const QString paxTransferPath   = QString("%1/mappings_pax_transfer.json").arg(jsonBase);
    const QString trafficMottPath   = QString("%1/Traffic_mott.json").arg(jsonBase);

    if (!yearsWithOldMappings.isEmpty()) m_mappingsManager->loadMappings(mappingsOldPath);
    if (!yearsWithNewMappings.isEmpty()) m_mappingsManager->loadMappings(mappingsNewPath);

    m_mappingsManager->loadSapYtdMappings(sapYtdPath);
    m_mappingsManager->loadPaxMappings(paxPath);
    m_mappingsManager->loadStaffMappings(staffPath);
    m_mappingsManager->loadBudgetRefiMappings(budgetRefiPath);
    m_mappingsManager->loadPaxTransferMappings(paxTransferPath);
    m_mappingsManager->loadTrafficMottMappings(trafficMottPath);

    for (const auto& period : periods) {
        const QString& month = period.first;
        int year = period.second;

        for (MappingEntry entry : m_mappingsManager->getMappingsForMonthYear(month, year)) {
            if (entry.sourceFileType == "sap_ytd") continue;
            entry.sourceJson = (year <= 2025) ? mappingsOldPath : mappingsNewPath;
            if (entry.sourceFileType == "sap")
                entry.sourcePath = m_excelHandler->findSAPFile(m_destFolder, month, year);
            else
                entry.sourcePath = findCostControlPath(month, year);
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

        if (m_mappingsManager) {
            for (MappingEntry entry : m_mappingsManager->getPaxTransferMappingsForMonthYear(month, year)) {
                entry.sourceJson = paxTransferPath;
                entry.sourcePath = m_excelHandler->findPaxFile(m_destFolder, month, year);
                m_mappingController->addMappingRow(month, year, entry);
            }
        }

        if (m_mappingsManager) {
            for (MappingEntry entry : m_mappingsManager->getTrafficMottMappingsForMonthYear(month, year)) {
                entry.sourceJson = trafficMottPath;
                entry.sourcePath = m_excelHandler->findPaxFile(m_destFolder, month, year);
                m_mappingController->addMappingRow(month, year, entry);
            }
        }
    }

    m_noMappingsLabel->setVisible(m_mappingController->mappingCount() == 0);
    m_mappingController->setAllChecked(true);  // auto-check so onRollingTransfer() sees them
    qInfo() << "[loadRTForHybrid] Loaded" << m_mappingController->mappingCount() << "mapping cards (all checked)";
    // ── End mapping card loading ──

    m_isLoadingRT = false;
    qInfo() << "[loadRTForHybrid] RT chain built:" << m_rollingChain.size() << "steps";
    emit rtChainReady();
}

void MainWindow::onLoadRT()
{
    if (guardBusy("LoadRT")) return;
    if (m_isLoadingRT || m_isLoadingPeriods) {
        qInfo() << "onLoadRT BLOCKED (re-entrant)";
        return;
    }
    if (!m_rollingService) return;
    m_isLoadingRT = true;
    if (m_busyTimeout) m_busyTimeout->start(10 * 60 * 1000); // 10-minute safety net

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
    const QString paxTransferPath = QString("%1/mappings_pax_transfer.json").arg(jsonBase);
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
    m_mappingsManager->loadPaxTransferMappings(paxTransferPath);
    m_mappingsManager->loadTrafficMottMappings(trafficMottPath);

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

        // 6. PAX Transfer rows (Sheet1 → TRAFFIC mott sheet, no division)
        if (m_mappingsManager) {
            for (MappingEntry entry : m_mappingsManager->getPaxTransferMappingsForMonthYear(month, year)) {
                entry.sourceJson = paxTransferPath;
                entry.sourcePath = m_excelHandler->findPaxFile(m_destFolder, month, year);
                m_mappingController->addMappingRow(month, year, entry);
            }
        }

        // 7. Traffic mott rows (Sheet1 → TRAFFIC mott sheet in cost control year/month)
        if (m_mappingsManager) {
            for (MappingEntry entry : m_mappingsManager->getTrafficMottMappingsForMonthYear(month, year)) {
                entry.sourceJson = trafficMottPath;
                entry.sourcePath = m_excelHandler->findPaxFile(m_destFolder, month, year);
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
    updateStatusBar(QString("RT Ready: %1 click Execute RT to start").arg(stepList.join(" � ")));
    qInfo() << "RT chain built:" << stepList.join(" � ");

    m_isLoadingRT = false;
    if (m_busyTimeout) m_busyTimeout->stop();
}

void MainWindow::onRollingTransfer()
{
    if (!m_rollingService) return;
    if (m_rollingChain.isEmpty()) {
        showToast("Click Load RT first.", ToastWidget::Warning);
        return;
    }
    
    // Check if at least one mapping card is checked
    bool anyChecked = false;
    if (m_mappingController) {
        for (int i = 0; i < m_mappingController->mappingCount(); ++i) {
            MappingRow* row = m_mappingController->rowAt(i);
            if (row && row->isChecked()) {
                anyChecked = true;
                break;
            }
        }
    }
    if (!anyChecked) {
        showToast("No mapping cards selected. Please check at least one mapping card to proceed.", ToastWidget::Warning);
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
        
        // Emit signal for HybridWorker
        bool success = result.errors.isEmpty();
        emit rollingTransferFinished(success,
            QString("%1/%2 months, %3 cells").arg(result.successfulMonths)
                .arg(result.totalMonths).arg(result.totalCells));
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
        const QString paxTransferPath = QString("%1/mappings_pax_transfer.json").arg(jsonBase);
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
        m_mappingsManager->loadPaxTransferMappings(paxTransferPath);
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

            for (MappingEntry entry : m_mappingsManager->getPaxTransferMappingsForMonthYear(month, year)) {
                entry.sourceJson = paxTransferPath;
                entry.sourcePath = m_excelHandler->findPaxFile(m_destFolder, month, year);
                allEntriesByMonth[key].append(entry);
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

    qInfo() << "RT: validating workbook path dependencies";
    QMap<QString, QString> resolvedPaths;
    
    if (!m_rollingChain.isEmpty()) {
        QString resolvedFirst;
        const QString firstLabel = QString("Previous month workbook (Source for %1 %2)")
                                       .arg(m_rollingChain.first().month)
                                       .arg(m_rollingChain.first().year);
        if (!resolveWorkbookPath(this, firstLabel, m_rollingChain.first().inputPath, resolvedPaths, &resolvedFirst)) {
            m_progressBar->setVisible(false);
            if (m_rtDialog) {
                m_rtDialog->reset();
                m_rtDialog->hide();
                m_rtDialog->deleteLater();
                m_rtDialog = nullptr;
            }
            m_btnRollingTransfer->setEnabled(true);
            m_btnStop->setEnabled(false);
            m_btnLoad->setEnabled(true);
            m_btnLoadRT->setEnabled(true);
            showToast("RT cancelled by user.", ToastWidget::Warning, 4000);
            return;
        }
        m_rollingChain[0].inputPath = resolvedFirst;
    }

    for (auto it = allEntriesByMonth.begin(); it != allEntriesByMonth.end(); ++it) {
        QVector<MappingEntry>& entries = it.value();
        for (MappingEntry& entry : entries) {
            if (!entry.sourcePath.trimmed().isEmpty()) {
                QString usageLabel = QString("RT source workbook: %1 (%2)")
                                         .arg(it.key(), entry.sourceFileType);
                
                QString resolvedSource;
                if (!resolveWorkbookPath(this, usageLabel, entry.sourcePath, resolvedPaths, &resolvedSource)) {
                    m_progressBar->setVisible(false);
                    if (m_rtDialog) {
                        m_rtDialog->reset();
                        m_rtDialog->hide();
                        m_rtDialog->deleteLater();
                        m_rtDialog = nullptr;
                    }
                    m_btnRollingTransfer->setEnabled(true);
                    m_btnStop->setEnabled(false);
                    m_btnLoad->setEnabled(true);
                    m_btnLoadRT->setEnabled(true);
                    showToast("RT cancelled by user.", ToastWidget::Warning, 4000);
                    return;
                }
                entry.sourcePath = resolvedSource;
            }
        }
    }

    qInfo() << "RT: using entries for months:" << allEntriesByMonth.keys();
    RollingWorker* worker = new RollingWorker(m_rollingService, m_rollingChain, m_destFolder, jsonBase, allEntriesByMonth, this);
    connect(worker, &RollingWorker::finished, this, [this, worker](const RollingResult&) {
        worker->deleteLater();
    });
    worker->start();
}

void MainWindow::onCreateMonthFile()
{
    // Called when Load Periods is clicked in Create Files mode.
    // Finds the last existing Cost Control xlsm for m_createFilesYear,
    // then copies it for each selected month that doesn't already have a file.

    int year = m_createFilesYear;
    QVector<QPair<QString, int>> periods = getSelectedPeriods();

    // Filter to only the create-mode year
    QVector<QString> selectedMonths;
    for (const auto& p : periods) {
        if (p.second == year) selectedMonths.append(p.first);
    }

    if (selectedMonths.isEmpty()) {
        showToast(QString("No months selected for %1. Select months first.").arg(year),
                  ToastWidget::Warning, 3000);
        return;
    }

    // Find the last existing Cost Control file for this year (scan Dec → Jan)
    QString srcPath;
    QString srcMonthName;
    for (int m = 12; m >= 1; --m) {
        QString mn = QString("%1").arg(m, 2, 10, QChar('0'));
        QString candidate = QString("%1/%2/%3/test/Cost Control ZAG %3_%2_working.xlsm")
                                .arg(m_destFolder).arg(year).arg(mn);
        if (QFile::exists(candidate)) {
            srcPath = candidate;
            srcMonthName = MONTHS_LIST[m - 1];
            break;
        }
    }

    if (srcPath.isEmpty()) {
        // No existing file found — ask user to pick one manually
        showToast(QString("No existing Cost Control file found for %1. Please select one manually.").arg(year),
                  ToastWidget::Warning, 3000);
        srcPath = QFileDialog::getOpenFileName(
            this,
            QString("Select source Cost Control file for %1").arg(year),
            m_destFolder,
            "Excel Files (*.xlsm *.xlsx)"
        );
        if (srcPath.isEmpty()) return;
        srcMonthName = "selected file";
    }

    // Build list of files to create
    QStringList toCreate, alreadyExist, failed, created;
    for (const QString& month : selectedMonths) {
        QString mn = MONTH_TO_NUM.value(month, "01");
        QString destDir  = QString("%1/%2/%3/test").arg(m_destFolder).arg(year).arg(mn);
        QString destFile = QString("Cost Control ZAG %1_%2_working.xlsm").arg(mn).arg(year);
        QString destPath = QString("%1/%2").arg(destDir).arg(destFile);
        if (QFile::exists(destPath)) {
            alreadyExist << QString("%1 %2").arg(month).arg(year);
        } else {
            toCreate << month;
        }
    }

    if (toCreate.isEmpty()) {
        showToast("All selected months already have files — nothing to create.", ToastWidget::Info, 3000);
        return;
    }

    // Confirm
    QString confirmMsg = QString("Create files for: %1\n\nSource: %2\n\nAlready exist (skipped): %3")
        .arg(toCreate.join(", "))
        .arg(srcPath)
        .arg(alreadyExist.isEmpty() ? "none" : alreadyExist.join(", "));

    if (QMessageBox::question(this, QString("Create %1 files for %2").arg(toCreate.size()).arg(year),
                              confirmMsg,
                              QMessageBox::Yes | QMessageBox::Cancel) != QMessageBox::Yes) return;

    // Create each file
    for (const QString& month : toCreate) {
        QString mn = MONTH_TO_NUM.value(month, "01");
        QString destDir  = QString("%1/%2/%3/test").arg(m_destFolder).arg(year).arg(mn);
        QString destFile = QString("Cost Control ZAG %1_%2_working.xlsm").arg(mn).arg(year);
        QString destPath = QString("%1/%2").arg(destDir).arg(destFile);

        QDir().mkpath(destDir);
        if (QFile::copy(srcPath, destPath)) {
            created << QString("%1 %2").arg(month).arg(year);
            qInfo() << "[CREATE MONTH FILE] OK:" << destPath;
        } else {
            failed << QString("%1 %2").arg(month).arg(year);
            qWarning() << "[CREATE MONTH FILE] Failed:" << destPath;
        }
    }

    // Report results
    QString result = QString("Created %1 file(s) from %2 %3.")
        .arg(created.size()).arg(srcMonthName).arg(year);
    if (!failed.isEmpty())
        result += QString("\nFailed: %1").arg(failed.join(", "));
    if (!alreadyExist.isEmpty())
        result += QString("\nSkipped (already exist): %1").arg(alreadyExist.join(", "));

    updateStatusBar(result);
    showToast(result, failed.isEmpty() ? ToastWidget::Success : ToastWidget::Warning, 5000);
    qInfo() << "[CREATE MONTH FILE]" << result;
}

void MainWindow::onIgnoreRows(int index)
{
    if (!m_mappingController) return;
    MappingRow* row = m_mappingController->rowAt(index);
    if (!row) return;

    MappingEntry entry = row->getMapping();
    if (entry.rowMap.isEmpty()) {
        showToast("This mapping has no rowMap — nothing to ignore.", ToastWidget::Info, 3000);
        return;
    }

    // Find the loaded dest workbook key for current value preview
    // Use the first loaded cost_control workbook
    QString destKey = m_currentDestKey; // set during load phase

    IgnoreRowsDialog dlg(entry, m_excelHandler, destKey, this);
    if (dlg.exec() == QDialog::Accepted) {
        entry.ignoredDestRows = dlg.ignoredDestRows();
        row->setIgnoredRows(dlg.ignoredDestRows());
        
        // ✅ FIX: Sync all widget state back to model using centralized helper
        m_mappingController->syncRowToModel(index);

        int count = entry.ignoredDestRows.size();
        if (count > 0)
            showToast(QString("%1 rows will be skipped for this mapping.").arg(count),
                      ToastWidget::Info, 3000);
        else
            showToast("No rows ignored — all rows will be transferred.", ToastWidget::Success, 2000);
    }
}

void MainWindow::onCreateMonthFilesMode(int year, bool active)
{
    m_createFilesMode = active;
    m_createFilesYear = active ? year : 0;

    // Disable/enable Load buttons to prevent normal load while in create mode
    if (m_btnLoad)   m_btnLoad->setEnabled(!active);
    if (m_btnLoadRT) m_btnLoadRT->setEnabled(!active);

    if (active) {
        updateStatusBar(QString("Create Files mode ON for %1 — select months then click Load Periods").arg(year));
        showToast(QString("Create Files mode: select months for %1, then click Load Periods").arg(year),
                  ToastWidget::Info, 4000);
        // Re-enable Load button — it will now CREATE files instead of loading mappings
        if (m_btnLoad) m_btnLoad->setEnabled(true);
    } else {
        updateStatusBar("Create Files mode OFF — normal load restored");
        if (m_btnLoad)   m_btnLoad->setEnabled(true);
        if (m_btnLoadRT) m_btnLoadRT->setEnabled(true);
    }
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
        m_mappingController->syncRowToModel(index);

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
        // Log panel stays hidden — user controls via Log toggle button
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
        // Log panel stays hidden — user controls via Log toggle button
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

        row->setRowMap(entry.rowMap);
        m_mappingController->updateEntryAt(index, entry);

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
    // Log panel stays hidden — user controls via Log toggle button
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
        // Headcount rows 12, 13, 16 — never divide by 1000
        const bool isHeadcountRow = (destRow == 12 || destRow == 13 || destRow == 16);
        if (!isHeadcountRow) {
            total /= 1000.0;
        }
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

void MainWindow::populateFillAllMappingCards(MappingController* controller,
                                              const QString& month, int year)
{
    if (!controller || !m_mappingsManager) return;

    const QString jsonBase = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../JSON");
    const QString mappingsOldPath  = QString("%1/mappings_old.json").arg(jsonBase);
    const QString mappingsNewPath  = QString("%1/mappings.json").arg(jsonBase);
    const QString sapYtdPath       = QString("%1/mappings_sap_ytd.json").arg(jsonBase);
    const QString paxPath          = QString("%1/pax.json").arg(jsonBase);
    const QString staffPath        = QString("%1/staff.json").arg(jsonBase);
    const QString budgetRefiPath   = QString("%1/mappings_budget_refi_prev_year.json").arg(jsonBase);
    const QString paxTransferPath  = QString("%1/mappings_pax_transfer.json").arg(jsonBase);
    const QString trafficMottPath  = QString("%1/Traffic_mott.json").arg(jsonBase);

    // Load all JSON files (same as onLoadSelectedPeriods)
    if (year <= 2025)
        m_mappingsManager->loadMappings(mappingsOldPath);
    else
        m_mappingsManager->loadMappings(mappingsNewPath);

    m_mappingsManager->loadSapYtdMappings(sapYtdPath);
    m_mappingsManager->loadPaxMappings(paxPath);
    m_mappingsManager->loadStaffMappings(staffPath);
    m_mappingsManager->loadBudgetRefiMappings(budgetRefiPath);
    m_mappingsManager->loadPaxTransferMappings(paxTransferPath);
    m_mappingsManager->loadTrafficMottMappings(trafficMottPath);

    if (!m_mappingService) m_mappingService = new MappingService(m_mappingsManager, this);

    // 1. Cost control / SAP rows
    for (MappingEntry entry : m_mappingsManager->getMappingsForMonthYear(month, year)) {
        if (entry.sourceFileType == "sap_ytd") continue;
        entry.sourceJson = (year <= 2025) ? mappingsOldPath : mappingsNewPath;
        entry.sourcePath = (entry.sourceFileType == "sap")
                           ? m_excelHandler->findSAPFile(m_destFolder, month, year)
                           : findCostControlPath(month, year);
        controller->addMappingRow(month, year, entry);
    }
    // 2. Budget / REFI
    for (MappingEntry entry : m_mappingsManager->getDynamicMappingsForMonthYear(month, year)) {
        entry.sourceJson = budgetRefiPath;
        entry.sourcePath = findCostControlPath(month, year);
        controller->addMappingRow(month, year, entry);
    }
    // 3. PAX
    if (m_mappingService) {
        for (auto sel : m_mappingService->collectPaxMappings({{month, year}})) {
            sel.entry.sourceJson = paxPath;
            sel.entry.sourcePath = m_excelHandler->findPaxFile(m_destFolder, month, year);
            controller->addMappingRow(sel.month, sel.year, sel.entry);
        }
        // 4. Staff
        for (auto sel : m_mappingService->collectStaffMappings({{month, year}})) {
            sel.entry.sourceJson = staffPath;
            sel.entry.sourcePath = m_excelHandler->findStaffFile(m_destFolder, year);
            controller->addMappingRow(sel.month, sel.year, sel.entry);
        }
    }
    // 5. SAP YTD
    for (MappingEntry entry : m_mappingsManager->getSapYtdMappingsForMonthYear(month, year)) {
        entry.sourceJson = sapYtdPath;
        entry.sourcePath = m_excelHandler->findSapYtdFile(m_destFolder, month, year);
        controller->addMappingRow(month, year, entry);
    }
    // 6. PAX Transfer
    for (MappingEntry entry : m_mappingsManager->getPaxTransferMappingsForMonthYear(month, year)) {
        entry.sourceJson = paxTransferPath;
        entry.sourcePath = m_excelHandler->findPaxFile(m_destFolder, month, year);
        controller->addMappingRow(month, year, entry);
    }
    // 7. Traffic Mott
    for (MappingEntry entry : m_mappingsManager->getTrafficMottMappingsForMonthYear(month, year)) {
        entry.sourceJson = trafficMottPath;
        entry.sourcePath = m_excelHandler->findPaxFile(m_destFolder, month, year);
        controller->addMappingRow(month, year, entry);
    }
}

void MainWindow::createComparatorTab()
{
    m_comparatorTab = new ComparatorTab(this);
    m_tabWidget->addTab(m_comparatorTab, "Comparator");
}

void MainWindow::createExcelSearchTab()
{
    m_excelSearchTab = new ExcelSearchTab(this);
    m_tabWidget->addTab(m_excelSearchTab, "Excel Search");
}

void MainWindow::createYearRolloverTab()
{
    m_yearRolloverTab = new YearRolloverTab(this);
    m_tabWidget->addTab(m_yearRolloverTab, "Prep New Year");
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
    qInfo() << "[FILL ALL] Starting execution for year" << result.year
            << "target month" << result.targetMonth;

    if (!m_excelHandler || !m_mappingsManager || !m_transferService) {
        showToast("Internal error: services not initialized.", ToastWidget::Error, 4000);
        return;
    }

    // Guard against double-click / concurrent execution
    if (m_fillAllRunning) {
        showToast("Fill All is already running — please wait.", ToastWidget::Warning, 3000);
        return;
    }
    m_fillAllRunning = true;
    if (m_fillAllMonthsTab) m_fillAllMonthsTab->setExecuteEnabled(false);
    if (m_busyTimeout) m_busyTimeout->start(10 * 60 * 1000); // 10-minute safety net

    // Fill All must not depend on whichever periods were previously loaded in the UI.
    // Reload mapping JSONs explicitly for the requested year.
    const QString jsonBase = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../JSON");
    const QString mappingsOldPath = QString("%1/mappings_old.json").arg(jsonBase);
    const QString mappingsNewPath = QString("%1/mappings.json").arg(jsonBase);
    const QString sapYtdPath = QString("%1/mappings_sap_ytd.json").arg(jsonBase);
    const QString paxPath = QString("%1/pax.json").arg(jsonBase);
    const QString staffPath = QString("%1/staff.json").arg(jsonBase);
    const QString budgetRefiPath = QString("%1/mappings_budget_refi_prev_year.json").arg(jsonBase);
    const QString paxTransferPath = QString("%1/mappings_pax_transfer.json").arg(jsonBase);
    const QString trafficMottPath = QString("%1/Traffic_mott.json").arg(jsonBase);

    bool loadedCore = (result.year <= 2025)
                          ? m_mappingsManager->loadMappings(mappingsOldPath)
                          : m_mappingsManager->loadMappings(mappingsNewPath);
    bool loadedAll = loadedCore
                  && m_mappingsManager->loadSapYtdMappings(sapYtdPath)
                  && m_mappingsManager->loadPaxMappings(paxPath)
                  && m_mappingsManager->loadStaffMappings(staffPath)
                  && m_mappingsManager->loadBudgetRefiMappings(budgetRefiPath)
                  && m_mappingsManager->loadPaxTransferMappings(paxTransferPath)
                  && m_mappingsManager->loadTrafficMottMappings(trafficMottPath);

    if (!loadedAll) {
        showToast("Fill All cancelled: failed to load mapping JSON files.", ToastWidget::Error, 6000);
        m_fillAllRunning = false;
        if (m_fillAllMonthsTab) m_fillAllMonthsTab->setExecuteEnabled(true);
        return;
    }

    FillAllScanResult scan = result;
    QMap<QString, QString> resolvedPaths;

    QString resolvedDest;
    if (!resolveWorkbookPath(this, "Fill All destination workbook",
                             scan.destFilePath, resolvedPaths, &resolvedDest)) {
        showToast("Fill All cancelled by user.", ToastWidget::Warning, 4000);
        m_fillAllRunning = false;
        if (m_fillAllMonthsTab) m_fillAllMonthsTab->setExecuteEnabled(true);
        return;
    }
    scan.destFilePath = resolvedDest;

    for (FillAllFileEntry& entry : scan.entries) {
        QString resolvedSource;
        const QString usageLabel = QString("Fill All source workbook: %1 (%2)")
                                       .arg(entry.month, entry.transferType);
        if (!resolveWorkbookPath(this, usageLabel,
                                 entry.sourceFilePath, resolvedPaths, &resolvedSource)) {
            showToast("Fill All cancelled by user.", ToastWidget::Warning, 4000);
            m_fillAllRunning = false;
            if (m_fillAllMonthsTab) m_fillAllMonthsTab->setExecuteEnabled(true);
            return;
        }
        entry.sourceFilePath = resolvedSource;
        entry.found = QFile::exists(entry.sourceFilePath);
    }

    if (scan.destFilePath.isEmpty() || !QFile::exists(scan.destFilePath)) {
        showToast("Fill All cancelled: destination file is missing.", ToastWidget::Warning, 5000);
        m_fillAllRunning = false;
        if (m_fillAllMonthsTab) m_fillAllMonthsTab->setExecuteEnabled(true);
        return;
    }

    // Check if destination file is locked (open in Excel)
    {
        QFile destFile(scan.destFilePath);
        if (!destFile.open(QIODevice::ReadWrite)) {
            const QString fileName = QFileInfo(scan.destFilePath).fileName();
            showToast(
                QString("File is open in Excel — please close it first:\n%1").arg(fileName),
                ToastWidget::Warning, 8000);
            m_fillAllRunning = false;
            if (m_fillAllMonthsTab) m_fillAllMonthsTab->setExecuteEnabled(true);
            return;
        }
        destFile.close();
    }

    if (m_fillAllDialog) {
        m_fillAllDialog->reset();
        m_fillAllDialog->hide();
        m_fillAllDialog->deleteLater();
        m_fillAllDialog = nullptr;
    }
    m_fillAllDialog = new QProgressDialog("Preparing Fill All...", QString(), 0, 100, this);
    m_fillAllDialog->setWindowTitle("Fill All Months");
    m_fillAllDialog->setWindowModality(Qt::NonModal);
    m_fillAllDialog->setWindowFlags(m_fillAllDialog->windowFlags() | Qt::Tool);
    m_fillAllDialog->setMinimumDuration(0);
    m_fillAllDialog->setAutoClose(false);
    m_fillAllDialog->setAutoReset(false);
    m_fillAllDialog->setCancelButton(nullptr);
    m_fillAllDialog->setValue(0);

    // Create worker + thread
    auto* worker = new FillAllWorker(m_excelHandler, m_mappingsManager,
                                     m_transferService, scan);
    auto* thread = new QThread(this);
    worker->moveToThread(thread);

    // Progress → status bar
    connect(worker, &FillAllWorker::progress,
            this, [this](int cur, int tot, const QString& msg) {
                const int percent = (tot > 0) ? qMin(100, (cur * 100) / tot) : 0;
                if (m_fillAllDialog) {
                    m_fillAllDialog->setValue(percent);
                    m_fillAllDialog->setLabelText(msg);
                }
                updateStatusBar(QString("[%1/%2] %3").arg(cur).arg(tot).arg(msg));
            });

    // Finished
    connect(worker, &FillAllWorker::finished,
            this, [this, thread](const FillAllResult& res) {
                thread->quit();
                m_fillAllRunning = false;
                if (m_busyTimeout) m_busyTimeout->stop();
                if (m_fillAllMonthsTab) m_fillAllMonthsTab->setExecuteEnabled(true);
                if (m_fillAllDialog) {
                    m_fillAllDialog->setValue(100);
                    m_fillAllDialog->setLabelText("Fill All finished");
                }
                if (res.errors.isEmpty() && res.warnings.isEmpty()) {
                    showToast(QString("Fill All complete — %1 cells transferred.")
                                  .arg(res.cellsTransferred),
                              ToastWidget::Success, 5000);
                    updateStatusBar(QString("Fill All done: %1 cells")
                                        .arg(res.cellsTransferred));
                } else {
                    QString summary;
                    if (!res.errors.isEmpty()) {
                        const QString firstError = res.errors.first();
                        summary = QString("Fill All finished with %1 error(s): %2")
                                      .arg(res.errors.size())
                                      .arg(firstError);
                    } else {
                        const QString firstWarning = res.warnings.first();
                        summary = QString("Fill All finished with %1 warning(s): %2")
                                      .arg(res.warnings.size())
                                      .arg(firstWarning);
                    }

                    showToast(summary, ToastWidget::Warning, 8000);
                    updateStatusBar(summary);

                    for (const QString& err : res.errors)
                        qWarning() << "[FILL ALL] Error:" << err;
                    for (const QString& warn : res.warnings)
                        qWarning() << "[FILL ALL] Warning:" << warn;
                }

                QTimer::singleShot(700, this, [this]() {
                    if (!m_fillAllDialog) return;
                    m_fillAllDialog->reset();
                    m_fillAllDialog->hide();
                    m_fillAllDialog->deleteLater();
                    m_fillAllDialog = nullptr;
                });
            });

    connect(thread, &QThread::started,  worker, &FillAllWorker::run);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    showToast(QString("Fill All starting → %1 %2...")
                  .arg(scan.targetMonth)
                  .arg(scan.year),
              ToastWidget::Info, 3000);

    thread->start();
}

QVector<MappingItem> MainWindow::getLoadedMappings() const
{
    if (m_mappingModel) {
        return m_mappingModel->items();
    }
    return QVector<MappingItem>();
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
    // Search multiple possible directories and file patterns
    // SAP YTD files may live in "SAP YTD" or "SAP export monthly" subfolders
    // and may be .xlsm or .xlsx with various naming conventions.
    QStringList searchDirs = {
        QString("%1/SAP YTD").arg(monthFolder),
        QString("%1/SAP export monthly").arg(monthFolder),
        monthFolder   // sometimes directly in the month folder
    };

    for (const QString& dirPath : searchDirs) {
        QDir dir(dirPath);
        if (!dir.exists()) continue;

        // Try exact name first: YTD_MM_YYYY.xlsm / .xlsx
        for (const QString& ext : {".xlsm", ".xlsx", ".xls"}) {
            QString exact = QString("YTD_%1_%2%3").arg(mm).arg(year).arg(ext);
            if (dir.exists(exact))
                return dir.absoluteFilePath(exact);
        }

        // Try wildcard patterns: YTD_*, *YTD*
        QStringList files = dir.entryList(
            QStringList() << QString("YTD_%1_%2*").arg(mm).arg(year)
                          << QString("YTD_%1*").arg(mm)
                          << "YTD_*"
                          << "*YTD*",
            QDir::Files);
        if (!files.isEmpty())
            return dir.absoluteFilePath(files.first());
    }

    // Fallback: return the most likely path (even if it doesn't exist yet)
    // so the scan can show what was expected
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

    // Load the source file if not already loaded
    const QString srcKey = "individual_source";
    if (!m_excelHandler->isLoaded(srcKey)) {
        if (!m_excelHandler->loadWorkbook(config.sourceFile, srcKey)) {
            showToast("Failed to load source file for preview", ToastWidget::Warning);
            return;
        }
    }

    SheetCellSelectorDialog dlg(this);
    dlg.setWindowTitle("Select Source Cell");
    dlg.setSheetNames(m_excelHandler->getSheetNames(srcKey));
    dlg.setExcelHandler(m_excelHandler, srcKey);
    SheetCellSelectorDialog::CellSelection sel;
    sel.sheetName = config.sourceSheet;
    sel.column    = config.sourceColumn;
    sel.row       = config.sourceRow > 0 ? config.sourceRow : 1;
    dlg.setCellSelection(sel);

    if (dlg.exec() == QDialog::Accepted) {
        auto selections = dlg.getCellSelections();
        if (!selections.isEmpty()) {
            std::sort(selections.begin(), selections.end(), [](const auto& a, const auto& b) {
                return a.row < b.row;
            });
            QVector<IndividualTransferPanel::MappingEntry> data = panel->getMappingData();
            if (data.size() < selections.size()) {
                data.resize(selections.size());
            }
            for (int i = 0; i < selections.size(); ++i) {
                data[i].sourceCell = QString("%1%2").arg(selections[i].column).arg(selections[i].row);
                data[i].sheetName = selections[i].sheetName;
            }
            panel->setMappingData(data);
            
            auto first = selections.first();
            config.sourceSheet  = first.sheetName;
            config.sourceColumn = first.column;
            config.sourceRow    = first.row;
            config.cellCount    = selections.size();
            panel->setTransferConfig(config);
        }
    }
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

    // Load the dest file if not already loaded
    const QString destKey = "individual_dest";
    if (!m_excelHandler->isLoaded(destKey)) {
        if (!m_excelHandler->loadWorkbook(config.destFile, destKey)) {
            showToast("Failed to load destination file for preview", ToastWidget::Warning);
            return;
        }
    }

    SheetCellSelectorDialog dlg(this);
    dlg.setWindowTitle("Select Destination Cell");
    dlg.setSheetNames(m_excelHandler->getSheetNames(destKey));
    dlg.setExcelHandler(m_excelHandler, destKey);
    SheetCellSelectorDialog::CellSelection sel;
    sel.sheetName = config.destSheet;
    sel.column    = config.destColumn;
    sel.row       = config.destRow > 0 ? config.destRow : 1;
    dlg.setCellSelection(sel);

    if (dlg.exec() == QDialog::Accepted) {
        auto selections = dlg.getCellSelections();
        if (!selections.isEmpty()) {
            std::sort(selections.begin(), selections.end(), [](const auto& a, const auto& b) {
                return a.row < b.row;
            });
            QVector<IndividualTransferPanel::MappingEntry> data = panel->getMappingData();
            if (data.size() < selections.size()) {
                data.resize(selections.size());
            }
            for (int i = 0; i < selections.size(); ++i) {
                data[i].destCell = QString("%1%2").arg(selections[i].column).arg(selections[i].row);
            }
            panel->setMappingData(data);
            
            auto first = selections.first();
            config.destSheet  = first.sheetName;
            config.destColumn = first.column;
            config.destRow    = first.row;
            panel->setTransferConfig(config);
        }
    }
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
        
        auto panel = m_individualTransferTab ? m_individualTransferTab->panel() : nullptr;
        QVector<IndividualTransferPanel::MappingEntry> mappings;
        if (panel) mappings = panel->getMappingData();
        
        if (!mappings.isEmpty()) {
            // New logic: Transfer using the graphical multi-cell mapping table
            for (const auto& entry : mappings) {
                if (entry.sourceCell.isEmpty() || entry.destCell.isEmpty()) continue;
                
                QRegularExpression re("^([A-Za-z]+)(\\d+)$");
                QRegularExpressionMatch srcMatch = re.match(entry.sourceCell);
                QRegularExpressionMatch destMatch = re.match(entry.destCell);
                if (!srcMatch.hasMatch() || !destMatch.hasMatch()) continue;
                
                int srcCol = m_excelHandler->letterToColumn(srcMatch.captured(1).toUpper());
                int srcRow = srcMatch.captured(2).toInt();
                int destCol = m_excelHandler->letterToColumn(destMatch.captured(1).toUpper());
                int destRow = destMatch.captured(2).toInt();
                
                QString srcSheet = entry.sheetName;
                if (srcSheet.isEmpty()) srcSheet = config.sourceSheet;
                
                QVariant value = m_excelHandler->getCellValue(srcKey, srcSheet, srcRow, srcCol);
                
                if (value.isValid()) {
                    const bool isHeadcountDestRow = (destRow == 12 || destRow == 13 || destRow == 16);
                    if (config.divideBy1000 && !isHeadcountDestRow && value.canConvert<double>()) {
                        double numValue = value.toDouble() / 1000.0;
                        value = numValue;
                    }
                    
                    bool success = m_excelHandler->setCellValue(
                        destKey, config.destSheet, destRow, destCol, value
                    );
                    
                    if (success) {
                        cellsTransferred++;
                    }
                }
            }
        } else {
            // Fallback to legacy loop mechanism if no mapping items are strictly laid out
            int srcCol = m_excelHandler->letterToColumn(config.sourceColumn);
            int destCol = m_excelHandler->letterToColumn(config.destColumn);
            
            for (int i = 0; i < config.cellCount; i++) {
                QVariant value = m_excelHandler->getCellValue(
                    srcKey, config.sourceSheet, config.sourceRow + i, srcCol
                );
                
                if (value.isValid()) {
                    const int actualDestRow = config.destRow + i;
                    const bool isHeadcountDestRow = (actualDestRow == 12 || actualDestRow == 13 || actualDestRow == 16);
                    if (config.divideBy1000 && !isHeadcountDestRow && value.canConvert<double>()) {
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



