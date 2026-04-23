#ifndef YEARROLLOVERTAB_H
#define YEARROLLOVERTAB_H

#include <QWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QGroupBox>
#include <QFrame>
#include <QComboBox>
#include <QListWidget>

class MainWindow;
class RolloverWorker;

class YearRolloverTab : public QWidget
{
    Q_OBJECT

public:
    explicit YearRolloverTab(MainWindow* mainWindow, QWidget* parent = nullptr);

private slots:
    void onBrowseSource();
    void onAutoDetectSource();
    void onBrowseDest();
    void onBrowseSubtotals();
    void onDryRun();
    void onExecute();
    void onCancel();
    void onOpenResult();
    void onReset();
    void onCreateFolders();
    void onCopyToDestination();
    void onPreviewSheets();
    void onWorkerProgress(int current, int total, const QString& message);
    void onWorkerLogLine(const QString& line);
    void onWorkerFinished(bool success, const QString& message,
                          int cellsCleared, int cellsPreserved, int subtotalsApplied,
                          int sheetsRenamed = 0);
    void onSourceChanged();

private:
    void setupUI();
    void setRunning(bool running);
    void appendLog(const QString& line);
    QString subtotalsConfigPath() const;
    void updateAutoDestPath();

    MainWindow*     m_mainWindow = nullptr;
    RolloverWorker* m_worker     = nullptr;

    // ── Configuration ─────────────────────────────────────────────────────
    QLineEdit*    m_sourceFileEdit      = nullptr;
    QPushButton*  m_sourceBtn           = nullptr;
    QPushButton*  m_autoDetectBtn       = nullptr;

    QLineEdit*    m_destFileEdit        = nullptr;
    QPushButton*  m_destBtn             = nullptr;

    QSpinBox*     m_yearPicker          = nullptr;

    QLineEdit*    m_sheetNameEdit       = nullptr;

    QSpinBox*     m_startRowPicker      = nullptr;
    QSpinBox*     m_endRowPicker        = nullptr;

    QLineEdit*    m_subtotalsPathEdit   = nullptr;
    QPushButton*  m_subtotalsBtn        = nullptr;
    QCheckBox*    m_applySubtotalsCheck = nullptr;

    QCheckBox*    m_dryRunCheck         = nullptr;

    // ── Column group checkboxes ─────────────────────────────────────────
    QCheckBox*    m_clearBaseCheck      = nullptr;   // G, W, AM, BD, ...
    QCheckBox*    m_clearCumulCheck     = nullptr;   // IP, IQ, IR, ...
    QCheckBox*    m_clearJGCheck        = nullptr;   // JG (YTD column)
    QCheckBox*    m_clearBudgetCheck    = nullptr;   // Budget/REFI columns

    // ── Sheet renaming ──────────────────────────────────────────────────
    QCheckBox*    m_renameSheetCheck    = nullptr;
    QCheckBox*    m_processAllSheetsCheck = nullptr;

    // ── Execution ─────────────────────────────────────────────────────────
    QPushButton*  m_dryRunBtn    = nullptr;
    QPushButton*  m_executeBtn   = nullptr;
    QPushButton*  m_cancelBtn    = nullptr;
    QPushButton*  m_openResBtn   = nullptr;
    QPushButton*  m_resetBtn     = nullptr;
    QPushButton*  m_createFoldersBtn = nullptr;
    QPushButton*  m_copyToDestBtn    = nullptr;
    QPushButton*  m_previewSheetsBtn = nullptr;

    QProgressBar* m_progressBar  = nullptr;
    QLabel*       m_statusLabel  = nullptr;

    // ── Log area ──────────────────────────────────────────────────────────
    QPlainTextEdit* m_logEdit    = nullptr;

    // ── Sheet preview list ───────────────────────────────────────────────
    QListWidget*  m_sheetList    = nullptr;

    // ── Summary labels ────────────────────────────────────────────────────
    QLabel*       m_lblCleared   = nullptr;
    QLabel*       m_lblPreserved = nullptr;
    QLabel*       m_lblSubtotals = nullptr;
    QLabel*       m_lblRenamed   = nullptr;
    QFrame*       m_summaryFrame = nullptr;

    // ── State ─────────────────────────────────────────────────────────────
    QString       m_lastResultFile;
    int           m_lastSheetsRenamed = 0;
};

#endif // YEARROLLOVERTAB_H