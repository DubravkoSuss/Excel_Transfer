#include "yearrollovertab.h"
#include "mainwindow.h"
#include "../features/rollover/rolloverworker.h"
#include "../services/excelhandler.h"

#include <QFileDialog>
#include <QGroupBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QDateTime>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QScrollBar>
#include <QSplitter>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>

// ═══════════════════════════════════════════════════════════════════════════
// Helpers – shared stylesheet snippets
// ═══════════════════════════════════════════════════════════════════════════

static QString btnPrimary() {
    return "QPushButton {"
           "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
           "    stop:0 #3B82F6, stop:1 #2563EB);"
           "  color: white; font-weight: 600; font-size: 13px;"
           "  padding: 9px 22px; border-radius: 7px; border: none; }"
           "QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
           "    stop:0 #2563EB, stop:1 #1D4ED8); }"
           "QPushButton:pressed { background: #1E40AF; }"
           "QPushButton:disabled { background: #E5E7EB; color: #9CA3AF; }";
}

static QString btnSuccess() {
    return "QPushButton {"
           "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
           "    stop:0 #059669, stop:1 #047857);"
           "  color: white; font-weight: 700; font-size: 14px;"
           "  padding: 10px 28px; border-radius: 8px; border: none; }"
           "QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
           "    stop:0 #047857, stop:1 #065F46); }"
           "QPushButton:pressed { background: #064E3B; }"
           "QPushButton:disabled { background: #E5E7EB; color: #9CA3AF; }";
}

static QString btnDanger() {
    return "QPushButton {"
           "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
           "    stop:0 #EF4444, stop:1 #DC2626);"
           "  color: white; font-weight: 600; font-size: 13px;"
           "  padding: 9px 22px; border-radius: 7px; border: none; }"
           "QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
           "    stop:0 #DC2626, stop:1 #B91C1C); }"
           "QPushButton:pressed { background: #991B1B; }"
           "QPushButton:disabled { background: #E5E7EB; color: #9CA3AF; }";
}

static QString btnNeutral() {
    return "QPushButton {"
           "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
           "    stop:0 #6B7280, stop:1 #4B5563);"
           "  color: white; font-weight: 600; font-size: 13px;"
           "  padding: 9px 22px; border-radius: 7px; border: none; }"
           "QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
           "    stop:0 #4B5563, stop:1 #374151); }"
           "QPushButton:pressed { background: #1F2937; }"
           "QPushButton:disabled { background: #E5E7EB; color: #9CA3AF; }";
}

static QString btnWarning() {
    return "QPushButton {"
           "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
           "    stop:0 #F59E0B, stop:1 #D97706);"
           "  color: white; font-weight: 600; font-size: 13px;"
           "  padding: 9px 22px; border-radius: 7px; border: none; }"
           "QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
           "    stop:0 #D97706, stop:1 #B45309); }"
           "QPushButton:pressed { background: #92400E; }"
           "QPushButton:disabled { background: #E5E7EB; color: #9CA3AF; }";
}

static QString btnTeal() {
    return "QPushButton {"
           "  background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
           "    stop:0 #14B8A6, stop:1 #0D9488);"
           "  color: white; font-weight: 600; font-size: 13px;"
           "  padding: 9px 22px; border-radius: 7px; border: none; }"
           "QPushButton:hover { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
           "    stop:0 #0D9488, stop:1 #0F766E); }"
           "QPushButton:pressed { background: #115E59; }"
           "QPushButton:disabled { background: #E5E7EB; color: #9CA3AF; }";
}

static QString browseBtn() {
    return "QPushButton { background: white; border: 1px solid #D1D5DB; color: #374151;"
           "  font-size: 12px; padding: 5px 12px; border-radius: 5px; font-weight: 500; }"
           "QPushButton:hover { background: #F3F4F6; border-color: #3B82F6; }";
}

static QString lineEditStyle() {
    return "QLineEdit { background: white; border: 2px solid #D1D5DB; border-radius: 8px;"
           "  padding: 10px 14px; font-size: 14px; color: #111827; min-height: 20px; }"
           "QLineEdit:focus { border-color: #3B82F6; }"
           "QLineEdit:read-only { background: #F9FAFB; color: #6B7280; }";
}

static QString spinStyle() {
    return "QSpinBox { background: white; border: 2px solid #D1D5DB; border-radius: 8px;"
           "  padding: 10px 12px; font-size: 14px; color: #111827; min-width: 100px; min-height: 20px; }"
           "QSpinBox:focus { border-color: #3B82F6; }"
           "QSpinBox::up-button, QSpinBox::down-button { border: none; width: 20px; }";
}

static QString cardStyle() {
    return "QFrame { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
           "  stop:0 #FAFBFC, stop:1 #F5F7FA);"
           "  border-radius: 10px; border: 1px solid #E5E7EB; }";
}

static QString labelStyle() {
    return "font-weight: 600; color: #374151; font-size: 12px;";
}

static QString checkboxStyle() {
    return "QCheckBox { font-size: 12px; color: #374151; font-weight: 500; spacing: 6px; }"
           "QCheckBox::indicator { width: 16px; height: 16px; }";
}

// ═══════════════════════════════════════════════════════════════════════════
// Constructor
// ═══════════════════════════════════════════════════════════════════════════

YearRolloverTab::YearRolloverTab(MainWindow* mainWindow, QWidget* parent)
    : QWidget(parent), m_mainWindow(mainWindow)
{
    setupUI();
}

// ═══════════════════════════════════════════════════════════════════════════
// setupUI
// ═══════════════════════════════════════════════════════════════════════════

void YearRolloverTab::setupUI()
{
    QVBoxLayout* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // ── Top: title bar ────────────────────────────────────────────────────
    QFrame* titleBar = new QFrame(this);
    titleBar->setStyleSheet(
        "QFrame { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "  stop:0 #1E3A5F, stop:1 #2563EB);"
        "  border-radius: 0; padding: 0; margin: 0; }");
    titleBar->setFixedHeight(60);
    QHBoxLayout* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(20, 0, 20, 0);

    QLabel* titleIcon = new QLabel("\xF0\x9F\x93\x85", titleBar);
    titleIcon->setStyleSheet("font-size: 22px; color: white;");
    QLabel* titleLabel = new QLabel("Prepare New Year (Rollover)", titleBar);
    titleLabel->setStyleSheet(
        "font-weight: 700; font-size: 16px; color: white; letter-spacing: -0.3px;");
    QLabel* descLabel = new QLabel(
        "Clears data columns, preserves structural formulas, renames sheet tabs, creates folder structure.", titleBar);
    descLabel->setStyleSheet("color: rgba(255,255,255,0.75); font-size: 11px;");

    titleLayout->addWidget(titleIcon);
    titleLayout->addSpacing(8);
    QVBoxLayout* titleTextLayout = new QVBoxLayout();
    titleTextLayout->setSpacing(1);
    titleTextLayout->addWidget(titleLabel);
    titleTextLayout->addWidget(descLabel);
    titleLayout->addLayout(titleTextLayout);
    titleLayout->addStretch();
    rootLayout->addWidget(titleBar);

    // ── Main splitter: left config | right log+sheets ─────────────────────
    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(5);
    splitter->setStyleSheet(
        "QSplitter::handle { background: #E5E7EB; }"
        "QSplitter::handle:hover { background: #3B82F6; }");
    rootLayout->addWidget(splitter, 1);

    // ═══════════════════════════════════════════
    // LEFT panel — Configuration (scrollable)
    // ═══════════════════════════════════════════
    QScrollArea* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet("QScrollArea { background: transparent; border: none; }");

    QWidget* leftPanel = new QWidget();
    leftPanel->setMinimumWidth(420);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(16, 16, 12, 16);
    leftLayout->setSpacing(12);

    // ── Card: Files ───────────────────────────────────────────────────────
    {
        QFrame* card = new QFrame();
        card->setStyleSheet(cardStyle());
        QVBoxLayout* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(16, 14, 16, 14);
        cardLayout->setSpacing(10);

        QLabel* title = new QLabel("\xF0\x9F\x93\x82  Files", card);
        title->setStyleSheet("font-weight: 700; font-size: 13px; color: #1F2937;");
        cardLayout->addWidget(title);

        // Source
        QLabel* srcLabel = new QLabel("Source File (completed previous year):", card);
        srcLabel->setStyleSheet(labelStyle());
        cardLayout->addWidget(srcLabel);
        QHBoxLayout* sourceRow = new QHBoxLayout();
        m_sourceFileEdit = new QLineEdit(card);
        m_sourceFileEdit->setReadOnly(true);
        m_sourceFileEdit->setPlaceholderText("Select MZLZ Consolidated workbook...");
        m_sourceFileEdit->setStyleSheet(lineEditStyle());
        m_sourceBtn = new QPushButton("Browse...", card);
        m_sourceBtn->setStyleSheet(browseBtn());
        m_sourceBtn->setFixedWidth(80);
        m_autoDetectBtn = new QPushButton("Auto-Detect", card);
        m_autoDetectBtn->setStyleSheet(browseBtn());
        m_autoDetectBtn->setFixedWidth(90);
        m_autoDetectBtn->setToolTip(
            "Automatically find the most recent Cost Control file\n"
            "in the standard folder structure (L:/Cost control/...)");
        sourceRow->addWidget(m_sourceFileEdit);
        sourceRow->addWidget(m_sourceBtn);
        sourceRow->addWidget(m_autoDetectBtn);
        cardLayout->addLayout(sourceRow);

        // Dest
        QLabel* dstLabel = new QLabel("Save New File As:", card);
        dstLabel->setStyleSheet(labelStyle());
        cardLayout->addWidget(dstLabel);
        QHBoxLayout* destRow = new QHBoxLayout();
        m_destFileEdit = new QLineEdit(card);
        m_destFileEdit->setReadOnly(true);
        m_destFileEdit->setPlaceholderText("Choose output path...");
        m_destFileEdit->setStyleSheet(lineEditStyle());
        m_destBtn = new QPushButton("Browse...", card);
        m_destBtn->setStyleSheet(browseBtn());
        m_destBtn->setFixedWidth(80);
        destRow->addWidget(m_destFileEdit);
        destRow->addWidget(m_destBtn);
        cardLayout->addLayout(destRow);

        leftLayout->addWidget(card);
    }

    // ── Card: Parameters ──────────────────────────────────────────────────
    {
        QFrame* card = new QFrame();
        card->setStyleSheet(cardStyle());
        QVBoxLayout* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(16, 14, 16, 14);
        cardLayout->setSpacing(10);

        QLabel* title = new QLabel("\xE2\x9A\x99\xEF\xB8\x8F  Parameters", card);
        title->setStyleSheet("font-weight: 700; font-size: 13px; color: #1F2937;");
        cardLayout->addWidget(title);

        QFormLayout* form = new QFormLayout();
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        form->setSpacing(8);
        form->setContentsMargins(0, 0, 0, 0);

        // Target year
        m_yearPicker = new QSpinBox(card);
        m_yearPicker->setRange(2020, 2100);
        m_yearPicker->setValue(QDateTime::currentDateTime().date().year() + 1);
        m_yearPicker->setStyleSheet(spinStyle());
        QLabel* yrLbl = new QLabel("New Target Year:", card);
        yrLbl->setStyleSheet(labelStyle());
        form->addRow(yrLbl, m_yearPicker);

        // Sheet name
        m_sheetNameEdit = new QLineEdit(card);
        m_sheetNameEdit->setText("MZLZ Consolidated");
        m_sheetNameEdit->setStyleSheet(lineEditStyle());
        m_sheetNameEdit->setToolTip("The exact name of the sheet to process inside the workbook.");
        QLabel* shLbl = new QLabel("Target Sheet:", card);
        shLbl->setStyleSheet(labelStyle());
        form->addRow(shLbl, m_sheetNameEdit);

        // Row range
        QHBoxLayout* rowRangeLayout = new QHBoxLayout();
        m_startRowPicker = new QSpinBox(card);
        m_startRowPicker->setRange(1, 10000);
        m_startRowPicker->setValue(5);
        m_startRowPicker->setStyleSheet(spinStyle());
        QLabel* toLabel = new QLabel("to", card);
        toLabel->setStyleSheet("color: #6B7280; font-size: 12px;");
        m_endRowPicker = new QSpinBox(card);
        m_endRowPicker->setRange(1, 10000);
        m_endRowPicker->setValue(300);
        m_endRowPicker->setStyleSheet(spinStyle());
        rowRangeLayout->addWidget(m_startRowPicker);
        rowRangeLayout->addWidget(toLabel);
        rowRangeLayout->addWidget(m_endRowPicker);
        rowRangeLayout->addStretch();
        QLabel* rrLbl = new QLabel("Row Range:", card);
        rrLbl->setStyleSheet(labelStyle());
        form->addRow(rrLbl, rowRangeLayout);

        cardLayout->addLayout(form);
        leftLayout->addWidget(card);
    }

    // ── Card: Column Groups ──────────────────────────────────────────────
    {
        QFrame* card = new QFrame();
        card->setStyleSheet(cardStyle());
        QVBoxLayout* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(16, 14, 16, 14);
        cardLayout->setSpacing(8);

        QLabel* title = new QLabel("\xF0\x9F\x93\x8A  Column Groups to Clear", card);
        title->setStyleSheet("font-weight: 700; font-size: 13px; color: #1F2937;");
        cardLayout->addWidget(title);

        QLabel* hint = new QLabel("Select which column groups to wipe for the new year:", card);
        hint->setStyleSheet("color: #6B7280; font-size: 11px;");
        hint->setWordWrap(true);
        cardLayout->addWidget(hint);

        m_clearBaseCheck = new QCheckBox("Base monthly columns (G, W, AM, BD, BW, CP, DJ, EF, FB, FY, GX, HW)", card);
        m_clearBaseCheck->setChecked(true);
        m_clearBaseCheck->setStyleSheet(checkboxStyle());
        m_clearBaseCheck->setToolTip("12 base monthly columns \xe2\x80\x94 each holds one month's transferred data.");
        cardLayout->addWidget(m_clearBaseCheck);

        m_clearCumulCheck = new QCheckBox("Cumulative columns (IP, IQ, IR, IS, IT, IU, IV, IW, IX, IY, IZ, JA)", card);
        m_clearCumulCheck->setChecked(true);
        m_clearCumulCheck->setStyleSheet(checkboxStyle());
        m_clearCumulCheck->setToolTip("12 cumulative (reported) columns \xe2\x80\x94 year-to-date accumulation.");
        cardLayout->addWidget(m_clearCumulCheck);

        m_clearJGCheck = new QCheckBox("JG column (SAP YTD total)", card);
        m_clearJGCheck->setChecked(true);
        m_clearJGCheck->setStyleSheet(checkboxStyle());
        m_clearJGCheck->setToolTip("The JG column holds the full-year SAP YTD values.");
        cardLayout->addWidget(m_clearJGCheck);

        m_clearBudgetCheck = new QCheckBox("Budget / REFI / PrevYear columns (D-F, T-V, AJ-AL, ...)", card);
        m_clearBudgetCheck->setChecked(true);
        m_clearBudgetCheck->setStyleSheet(checkboxStyle());
        m_clearBudgetCheck->setToolTip(
            "36 columns (3 per month) that hold Budget, REFI, and Previous Year data.\n"
            "D/E/F, T/U/V, AJ/AK/AL, BA/BB/BC, BT/BU/BV, CM/CN/CO,\n"
            "DG/DH/DI, EC/ED/EE, EY/EZ/FA, FV/FW/FX, GU/GV/GW, HT/HU/HV.\n\n"
            "Typically NOT cleared during rollover \xe2\x80\x94 only check if starting fresh.");
        cardLayout->addWidget(m_clearBudgetCheck);

        leftLayout->addWidget(card);
    }

    // ── Card: Sheet Operations ──────────────────────────────────────────
    {
        QFrame* card = new QFrame();
        card->setStyleSheet(cardStyle());
        QVBoxLayout* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(16, 14, 16, 14);
        cardLayout->setSpacing(8);

        QLabel* title = new QLabel("\xF0\x9F\x93\x8B  Sheet Operations", card);
        title->setStyleSheet("font-weight: 700; font-size: 13px; color: #1F2937;");
        cardLayout->addWidget(title);

        m_renameSheetCheck = new QCheckBox("Rename sheet tabs with year references", card);
        m_renameSheetCheck->setChecked(true);
        m_renameSheetCheck->setStyleSheet(checkboxStyle());
        m_renameSheetCheck->setToolTip(
            "Renames sheets like \"TRAFFIC mott 2025\" to \"TRAFFIC mott 2026\".\n"
            "Uses target year - 1 as the old year.");
        cardLayout->addWidget(m_renameSheetCheck);

        m_processAllSheetsCheck = new QCheckBox("Process additional sheets (TRAFFIC mott, Staff report, etc.)", card);
        m_processAllSheetsCheck->setChecked(false);
        m_processAllSheetsCheck->setStyleSheet(checkboxStyle());
        m_processAllSheetsCheck->setToolTip(
            "When enabled, also clears data columns in secondary sheets\n"
            "like TRAFFIC mott and Staff report.");
        cardLayout->addWidget(m_processAllSheetsCheck);

        // Preview sheets button
        m_previewSheetsBtn = new QPushButton("\xF0\x9F\x94\x8D Preview Sheets", card);
        m_previewSheetsBtn->setStyleSheet(browseBtn());
        m_previewSheetsBtn->setToolTip("Load the source file and show all sheet names.");
        cardLayout->addWidget(m_previewSheetsBtn);

        leftLayout->addWidget(card);
    }

    // ── Card: Subtotals config ────────────────────────────────────────────
    {
        QFrame* card = new QFrame();
        card->setStyleSheet(cardStyle());
        QVBoxLayout* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(16, 14, 16, 14);
        cardLayout->setSpacing(8);

        QLabel* title = new QLabel("\xF0\x9F\x93\x88  Subtotals Restoration", card);
        title->setStyleSheet("font-weight: 700; font-size: 13px; color: #1F2937;");
        cardLayout->addWidget(title);

        m_applySubtotalsCheck = new QCheckBox("Restore subtotal formulas after clearing", card);
        m_applySubtotalsCheck->setChecked(true);
        m_applySubtotalsCheck->setStyleSheet(checkboxStyle());
        cardLayout->addWidget(m_applySubtotalsCheck);

        QLabel* subPathLbl = new QLabel("Subtotals Config JSON:", card);
        subPathLbl->setStyleSheet(labelStyle());
        cardLayout->addWidget(subPathLbl);

        QHBoxLayout* subPathRow = new QHBoxLayout();
        m_subtotalsPathEdit = new QLineEdit(card);
        m_subtotalsPathEdit->setPlaceholderText("config/mzlz_subtotals.json");
        m_subtotalsPathEdit->setStyleSheet(lineEditStyle());
        // Auto-populate with the bundled config
        QString bundledPath = QCoreApplication::applicationDirPath() + "/config/mzlz_subtotals.json";
        if (QFile::exists(bundledPath))
            m_subtotalsPathEdit->setText(bundledPath);
        else {
            // Try relative path from working directory
            QString relPath = "config/mzlz_subtotals.json";
            if (QFile::exists(relPath))
                m_subtotalsPathEdit->setText(relPath);
        }

        m_subtotalsBtn = new QPushButton("Browse...", card);
        m_subtotalsBtn->setStyleSheet(browseBtn());
        m_subtotalsBtn->setFixedWidth(80);
        subPathRow->addWidget(m_subtotalsPathEdit);
        subPathRow->addWidget(m_subtotalsBtn);
        cardLayout->addLayout(subPathRow);

        connect(m_applySubtotalsCheck, &QCheckBox::toggled, this, [this](bool checked) {
            m_subtotalsPathEdit->setEnabled(checked);
            m_subtotalsBtn->setEnabled(checked);
        });

        leftLayout->addWidget(card);
    }

    // ── Card: Options ────────────────────────────────────────────────────
    {
        QFrame* card = new QFrame();
        card->setStyleSheet(cardStyle());
        QVBoxLayout* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(16, 12, 16, 12);
        cardLayout->setSpacing(6);

        QLabel* title = new QLabel("\xF0\x9F\x94\xA7  Options", card);
        title->setStyleSheet("font-weight: 700; font-size: 13px; color: #1F2937;");
        cardLayout->addWidget(title);

        m_dryRunCheck = new QCheckBox(
            "Dry Run  \xe2\x80\x94  scan only, do not write changes", card);
        m_dryRunCheck->setStyleSheet(checkboxStyle());
        m_dryRunCheck->setToolTip(
            "When checked, the tool will scan the workbook and report what would be cleared,\n"
            "but will NOT save any file. Use this to preview results safely.");
        cardLayout->addWidget(m_dryRunCheck);

        leftLayout->addWidget(card);
    }

    leftLayout->addStretch();

    // ── Action buttons ──────────────────────────────────────────────────
    {
        QFrame* btnFrame = new QFrame();
        btnFrame->setStyleSheet("QFrame { background: transparent; border: none; }");
        QVBoxLayout* btnMainLayout = new QVBoxLayout(btnFrame);
        btnMainLayout->setContentsMargins(0, 0, 0, 0);
        btnMainLayout->setSpacing(8);

        // Row 1: Main action buttons
        QHBoxLayout* btnRow1 = new QHBoxLayout();
        btnRow1->setSpacing(8);

        m_dryRunBtn = new QPushButton("\xF0\x9F\x94\x8D Dry Run", btnFrame);
        m_dryRunBtn->setStyleSheet(btnNeutral());
        m_dryRunBtn->setToolTip("Scan and report without writing any changes.");

        m_executeBtn = new QPushButton("\xE2\x96\xB6 Prepare Workbook", btnFrame);
        m_executeBtn->setStyleSheet(btnSuccess());
        m_executeBtn->setMinimumHeight(42);

        m_cancelBtn = new QPushButton("\xE2\xAC\x9B Cancel", btnFrame);
        m_cancelBtn->setStyleSheet(btnDanger());
        m_cancelBtn->setVisible(false);

        m_resetBtn = new QPushButton("\xF0\x9F\x94\x84 Reset", btnFrame);
        m_resetBtn->setStyleSheet(btnWarning());
        m_resetBtn->setToolTip("Reset all fields to defaults.");

        btnRow1->addWidget(m_dryRunBtn);
        btnRow1->addWidget(m_executeBtn);
        btnRow1->addWidget(m_cancelBtn);
        btnRow1->addWidget(m_resetBtn);
        btnRow1->addStretch();
        btnMainLayout->addLayout(btnRow1);

        // Row 2: Utility buttons
        QHBoxLayout* btnRow2 = new QHBoxLayout();
        btnRow2->setSpacing(8);

        m_createFoldersBtn = new QPushButton("\xF0\x9F\x93\x81 Create Year Folders", btnFrame);
        m_createFoldersBtn->setStyleSheet(btnTeal());
        m_createFoldersBtn->setToolTip(
            "Create the 12-month folder structure for the new year:\n"
            "{base}/{year}/01/ through /12/\n"
            "Each with subfolders: test/, SAP export monthly/, SAP YTD/, Traffic/");

        m_copyToDestBtn = new QPushButton("\xF0\x9F\x93\x8B Copy to Destination", btnFrame);
        m_copyToDestBtn->setStyleSheet(btnPrimary());
        m_copyToDestBtn->setVisible(false);
        m_copyToDestBtn->setToolTip(
            "Copy the prepared workbook to the standard folder structure\n"
            "for each selected month.");

        m_openResBtn = new QPushButton("\xF0\x9F\x93\x82 Open Result", btnFrame);
        m_openResBtn->setStyleSheet(btnPrimary());
        m_openResBtn->setVisible(false);
        m_openResBtn->setToolTip("Open the prepared workbook in the default application.");

        btnRow2->addWidget(m_createFoldersBtn);
        btnRow2->addWidget(m_copyToDestBtn);
        btnRow2->addStretch();
        btnRow2->addWidget(m_openResBtn);
        btnMainLayout->addLayout(btnRow2);

        leftLayout->addWidget(btnFrame);
    }

    // ── Progress bar + status ─────────────────────────────────────────────
    m_progressBar = new QProgressBar(leftPanel);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(false);
    m_progressBar->setStyleSheet(
        "QProgressBar { background: #E5E7EB; border-radius: 5px; height: 10px; text-align: center;"
        "  font-size: 10px; color: #374151; }"
        "QProgressBar::chunk { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "  stop:0 #3B82F6, stop:1 #06B6D4); border-radius: 5px; }");
    leftLayout->addWidget(m_progressBar);

    m_statusLabel = new QLabel("Ready \xe2\x80\x94 select a source file to begin.", leftPanel);
    m_statusLabel->setStyleSheet("color: #6B7280; font-style: italic; font-size: 12px; padding: 2px 0;");
    m_statusLabel->setWordWrap(true);
    leftLayout->addWidget(m_statusLabel);

    // ── Summary row ───────────────────────────────────────────────────────
    m_summaryFrame = new QFrame();
    m_summaryFrame->setObjectName("summaryFrame");
    m_summaryFrame->setStyleSheet(
        "QFrame#summaryFrame { background: #F0FDF4; border: 1px solid #BBF7D0;"
        "  border-radius: 8px; padding: 4px; }");
    m_summaryFrame->setVisible(false);
    QHBoxLayout* summaryLayout = new QHBoxLayout(m_summaryFrame);
    summaryLayout->setContentsMargins(12, 8, 12, 8);
    summaryLayout->setSpacing(16);

    m_lblCleared = new QLabel("Cleared: \xe2\x80\x94", m_summaryFrame);
    m_lblCleared->setStyleSheet("font-weight: 600; color: #DC2626; font-size: 12px;");
    m_lblPreserved = new QLabel("Preserved: \xe2\x80\x94", m_summaryFrame);
    m_lblPreserved->setStyleSheet("font-weight: 600; color: #059669; font-size: 12px;");
    m_lblSubtotals = new QLabel("Subtotals: \xe2\x80\x94", m_summaryFrame);
    m_lblSubtotals->setStyleSheet("font-weight: 600; color: #2563EB; font-size: 12px;");
    m_lblRenamed = new QLabel("Renamed: \xe2\x80\x94", m_summaryFrame);
    m_lblRenamed->setStyleSheet("font-weight: 600; color: #7C3AED; font-size: 12px;");

    summaryLayout->addWidget(m_lblCleared);
    summaryLayout->addWidget(m_lblPreserved);
    summaryLayout->addWidget(m_lblSubtotals);
    summaryLayout->addWidget(m_lblRenamed);
    summaryLayout->addStretch();
    leftLayout->addWidget(m_summaryFrame);

    scrollArea->setWidget(leftPanel);
    splitter->addWidget(scrollArea);

    // ═══════════════════════════════════════════
    // RIGHT panel — Log + Sheet Preview
    // ═══════════════════════════════════════════
    QWidget* rightPanel = new QWidget();
    rightPanel->setMinimumWidth(300);
    QVBoxLayout* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(12, 16, 16, 16);
    rightLayout->setSpacing(8);

    // Vertical splitter: log on top, sheets on bottom
    QSplitter* rightSplitter = new QSplitter(Qt::Vertical, rightPanel);
    rightSplitter->setHandleWidth(4);
    rightSplitter->setStyleSheet(
        "QSplitter::handle { background: #E5E7EB; }"
        "QSplitter::handle:hover { background: #3B82F6; }");

    // --- Log section ---
    QWidget* logSection = new QWidget();
    QVBoxLayout* logLayout = new QVBoxLayout(logSection);
    logLayout->setContentsMargins(0, 0, 0, 0);
    logLayout->setSpacing(4);

    QLabel* logTitle = new QLabel("\xF0\x9F\x93\x8B  Operation Log", logSection);
    logTitle->setStyleSheet("font-weight: 700; font-size: 13px; color: #1F2937;");
    logLayout->addWidget(logTitle);

    m_logEdit = new QPlainTextEdit(logSection);
    m_logEdit->setReadOnly(true);
    m_logEdit->setStyleSheet(
        "QPlainTextEdit {"
        "  background: #0F172A; color: #E2E8F0;"
        "  font-family: 'Consolas', 'Courier New', monospace; font-size: 11px;"
        "  border-radius: 8px; border: 1px solid #1E293B; padding: 8px; }"
        "QScrollBar:vertical { background: #1E293B; width: 8px; border-radius: 4px; }"
        "QScrollBar::handle:vertical { background: #475569; border-radius: 4px; }");
    m_logEdit->setPlaceholderText("Log output will appear here when the operation starts...");
    logLayout->addWidget(m_logEdit, 1);

    QPushButton* clearLogBtn = new QPushButton("Clear Log", logSection);
    clearLogBtn->setStyleSheet(
        "QPushButton { background: #1E293B; color: #94A3B8; font-size: 11px;"
        "  padding: 4px 12px; border-radius: 5px; border: none; }"
        "QPushButton:hover { background: #334155; color: white; }");
    clearLogBtn->setFixedHeight(26);
    connect(clearLogBtn, &QPushButton::clicked, m_logEdit, &QPlainTextEdit::clear);
    QHBoxLayout* clearRow = new QHBoxLayout();
    clearRow->addStretch();
    clearRow->addWidget(clearLogBtn);
    logLayout->addLayout(clearRow);

    rightSplitter->addWidget(logSection);

    // --- Sheet preview section ---
    QWidget* sheetSection = new QWidget();
    QVBoxLayout* sheetLayout = new QVBoxLayout(sheetSection);
    sheetLayout->setContentsMargins(0, 0, 0, 0);
    sheetLayout->setSpacing(4);

    QLabel* sheetTitle = new QLabel("\xF0\x9F\x93\x84  Sheet Preview", sheetSection);
    sheetTitle->setStyleSheet("font-weight: 700; font-size: 13px; color: #1F2937;");
    sheetLayout->addWidget(sheetTitle);

    m_sheetList = new QListWidget(sheetSection);
    m_sheetList->setStyleSheet(
        "QListWidget { background: #FAFBFC; border: 1px solid #E5E7EB; border-radius: 8px;"
        "  font-size: 12px; padding: 4px; }"
        "QListWidget::item { padding: 4px 8px; border-radius: 4px; }"
        "QListWidget::item:selected { background: #DBEAFE; color: #1E40AF; }");
    m_sheetList->setAlternatingRowColors(true);
    m_sheetList->addItem("(No workbook loaded \xe2\x80\x94 click Preview Sheets or browse a file)");
    sheetLayout->addWidget(m_sheetList, 1);

    rightSplitter->addWidget(sheetSection);
    rightSplitter->setSizes({400, 200});

    rightLayout->addWidget(rightSplitter, 1);

    splitter->addWidget(rightPanel);
    splitter->setSizes({500, 380});
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    // ── Wire signals ──────────────────────────────────────────────────────
    connect(m_sourceBtn,        &QPushButton::clicked, this, &YearRolloverTab::onBrowseSource);
    connect(m_autoDetectBtn,    &QPushButton::clicked, this, &YearRolloverTab::onAutoDetectSource);
    connect(m_destBtn,          &QPushButton::clicked, this, &YearRolloverTab::onBrowseDest);
    connect(m_subtotalsBtn,     &QPushButton::clicked, this, &YearRolloverTab::onBrowseSubtotals);
    connect(m_dryRunBtn,        &QPushButton::clicked, this, &YearRolloverTab::onDryRun);
    connect(m_executeBtn,       &QPushButton::clicked, this, &YearRolloverTab::onExecute);
    connect(m_cancelBtn,        &QPushButton::clicked, this, &YearRolloverTab::onCancel);
    connect(m_openResBtn,       &QPushButton::clicked, this, &YearRolloverTab::onOpenResult);
    connect(m_resetBtn,         &QPushButton::clicked, this, &YearRolloverTab::onReset);
    connect(m_createFoldersBtn, &QPushButton::clicked, this, &YearRolloverTab::onCreateFolders);
    connect(m_copyToDestBtn,    &QPushButton::clicked, this, &YearRolloverTab::onCopyToDestination);
    connect(m_previewSheetsBtn, &QPushButton::clicked, this, &YearRolloverTab::onPreviewSheets);
    connect(m_sourceFileEdit,   &QLineEdit::textChanged, this, &YearRolloverTab::onSourceChanged);
    connect(m_yearPicker, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { updateAutoDestPath(); });
}

// ═══════════════════════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════════════════════

void YearRolloverTab::appendLog(const QString& line)
{
    if (!m_logEdit) return;
    QString html;
    if (line.startsWith("[OK]"))
        html = QString("<span style='color:#4ADE80'>%1</span>").arg(line.toHtmlEscaped());
    else if (line.startsWith("[ERROR]"))
        html = QString("<span style='color:#F87171'>%1</span>").arg(line.toHtmlEscaped());
    else if (line.startsWith("[WARN]"))
        html = QString("<span style='color:#FCD34D'>%1</span>").arg(line.toHtmlEscaped());
    else if (line.startsWith("[INFO]"))
        html = QString("<span style='color:#93C5FD'>%1</span>").arg(line.toHtmlEscaped());
    else
        html = line.toHtmlEscaped();

    m_logEdit->appendHtml(html);
    QScrollBar* sb = m_logEdit->verticalScrollBar();
    if (sb) sb->setValue(sb->maximum());
}

void YearRolloverTab::setRunning(bool running)
{
    m_executeBtn->setEnabled(!running);
    m_dryRunBtn->setEnabled(!running);
    m_resetBtn->setEnabled(!running);
    m_cancelBtn->setVisible(running);
    m_sourceBtn->setEnabled(!running);
    m_autoDetectBtn->setEnabled(!running);
    m_destBtn->setEnabled(!running);
    m_createFoldersBtn->setEnabled(!running);
    m_previewSheetsBtn->setEnabled(!running);
    m_progressBar->setVisible(running);
    if (!running) m_progressBar->setValue(0);
}

QString YearRolloverTab::subtotalsConfigPath() const
{
    return m_subtotalsPathEdit ? m_subtotalsPathEdit->text().trimmed() : QString();
}

void YearRolloverTab::updateAutoDestPath()
{
    QString src = m_sourceFileEdit->text().trimmed();
    if (src.isEmpty()) return;

    QFileInfo fi(src);
    int newYear = m_yearPicker->value();

    // Generate output name following Cost Control naming convention
    QString destName = QString("Cost Control ZAG 01_%1_working.xlsm").arg(newYear);
    m_destFileEdit->setText(fi.absolutePath() + "/" + destName);
}

// ═══════════════════════════════════════════════════════════════════════════
// Slots
// ═══════════════════════════════════════════════════════════════════════════

void YearRolloverTab::onSourceChanged()
{
    updateAutoDestPath();
}

void YearRolloverTab::onBrowseSource()
{
    QString startDir = m_mainWindow ? m_mainWindow->destFolder() : QString();
    QString path = QFileDialog::getOpenFileName(
        this, "Select Completed Workbook", startDir,
        "Excel Files (*.xlsx *.xlsm *.xls);;All Files (*)");
    if (!path.isEmpty()) {
        m_sourceFileEdit->setText(path);
    }
}

void YearRolloverTab::onAutoDetectSource()
{
    QString baseDir = m_mainWindow ? m_mainWindow->destFolder() : QString();
    if (baseDir.isEmpty()) {
        QMessageBox::warning(this, "No Base Folder",
            "Base folder is not configured. Please browse for the source file manually.");
        return;
    }

    int prevYear = m_yearPicker->value() - 1;
    appendLog(QString("[INFO] Auto-detecting source file for year %1 in %2...").arg(prevYear).arg(baseDir));

    // Scan Dec -> Jan to find the last existing cost control file
    for (int m = 12; m >= 1; --m) {
        QString mm = QString("%1").arg(m, 2, 10, QChar('0'));
        QString testFolder = QString("%1/%2/%3/test").arg(baseDir).arg(prevYear).arg(mm);
        QDir dir(testFolder);
        if (!dir.exists()) continue;

        QStringList filters = {"Cost Control*.xlsm", "Cost Control*.xlsx"};
        QStringList files = dir.entryList(filters, QDir::Files, QDir::Name);
        if (!files.isEmpty()) {
            QString found = dir.absoluteFilePath(files.first());
            m_sourceFileEdit->setText(found);
            appendLog(QString("[OK] Found: %1").arg(found));
            m_statusLabel->setText(QString("Auto-detected source: %1").arg(QFileInfo(found).fileName()));
            m_statusLabel->setStyleSheet("color: #059669; font-weight: 600; font-size: 12px;");
            return;
        }
    }

    appendLog(QString("[WARN] No Cost Control file found for year %1 in %2").arg(prevYear).arg(baseDir));
    QMessageBox::information(this, "Not Found",
        QString("No Cost Control file found for %1.\n\n"
                "Searched: %2/%1/01..12/test/\n\n"
                "Please browse manually.").arg(prevYear).arg(baseDir));
}

void YearRolloverTab::onBrowseDest()
{
    QString startPath = m_destFileEdit->text().isEmpty()
        ? (m_mainWindow ? m_mainWindow->destFolder() : QString())
        : m_destFileEdit->text();
    QString path = QFileDialog::getSaveFileName(
        this, "Save Prepared Workbook As", startPath,
        "Excel Files (*.xlsx *.xlsm);;All Files (*)");
    if (!path.isEmpty()) {
        m_destFileEdit->setText(path);
    }
}

void YearRolloverTab::onBrowseSubtotals()
{
    QString path = QFileDialog::getOpenFileName(
        this, "Select Subtotals Config", QString(),
        "JSON Files (*.json);;All Files (*)");
    if (!path.isEmpty())
        m_subtotalsPathEdit->setText(path);
}

void YearRolloverTab::onDryRun()
{
    m_dryRunCheck->setChecked(true);
    onExecute();
}

void YearRolloverTab::onReset()
{
    m_sourceFileEdit->clear();
    m_destFileEdit->clear();
    m_yearPicker->setValue(QDateTime::currentDateTime().date().year() + 1);
    m_sheetNameEdit->setText("MZLZ Consolidated");
    m_startRowPicker->setValue(5);
    m_endRowPicker->setValue(300);
    m_clearBaseCheck->setChecked(true);
    m_clearCumulCheck->setChecked(true);
    m_clearJGCheck->setChecked(true);
    m_clearBudgetCheck->setChecked(true);
    m_renameSheetCheck->setChecked(true);
    m_processAllSheetsCheck->setChecked(false);
    m_applySubtotalsCheck->setChecked(true);
    m_dryRunCheck->setChecked(false);
    m_openResBtn->setVisible(false);
    m_copyToDestBtn->setVisible(false);
    m_lastResultFile.clear();
    m_lastSheetsRenamed = 0;
    m_logEdit->clear();
    m_sheetList->clear();
    m_sheetList->addItem("(No workbook loaded)");
    m_summaryFrame->setVisible(false);
    m_statusLabel->setText("Ready \xe2\x80\x94 select a source file to begin.");
    m_statusLabel->setStyleSheet("color: #6B7280; font-style: italic; font-size: 12px; padding: 2px 0;");
}

void YearRolloverTab::onPreviewSheets()
{
    QString src = m_sourceFileEdit->text().trimmed();
    if (src.isEmpty()) {
        QMessageBox::warning(this, "No Source File",
            "Please select a source file first.");
        return;
    }

    if (!QFile::exists(src)) {
        QMessageBox::warning(this, "File Not Found",
            QString("Source file not found:\n%1").arg(src));
        return;
    }

    ExcelHandler handler;
    QString key = "preview_sheets_" + QString::number(QDateTime::currentMSecsSinceEpoch());
    if (!handler.loadWorkbook(src, key)) {
        QMessageBox::warning(this, "Load Failed",
            "Failed to load the workbook for sheet preview.");
        return;
    }

    QStringList sheets = handler.getSheetNames(key);
    handler.unloadWorkbook(key);

    m_sheetList->clear();
    int oldYear = m_yearPicker->value() - 1;
    QString oldYearStr = QString::number(oldYear);

    for (const QString& name : sheets) {
        QListWidgetItem* item = new QListWidgetItem(name);
        if (name.compare(m_sheetNameEdit->text(), Qt::CaseInsensitive) == 0) {
            item->setBackground(QColor("#DBEAFE"));
            item->setForeground(QColor("#1E40AF"));
            item->setText(name + "  \xe2\x9c\x93 (target)");
        } else if (name.contains(oldYearStr)) {
            item->setBackground(QColor("#FEF3C7"));
            item->setForeground(QColor("#92400E"));
            item->setText(name + QString("  \xe2\x86\x92 %1").arg(
                              QString(name).replace(oldYearStr, QString::number(m_yearPicker->value()))));
        }
        m_sheetList->addItem(item);
    }

    appendLog(QString("[INFO] Loaded %1 sheets from source file.").arg(sheets.size()));
    m_statusLabel->setText(QString("Found %1 sheets in source file.").arg(sheets.size()));
    m_statusLabel->setStyleSheet("color: #3B82F6; font-weight: 500; font-size: 12px;");
}

void YearRolloverTab::onCreateFolders()
{
    QString baseDir = m_mainWindow ? m_mainWindow->destFolder() : QString();
    if (baseDir.isEmpty()) {
        baseDir = QFileDialog::getExistingDirectory(this, "Select Base Directory",
                                                     "L:/Cost control/Cost Control/Cost control");
        if (baseDir.isEmpty()) return;
    }

    int newYear = m_yearPicker->value();

    QStringList subfolders = {"test", "SAP export monthly", "SAP YTD", "Traffic"};
    int created = 0;
    int existed = 0;

    appendLog(QString("[INFO] Creating folder structure for year %1...").arg(newYear));

    for (int m = 1; m <= 12; ++m) {
        QString mm = QString("%1").arg(m, 2, 10, QChar('0'));
        for (const QString& sub : subfolders) {
            QString path = QString("%1/%2/%3/%4").arg(baseDir).arg(newYear).arg(mm).arg(sub);
            QDir dir(path);
            if (dir.exists()) {
                ++existed;
            } else if (dir.mkpath(".")) {
                ++created;
            } else {
                appendLog(QString("[ERROR] Failed to create: %1").arg(path));
            }
        }
    }

    QString msg = QString("Created %1 folders, %2 already existed.").arg(created).arg(existed);
    appendLog(QString("[OK] %1").arg(msg));
    m_statusLabel->setText(msg);
    m_statusLabel->setStyleSheet("color: #059669; font-weight: 600; font-size: 12px;");

    QMessageBox::information(this, "Folders Created",
        QString("Year %1 folder structure created.\n\n"
                "New folders: %2\n"
                "Already existed: %3\n\n"
                "Location: %4/%1/")
            .arg(newYear).arg(created).arg(existed).arg(baseDir));
}

void YearRolloverTab::onCopyToDestination()
{
    if (m_lastResultFile.isEmpty() || !QFile::exists(m_lastResultFile)) {
        QMessageBox::warning(this, "No Result File",
            "No prepared file available. Run the rollover first.");
        return;
    }

    QString baseDir = m_mainWindow ? m_mainWindow->destFolder() : QString();
    if (baseDir.isEmpty()) {
        baseDir = QFileDialog::getExistingDirectory(this, "Select Base Directory");
        if (baseDir.isEmpty()) return;
    }

    int newYear = m_yearPicker->value();
    QStringList copied, failed;

    appendLog(QString("[INFO] Copying prepared workbook to all 12 month folders..."));

    for (int m = 1; m <= 12; ++m) {
        QString mm = QString("%1").arg(m, 2, 10, QChar('0'));
        QString destDir = QString("%1/%2/%3/test").arg(baseDir).arg(newYear).arg(mm);
        QDir().mkpath(destDir); // ensure exists

        QString destName = QString("Cost Control ZAG %1_%2_working.xlsm").arg(mm).arg(newYear);
        QString destPath = destDir + "/" + destName;

        if (QFile::exists(destPath)) {
            // Skip if already exists to avoid accidental overwrite
            appendLog(QString("[WARN] Already exists, skipping: %1").arg(destPath));
            continue;
        }

        if (QFile::copy(m_lastResultFile, destPath)) {
            copied << mm;
        } else {
            failed << mm;
            appendLog(QString("[ERROR] Failed to copy to: %1").arg(destPath));
        }
    }

    QString msg = QString("Copied to %1 month folders.").arg(copied.size());
    if (!failed.isEmpty())
        msg += QString(" Failed: %1.").arg(failed.join(", "));

    appendLog(QString("[OK] %1").arg(msg));
    m_statusLabel->setText(msg);
    m_statusLabel->setStyleSheet("color: #059669; font-weight: 600; font-size: 12px;");

    QMessageBox::information(this, "Copy Complete", msg);
}

void YearRolloverTab::onExecute()
{
    // ── Validation ────────────────────────────────────────────────────────
    if (m_sourceFileEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Missing Source File",
            "Please select a source workbook to prepare.");
        return;
    }

    if (!QFile::exists(m_sourceFileEdit->text().trimmed())) {
        QMessageBox::warning(this, "Source File Not Found",
            QString("The source file does not exist:\n%1").arg(m_sourceFileEdit->text()));
        return;
    }

    bool isDryRun = m_dryRunCheck->isChecked();

    if (!isDryRun && m_destFileEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Missing Destination",
            "Please choose where to save the prepared workbook.");
        return;
    }

    if (m_sheetNameEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Missing Sheet Name",
            "Please enter the name of the sheet to process.");
        return;
    }

    // Check at least one column group is selected
    if (!m_clearBaseCheck->isChecked() && !m_clearCumulCheck->isChecked()
        && !m_clearJGCheck->isChecked() && !m_clearBudgetCheck->isChecked()) {
        QMessageBox::warning(this, "No Columns Selected",
            "Please select at least one column group to clear.");
        return;
    }

    // Row range sanity
    if (m_startRowPicker->value() > m_endRowPicker->value()) {
        QMessageBox::warning(this, "Invalid Row Range",
            "Start row must be less than or equal to end row.");
        return;
    }

    // ── File-lock check (skip for dry run) ───────────────────────────────
    if (!isDryRun) {
        QString dest = m_destFileEdit->text().trimmed();
        if (QFile::exists(dest)) {
            QFile testFile(dest);
            if (!testFile.open(QIODevice::ReadWrite)) {
                QMessageBox::warning(this, "File is Open",
                    QString("The destination file appears to be open in another application:\n\n%1\n\n"
                            "Please close it and try again.").arg(QFileInfo(dest).fileName()));
                return;
            }
            testFile.close();
        }
    }

    // ── Confirmation for non-dry run ─────────────────────────────────────
    if (!isDryRun) {
        QStringList actions;
        if (m_clearBaseCheck->isChecked())   actions << "Clear 12 base monthly columns";
        if (m_clearCumulCheck->isChecked())  actions << "Clear 12 cumulative columns";
        if (m_clearJGCheck->isChecked())     actions << "Clear JG (YTD) column";
        if (m_clearBudgetCheck->isChecked()) actions << "Clear 36 Budget/REFI/PrevYear columns";
        if (m_renameSheetCheck->isChecked()) actions << QString("Rename sheet year references to %1").arg(m_yearPicker->value());
        if (m_applySubtotalsCheck->isChecked()) actions << "Restore subtotal formulas";

        QString confirmMsg = QString(
            "This will prepare the workbook for year %1:\n\n\xe2\x80\xa2 %2\n\n"
            "Row range: %3 to %4\n"
            "Sheet: %5\n\n"
            "Continue?")
            .arg(m_yearPicker->value())
            .arg(actions.join("\n\xe2\x80\xa2 "))
            .arg(m_startRowPicker->value())
            .arg(m_endRowPicker->value())
            .arg(m_sheetNameEdit->text());

        if (QMessageBox::question(this, "Confirm Rollover", confirmMsg,
                                   QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
    }

    // ── Stop previous worker if still alive ──────────────────────────────
    if (m_worker) {
        m_worker->requestStop();
        m_worker->wait(3000);
        m_worker->deleteLater();
        m_worker = nullptr;
    }

    // ── Build config ──────────────────────────────────────────────────────
    RolloverConfig config;
    config.sourceFile           = m_sourceFileEdit->text().trimmed();
    config.destFile             = isDryRun ? QString() : m_destFileEdit->text().trimmed();
    config.targetYear           = m_yearPicker->value();
    config.startRow             = m_startRowPicker->value();
    config.endRow               = m_endRowPicker->value();
    config.sheetName            = m_sheetNameEdit->text().trimmed();
    config.dryRun               = isDryRun;
    config.applySubtotals       = m_applySubtotalsCheck->isChecked();
    config.subtotalsConfigPath  = subtotalsConfigPath();
    config.clearBase            = m_clearBaseCheck->isChecked();
    config.clearCumul           = m_clearCumulCheck->isChecked();
    config.clearJG              = m_clearJGCheck->isChecked();
    config.clearBudget          = m_clearBudgetCheck->isChecked();
    config.renameSheets         = m_renameSheetCheck->isChecked();
    config.processAllSheets     = m_processAllSheetsCheck->isChecked();

    // ── Clear log / hide summary / hide open-result ───────────────────────
    m_logEdit->clear();
    m_summaryFrame->setVisible(false);
    m_openResBtn->setVisible(false);
    m_copyToDestBtn->setVisible(false);
    m_lastResultFile.clear();
    m_lastSheetsRenamed = 0;

    if (isDryRun)
        appendLog("[INFO] === DRY RUN mode \xe2\x80\x94 no files will be written ===");
    else
        appendLog(QString("[INFO] === Prepare New Year %1 started ===").arg(config.targetYear));

    // ── Launch worker ─────────────────────────────────────────────────────
    m_worker = new RolloverWorker(config, this);
    connect(m_worker, &RolloverWorker::progress,  this, &YearRolloverTab::onWorkerProgress);
    connect(m_worker, &RolloverWorker::logLine,   this, &YearRolloverTab::onWorkerLogLine);
    connect(m_worker, &RolloverWorker::finished,  this, &YearRolloverTab::onWorkerFinished);

    setRunning(true);
    m_statusLabel->setText(isDryRun ? "Dry run in progress..." : "Preparing workbook...");
    m_statusLabel->setStyleSheet("color: #3B82F6; font-weight: 600; font-size: 12px;");
    m_worker->start();
}

void YearRolloverTab::onCancel()
{
    if (m_worker && m_worker->isRunning()) {
        appendLog("[WARN] Cancel requested \xe2\x80\x94 waiting for current row to finish...");
        m_cancelBtn->setEnabled(false);
        m_worker->requestStop();
    }
}

void YearRolloverTab::onOpenResult()
{
    if (m_lastResultFile.isEmpty()) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_lastResultFile));
}

void YearRolloverTab::onWorkerProgress(int current, int total, const QString& message)
{
    m_progressBar->setMaximum(total);
    m_progressBar->setValue(current);
    m_statusLabel->setText(message);
}

void YearRolloverTab::onWorkerLogLine(const QString& line)
{
    appendLog(line);
}

void YearRolloverTab::onWorkerFinished(bool success, const QString& message,
                                        int cellsCleared, int cellsPreserved,
                                        int subtotalsApplied, int sheetsRenamed)
{
    setRunning(false);
    m_cancelBtn->setEnabled(true);
    m_lastSheetsRenamed = sheetsRenamed;

    // Update summary strip
    m_lblCleared->setText(QString("Cleared: %1").arg(cellsCleared));
    m_lblPreserved->setText(QString("Preserved: %1").arg(cellsPreserved));
    m_lblSubtotals->setText(QString("Subtotals: %1").arg(subtotalsApplied));
    m_lblRenamed->setText(QString("Renamed: %1").arg(sheetsRenamed));
    m_summaryFrame->setVisible(true);

    if (success) {
        m_statusLabel->setText(m_dryRunCheck->isChecked()
            ? "Dry run complete. No files were written."
            : "Workbook prepared successfully!");
        m_statusLabel->setStyleSheet("color: #059669; font-weight: 600; font-size: 12px;");

        if (!m_dryRunCheck->isChecked() && !m_destFileEdit->text().isEmpty()) {
            m_lastResultFile = m_destFileEdit->text().trimmed();
            m_openResBtn->setVisible(QFile::exists(m_lastResultFile));
            m_copyToDestBtn->setVisible(QFile::exists(m_lastResultFile));
        }

        QMessageBox::information(this,
            m_dryRunCheck->isChecked() ? "Dry Run Complete" : "Rollover Complete",
            message);
    } else {
        m_statusLabel->setText("Operation failed or was cancelled.");
        m_statusLabel->setStyleSheet("color: #DC2626; font-weight: 600; font-size: 12px;");
        if (!message.contains("cancelled", Qt::CaseInsensitive))
            QMessageBox::critical(this, "Rollover Failed", message);
    }

    // Reset dry-run checkbox
    if (m_dryRunCheck->isChecked())
        m_dryRunCheck->setChecked(false);

    m_worker->deleteLater();
    m_worker = nullptr;
}