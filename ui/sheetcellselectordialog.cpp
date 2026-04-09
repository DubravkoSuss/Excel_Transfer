#include "sheetcellselectordialog.h"
#include "../services/excelhandler.h"
#include <QDebug>
#include <QApplication>
#include <QScreen>

SheetCellSelectorDialog::SheetCellSelectorDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    m_currentSelection = {"Sheet1", "A", 1};
}

SheetCellSelectorDialog::~SheetCellSelectorDialog()
{
}

void SheetCellSelectorDialog::setupUI()
{
    setWindowTitle("Select Cell");
    setMinimumSize(900, 600);
    setModal(true);

    // Center on screen
    QScreen* screen = QGuiApplication::primaryScreen();
    if (screen) {
        QRect sg = screen->availableGeometry();
        resize(qMin(1200, sg.width() - 100), qMin(700, sg.height() - 100));
        move(sg.center() - rect().center());
    }

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    // Title
    QLabel* titleLabel = new QLabel("Select Cell — click a cell in the grid or enter manually");
    titleLabel->setStyleSheet("font-size: 15px; font-weight: 700; color: #1E40AF;");
    mainLayout->addWidget(titleLabel);

    // Top controls: sheet selector + manual column/row
    QHBoxLayout* controlsLayout = new QHBoxLayout();
    controlsLayout->setSpacing(16);

    controlsLayout->addWidget(new QLabel("Sheet:"));
    m_sheetCombo = new QComboBox();
    m_sheetCombo->setMinimumWidth(200);
    m_sheetCombo->setStyleSheet("padding: 6px;");
    connect(m_sheetCombo, &QComboBox::currentIndexChanged, this, &SheetCellSelectorDialog::onSheetChanged);
    controlsLayout->addWidget(m_sheetCombo);

    controlsLayout->addWidget(new QLabel("Column:"));
    m_columnEdit = new QLineEdit();
    m_columnEdit->setFixedWidth(70);
    m_columnEdit->setPlaceholderText("e.g. A");
    m_columnEdit->setStyleSheet("padding: 6px;");
    connect(m_columnEdit, &QLineEdit::textChanged, this, &SheetCellSelectorDialog::onColumnChanged);
    controlsLayout->addWidget(m_columnEdit);

    controlsLayout->addWidget(new QLabel("Row:"));
    m_rowSpinBox = new QSpinBox();
    m_rowSpinBox->setMinimum(1);
    m_rowSpinBox->setMaximum(1048576);
    m_rowSpinBox->setFixedWidth(90);
    m_rowSpinBox->setStyleSheet("padding: 6px;");
    connect(m_rowSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &SheetCellSelectorDialog::onRowChanged);
    controlsLayout->addWidget(m_rowSpinBox);

    controlsLayout->addStretch();

    m_selectedCellLabel = new QLabel("Selected: —");
    m_selectedCellLabel->setStyleSheet("font-weight: 600; color: #059669; font-size: 13px;");
    controlsLayout->addWidget(m_selectedCellLabel);

    mainLayout->addLayout(controlsLayout);

    // Data grid
    m_tableWidget = new QTableWidget(this);
    m_tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tableWidget->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_tableWidget->horizontalHeader()->setDefaultSectionSize(80);
    m_tableWidget->verticalHeader()->setDefaultSectionSize(24);
    m_tableWidget->setStyleSheet(
        "QTableWidget { border: 1px solid #D1D5DB; font-size: 12px; }"
        "QTableWidget::item:selected { background: #BFDBFE; color: #1E3A5F; }"
        "QTableWidget::item:hover { background: #EFF6FF; }"
    );
    connect(m_tableWidget, &QTableWidget::cellClicked, this, &SheetCellSelectorDialog::onTableCellClicked);
    mainLayout->addWidget(m_tableWidget, 1);

    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    m_selectBtn = new QPushButton("✓  Select");
    m_selectBtn->setStyleSheet(
        "background-color: #059669; color: white; font-weight: 600;"
        "padding: 10px 24px; border-radius: 6px;"
    );
    connect(m_selectBtn, &QPushButton::clicked, this, &SheetCellSelectorDialog::onSelectClicked);
    buttonLayout->addWidget(m_selectBtn);

    m_cancelBtn = new QPushButton("Cancel");
    m_cancelBtn->setStyleSheet(
        "background-color: #F3F4F6; color: #374151;"
        "padding: 10px 20px; border-radius: 6px;"
    );
    connect(m_cancelBtn, &QPushButton::clicked, this, &SheetCellSelectorDialog::onCancelClicked);
    buttonLayout->addWidget(m_cancelBtn);

    mainLayout->addLayout(buttonLayout);

    updateFields();
}

void SheetCellSelectorDialog::setExcelHandler(ExcelHandler* handler, const QString& workbookKey)
{
    m_handler = handler;
    m_workbookKey = workbookKey;
    populateTable();
}

void SheetCellSelectorDialog::populateTable()
{
    m_tableWidget->clear();
    if (!m_handler || m_workbookKey.isEmpty()) {
        m_tableWidget->setRowCount(0);
        m_tableWidget->setColumnCount(0);
        return;
    }

    const QString sheet = m_sheetCombo->currentText();
    if (sheet.isEmpty()) return;

    // Set up columns A–Z (26 cols) + beyond if needed
    QStringList colHeaders;
    for (int c = 1; c <= PREVIEW_COLS; ++c)
        colHeaders << columnNumberToLetter(c);
    m_tableWidget->setColumnCount(PREVIEW_COLS);
    m_tableWidget->setHorizontalHeaderLabels(colHeaders);

    // Row headers 1..PREVIEW_ROWS
    m_tableWidget->setRowCount(PREVIEW_ROWS);
    QStringList rowHeaders;
    for (int r = 1; r <= PREVIEW_ROWS; ++r)
        rowHeaders << QString::number(r);
    m_tableWidget->setVerticalHeaderLabels(rowHeaders);

    // Populate cells from ExcelHandler
    for (int r = 1; r <= PREVIEW_ROWS; ++r) {
        for (int c = 1; c <= PREVIEW_COLS; ++c) {
            QVariant val = m_handler->getCellValue(m_workbookKey, sheet, r, c);
            QString text = val.isValid() ? val.toString() : "";
            if (text.length() > 20) text = text.left(17) + "...";
            auto* item = new QTableWidgetItem(text);
            item->setToolTip(val.toString());
            m_tableWidget->setItem(r - 1, c - 1, item);
        }
    }

    // Highlight current selection
    int selCol = columnLetterToNumber(m_currentSelection.column) - 1;
    int selRow = m_currentSelection.row - 1;
    if (selRow >= 0 && selRow < PREVIEW_ROWS && selCol >= 0 && selCol < PREVIEW_COLS) {
        m_tableWidget->setCurrentCell(selRow, selCol);
    }
}

void SheetCellSelectorDialog::setSheetNames(const QStringList& sheetNames)
{
    m_sheetCombo->clear();
    m_sheetCombo->addItems(sheetNames);
}

void SheetCellSelectorDialog::setCellSelection(const CellSelection& selection)
{
    m_currentSelection = selection;
    updateFields();
}

SheetCellSelectorDialog::CellSelection SheetCellSelectorDialog::getCellSelection() const
{
    return m_currentSelection;
}

QList<SheetCellSelectorDialog::CellSelection> SheetCellSelectorDialog::getCellSelections() const
{
    QList<CellSelection> list;
    QList<QTableWidgetItem*> selectedItems = m_tableWidget->selectedItems();
    if (selectedItems.size() > 1) {
        for (QTableWidgetItem* item : selectedItems) {
            CellSelection sel;
            sel.sheetName = m_sheetCombo->currentText();
            sel.column = columnNumberToLetter(item->column() + 1);
            sel.row = item->row() + 1;
            list.append(sel);
        }
    } else {
        list.append(m_currentSelection);
    }
    return list;
}

void SheetCellSelectorDialog::updateFields()
{
    int sheetIndex = m_sheetCombo->findText(m_currentSelection.sheetName);
    if (sheetIndex != -1) {
        m_sheetCombo->setCurrentIndex(sheetIndex);
    }
    m_columnEdit->setText(m_currentSelection.column);
    m_rowSpinBox->setValue(m_currentSelection.row);
}

void SheetCellSelectorDialog::onSelectClicked()
{
    m_currentSelection.sheetName = m_sheetCombo->currentText();
    m_currentSelection.column = m_columnEdit->text().toUpper().trimmed();
    m_currentSelection.row = m_rowSpinBox->value();

    // Validate column
    for (QChar c : m_currentSelection.column) {
        if (!c.isLetter()) {
            m_columnEdit->setStyleSheet(
                "padding: 8px;"
                "border: 1px solid #DC2626;"
                "background-color: #FEF2F2;"
            );
            return;
        }
    }

    emit cellSelected(m_currentSelection);
    accept();
}

void SheetCellSelectorDialog::onCancelClicked()
{
    reject();
}

void SheetCellSelectorDialog::onSheetChanged(int index)
{
    Q_UNUSED(index);
    m_currentSelection.sheetName = m_sheetCombo->currentText();
    populateTable(); // repopulate grid when sheet changes
}

void SheetCellSelectorDialog::onTableCellClicked(int row, int col)
{
    // Convert 0-based table indices to 1-based Excel row/col
    m_currentSelection.column = columnNumberToLetter(col + 1);
    m_currentSelection.row    = row + 1;

    // Update the manual input fields
    m_columnEdit->blockSignals(true);
    m_rowSpinBox->blockSignals(true);
    m_columnEdit->setText(m_currentSelection.column);
    m_rowSpinBox->setValue(m_currentSelection.row);
    m_columnEdit->blockSignals(false);
    m_rowSpinBox->blockSignals(false);

    // Update selected cell label
    QString cellRef = QString("%1%2").arg(m_currentSelection.column).arg(m_currentSelection.row);
    m_selectedCellLabel->setText(QString("Selected: %1!%2").arg(m_currentSelection.sheetName, cellRef));

    // Show cell value in tooltip
    if (m_tableWidget->item(row, col)) {
        QString val = m_tableWidget->item(row, col)->toolTip();
        if (!val.isEmpty())
            m_selectedCellLabel->setToolTip(val);
    }
}

void SheetCellSelectorDialog::onColumnChanged(const QString& text)
{
    Q_UNUSED(text);
    m_currentSelection.column = m_columnEdit->text().toUpper().trimmed();

    // Reset validation style
    m_columnEdit->setStyleSheet("padding: 8px;");
}

void SheetCellSelectorDialog::onRowChanged(int value)
{
    m_currentSelection.row = value;
}

QString SheetCellSelectorDialog::columnNumberToLetter(int column) const
{
    QString letter;
    while (column > 0) {
        column--;
        letter.prepend(QChar('A' + (column % 26)));
        column /= 26;
    }
    return letter;
}

int SheetCellSelectorDialog::columnLetterToNumber(const QString& column) const
{
    int number = 0;
    for (QChar c : column.toUpper()) {
        number = number * 26 + (c.unicode() - 'A' + 1);
    }
    return number;
}