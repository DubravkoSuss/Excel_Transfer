// Include necessary Qt headers at the beginning of sheetcellselectordialog.cpp
#include "sheetcellselectordialog.h"
#include <QDebug>

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
    setMinimumSize(400, 250);
    setModal(true);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);

    // Title
    QLabel* titleLabel = new QLabel("Select Cell");
    titleLabel->setStyleSheet(
        "font-size: 18px;"
        "font-weight: 700;"
        "color: #1E40AF;"
    );
    mainLayout->addWidget(titleLabel);

    // Form Layout
    QFormLayout* formLayout = new QFormLayout();
    formLayout->setSpacing(12);
    formLayout->setLabelAlignment(Qt::AlignRight);

    // Sheet Combobox
    m_sheetCombo = new QComboBox();
    m_sheetCombo->setStyleSheet("padding: 8px;");
    connect(m_sheetCombo, &QComboBox::currentIndexChanged, this, &SheetCellSelectorDialog::onSheetChanged);
    formLayout->addRow("Sheet:", m_sheetCombo);

    // Column Line Edit
    m_columnEdit = new QLineEdit();
    m_columnEdit->setPlaceholderText("Enter column letter (e.g., A, B, Z)");
    m_columnEdit->setStyleSheet("padding: 8px;");
    connect(m_columnEdit, &QLineEdit::textChanged, this, &SheetCellSelectorDialog::onColumnChanged);
    formLayout->addRow("Column:", m_columnEdit);

    // Row Spin Box
    m_rowSpinBox = new QSpinBox();
    m_rowSpinBox->setMinimum(1);
    m_rowSpinBox->setMaximum(1048576);
    m_rowSpinBox->setStyleSheet("padding: 8px;");
    connect(m_rowSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &SheetCellSelectorDialog::onRowChanged);
    formLayout->addRow("Row:", m_rowSpinBox);

    mainLayout->addLayout(formLayout);

    // Button Box
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    m_selectBtn = new QPushButton("Select");
    m_selectBtn->setStyleSheet(
        "background-color: #059669;"
        "color: white;"
        "font-weight: 600;"
        "padding: 10px 20px;"
        "border-radius: 6px;"
    );
    connect(m_selectBtn, &QPushButton::clicked, this, &SheetCellSelectorDialog::onSelectClicked);
    buttonLayout->addWidget(m_selectBtn);

    m_cancelBtn = new QPushButton("Cancel");
    m_cancelBtn->setStyleSheet(
        "background-color: #F3F4F6;"
        "color: #374151;"
        "padding: 10px 20px;"
        "border-radius: 6px;"
    );
    connect(m_cancelBtn, &QPushButton::clicked, this, &SheetCellSelectorDialog::onCancelClicked);
    buttonLayout->addWidget(m_cancelBtn);

    mainLayout->addLayout(buttonLayout);

    // Set initial values
    updateFields();
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