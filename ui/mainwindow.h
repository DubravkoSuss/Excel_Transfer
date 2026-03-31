#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QGroupBox>
#include <QScrollArea>
#include <QFrame>
#include <QSpinBox>
#include <QLineEdit>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QComboBox>
#include <QHeaderView>
#include <QTabWidget>
#include <QProgressBar>
#include <QProgressDialog>
#include <QDockWidget>
#include <QTextEdit>
#include <QScrollBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QToolButton>
#include <QListWidget>
#include <QListWidgetItem>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QDateTime>
#include <QThread>
#include <QMutex>
#include <QQueue>
#include <QPair>
#include <QMap>
#include <QSet>
#include <QToolTip>
#include <QIcon>
#include <QPixmap>
#include <QFont>
#include <QColor>
#include <QPalette>
#include <QStatusBar>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QPlainTextEdit>
#include <QInputDialog>

#include "../services/excelhandler.h"
#include "../services/breaklinks.h"
#include "../core/mappingsmanager.h"
#include "../services/transferservice.h"
#include "../features/transfer/rollingtransferservice.h"
#include "../services/loadservice.h"
#include "../services/mappingservice.h"
#include "toastwidget.h"
#include "../core/mappingcontroller.h"
#include "../core/mappingmodel.h"
#include "../core/periodcontroller.h"
#include "../core/periodmodel.h"
#include "../features/transfer/individualtransferpanel.h"
#include "../features/periods/yearcard.h"

class MappingRow;
class PeriodRow;
class PeriodController;
class PeriodModel;
class MappingModel;
class IndividualTransferPanel;
class IndividualTransferTab;
class FillAllMonthsTab;
struct FillAllScanResult;
class TransferSummaryDialog;
class TransferWorker;
class LoadWorker;
class RollingTransferService;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

public slots:
    // Individual Transfer tab slots (public so tab can connect)
    void onIndividualBrowseSource();
    void onIndividualBrowseDest();
    void onIndividualSelectSourceCell();
    void onIndividualSelectDestCell();
    void onIndividualTransfer(const IndividualTransferPanel::TransferConfig& config);
    
    // Fill All tab slots (public so tab can connect)
    void onFillAllExecute(const FillAllScanResult& result);

private slots:
    // onGeneratePeriodRows() removed — period rows now auto-generate on year checkbox toggle
    void onClearPeriodRows();
    void onResetAll();          // Shows confirmation popup then calls doReset()

private:
    void doReset();             // Actual safe reset: disconnect → delete → reconnect
    void safeDisconnectAll();   // Disconnects MappingController, PeriodController, YearCards from MainWindow
    void reconnectSignals();    // Re-establishes those connections after reset
    void safeClearLayout(QLayout* layout); // Disconnect+delete all widgets in a layout
    void onLoadSelectedPeriods();
    void onExecuteAll();
    void onRollingTransfer();
    void onLoadRT();
    void onStopTransfer();
    void onPauseTransfer();
    void onExportSelectedMonths();
    void onYearCheckboxChanged(Qt::CheckState state);
    void onMonthCheckboxChanged(int row, int col, int year, const QString& month);
    void onMappingRunClicked(int index);
    void onMappingRemoveClicked(int index);
    void onMappingEditRowsClicked(int index);
    void onExportRowMapClicked(int index);
    void onImportRowMapClicked(int index);
    
    void onTransferProgress(int current, int total, const QString& message);
    void onTransferRowDone(int index, bool success, const QString& message);
    void onTransferFinished(int totalCells, int executed, const QStringList& skipped);
    void onTransferError(const QString& error);
    
    void onLoadProgress(int current, int total, const QString& message);
    void onLoadComplete(bool success, const QString& message);
    void onLoadError(const QString& error);
    
    void onBreakLinksComplete(const BreakLinks::Result& result);
    void showStatusSidebar();
    void updateExecuteAllButton();
    void appendStatusLine(const QString& line);
    static void handleQtMessage(QtMsgType type, const QMessageLogContext& context, const QString& msg);

public:
    void registerYearMonthCheckbox(const QPair<int, QString>& key, QCheckBox* cb);
    
    // Public accessors for tab classes
    QString destFolder() const { return m_destFolder; }
    QString findCostControlFile(const QString& monthFolder);
    QString findSapFile(const QString& monthFolder, const QString& mm, int year);
    QString findSapYtdFile(const QString& monthFolder, const QString& mm, int year);
    QString findTrafficFile(const QString& basePath, const QString& mm, int year);
    QString findStaffFile(int year);

private:
    void setupUI();
    void createHeader();
    void createMonthYearSelector();
    void createFileInfoCard();
    void createMappingsCard();
    void createIndividualTab();
    void createFillAllTab();
    
    void updateStatusBar(const QString& message);
    void showToast(const QString& message, ToastWidget::ToastType type = ToastWidget::Info, int duration = 3000);
    
    QVector<QPair<QString, int>> getSelectedPeriods() const;
    void generatePeriodRowsForYears();
    void loadMappingsForPeriods();
    
    bool verifyPassword(const QString& action);
    bool executeTransfer(const MappingRow& mapping);
    int transferData(const MappingEntry& mapping, int year);
    void blockAllYearCardSignals(bool block);
    
    QString getSourceFolder() const { return m_sourceFolder; }
    QString getDestFolder() const { return m_destFolder; }
    
    QString findMonthNum(const QString& monthName) const;
    QString findCostControlPath(const QString& month, int year) const;
    QString findSAPPath(const QString& month, int year) const;
    
    void showFillAllResults(const struct FillAllResult& result);

    int handleSapYtdTransfer(const MappingEntry& entry, int year, const QString& destKey, const QString& destFilePath);
    int handleYtdTransfer(const MappingEntry& entry, int year, const QString& destKey, const QString& destFilePath);
    
    struct PeriodKey {
        QString month;
        int year;
        QString type;
        bool operator<(const PeriodKey& other) const {
            if (year != other.year) return year < other.year;
            if (month != other.month) return month < other.month;
            return type < other.type;
        }
    };
    
    struct LoadedWorkbook {
        QString filePath;
        QString type;
        bool isLoaded;
    };

    QWidget* m_centralWidget;
    QVBoxLayout* m_mainLayout;
    
    QFrame* m_headerFrame;
    QLabel* m_titleLabel;
    QLabel* m_subtitleLabel;
    
    QWidget* m_monthYearSelector;
    QVBoxLayout* m_monthYearLayout;
    QMap<int, QCheckBox*> m_yearCheckboxes;  // nullptrs when using YearCard
    QMap<int, YearCard*>  m_yearCards;
    QMap<QPair<int, QString>, QCheckBox*> m_yearMonthCheckboxes;
    PeriodController* m_periodController = nullptr;
    PeriodModel* m_periodModel = nullptr;

    void registerYearMonthCheckboxInternal(const QPair<int, QString>& key, QCheckBox* cb);
    void displayPeriodRows(const QList<YearEntry>& periods);
    void clearPeriodRows();
    
    QFrame* m_fileInfoCard;
    
    QFrame* m_mappingsCard;
    QVBoxLayout* m_mappingsLayout;
    QWidget* m_mappingsContainer;
    QVBoxLayout* m_mappingsContainerLayout;
    QLabel* m_noMappingsLabel;

    QWidget* m_costControlContainer = nullptr;
    QVBoxLayout* m_costControlLayout = nullptr;
    
    QTabWidget* m_tabWidget = nullptr;
    IndividualTransferTab* m_individualTransferTab = nullptr;
    FillAllMonthsTab* m_fillAllMonthsTab = nullptr;
    
    // m_btnGenerate removed — generate is now automatic on year checkbox toggle
    QPushButton* m_btnClear;
    QPushButton* m_btnLoad;
    QPushButton* m_btnExecuteAll;
    QPushButton* m_btnPause;
    QPushButton* m_btnStop;
    QPushButton* m_btnRollingTransfer;
    QPushButton* m_btnLoadRT = nullptr;
    QPushButton* m_btnExportSelectedMonths;
    QPushButton* m_btnSelectAll = nullptr;
    bool m_allSelected = false;
    
    QProgressBar* m_progressBar;
    QProgressDialog* m_transferDialog = nullptr;
    QProgressDialog* m_rtDialog = nullptr;
    QDockWidget* m_statusDock = nullptr;
    QTextEdit* m_statusText = nullptr;
    QLabel* m_statusLabel;
    QStatusBar* m_statusBar;
    
    QString m_sourceFolder;
    QString m_destFolder;
    
    QMap<QString, LoadedWorkbook> m_loadedWorkbooks;
    QMap<QString, QVector<MappingEntry>> m_dynamicMappingsCache;
    MappingController* m_mappingController = nullptr;
    MappingModel* m_mappingModel = nullptr;
    QString m_paxFilePath;
    QString m_staffFilePath;
    QString m_budgetRefiFilePath;
    QMap<QString, QString> m_loadedFiles;
    
    MappingsManager* m_mappingsManager;
    ExcelHandler* m_excelHandler;
    BreakLinks* m_breakLinks;
    TransferService* m_transferService;
    RollingTransferService* m_rollingService = nullptr;
    QVector<RollingStep> m_rollingChain;
    LoadService* m_loadService;
    MappingService* m_mappingService;
    
    QThread* m_transferThread = nullptr;
    QThread* m_loadThread = nullptr;
    TransferWorker* m_transferWorker = nullptr;
    LoadWorker* m_loadWorker = nullptr;
    
    bool m_isTransferRunning = false;
    bool m_isTransferPaused = false;
    bool m_isTransferStopRequested = false;
    QMutex m_transferMutex;
    bool m_isLoadingPeriods = false;
    bool m_isLoadingRT = false;
    
    int m_transferTotalMappings;
    int m_transferSuccessfulMappings;
    QStringList m_transferFailedMappings;
    
    int m_currentEditMappingIndex;
    
    ToastWidget* m_toastWidget;
    
    static const QString PASSWORD;
    static const QString DEFAULT_SOURCE_FOLDER;
    static const QString DEFAULT_DEST_FOLDER;
    static const QStringList MONTHS_LIST;
    static const QMap<QString, QString> MONTH_TO_NUM;
    static const QMap<int, QList<int>> QUARTERS;
    static MainWindow* s_instance;
    static const QVector<int> YEAR_RANGE;
};

    
#endif // MAINWINDOW_H