#include "mappingrow.h"
#include "rowmapvalidatordialog.h"
#include "../../ui/sheetcellselectordialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QMessageBox>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTextStream>

MappingRow::MappingRow(int index, const QString& month, int year, const MappingEntry& entry, QWidget* parent)
    : QFrame(parent)
    , m_index(index)
    , m_month(month)
    , m_year(year)
    , m_entry(entry)
{
    buildUI();
}

MappingRow::~MappingRow() = default;

void MappingRow::buildUI()
{
    setObjectName(QString("mappingRow_%1").arg(m_index));
    setStyleSheet(QString("#mappingRow_%1 { background: white; border: 1px solid #E5E7EF; border-radius: 6px; }").arg(m_index));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(6);

    QHBoxLayout* topRow = new QHBoxLayout();
    m_checkbox = new QCheckBox();
    topRow->addWidget(m_checkbox);

    QLabel* title = new QLabel(QString("%1 %2").arg(m_month).arg(m_year));
    title->setStyleSheet("font-weight: 600; font-size: 13px;");
    topRow->addWidget(title);
    topRow->addStretch();

    m_statusLabel = new QLabel();
    m_statusLabel->setVisible(false);
    topRow->addWidget(m_statusLabel);

    QPushButton* runBtn = new QPushButton("Run");
    QPushButton* removeBtn = new QPushButton("Remove");
    connect(runBtn, &QPushButton::clicked, this, &MappingRow::onRunClicked);
    connect(removeBtn, &QPushButton::clicked, this, &MappingRow::onRemoveClicked);
    connect(m_checkbox, &QCheckBox::toggled, this, &MappingRow::changed);
    topRow->addWidget(runBtn);
    topRow->addWidget(removeBtn);

    layout->addLayout(topRow);

    QGridLayout* grid = new QGridLayout();
    m_srcSheetEdit = new QLineEdit(m_entry.sourceSheetTemplate);
    m_srcColumnEdit = new QLineEdit(m_entry.sourceColumn);
    m_destSheetEdit = new QLineEdit(m_entry.destSheet);
    m_destColumnEdit = new QLineEdit(m_entry.destColumn);

    grid->addWidget(new QLabel("Src Sheet:"), 0, 0);
    grid->addWidget(m_srcSheetEdit, 0, 1);
    grid->addWidget(new QLabel("Src Col:"), 0, 2);
    grid->addWidget(m_srcColumnEdit, 0, 3);

    grid->addWidget(new QLabel("Dest Sheet:"), 1, 0);
    grid->addWidget(m_destSheetEdit, 1, 1);
    grid->addWidget(new QLabel("Dest Col:"), 1, 2);
    grid->addWidget(m_destColumnEdit, 1, 3);

    layout->addLayout(grid);

    QHBoxLayout* rowActions = new QHBoxLayout();
    QPushButton* editRowsBtn = new QPushButton("Edit Rows");
    QPushButton* exportBtn = new QPushButton("Export RowMap");
    QPushButton* importBtn = new QPushButton("Import RowMap");

    connect(editRowsBtn, &QPushButton::clicked, this, &MappingRow::onEditRowsClicked);
    connect(exportBtn, &QPushButton::clicked, this, &MappingRow::onExportRowMapClicked);
    connect(importBtn, &QPushButton::clicked, this, &MappingRow::onImportRowMapClicked);

    rowActions->addWidget(editRowsBtn);
    rowActions->addWidget(exportBtn);
    rowActions->addWidget(importBtn);
    rowActions->addStretch();

    layout->addLayout(rowActions);

    // Source info label
    m_sourceInfoLabel = new QLabel();
    m_sourceInfoLabel->setStyleSheet("color: #6B7280; font-size: 11px;");
    m_sourceInfoLabel->setWordWrap(true);
    QString info = QString("JSON: %1 | Source: %2")
                       .arg(m_entry.sourceJson.isEmpty() ? "(unknown)" : m_entry.sourceJson)
                       .arg(m_entry.sourcePath.isEmpty() ? "(unknown)" : m_entry.sourcePath);
    m_sourceInfoLabel->setText(info);
    layout->addWidget(m_sourceInfoLabel);

    // Copy full sheet option
    QHBoxLayout* copyLayout = new QHBoxLayout();
    m_copyFullSheetCheck = new QCheckBox("Copy full sheet");
    m_copyFullSheetCheck->setChecked(m_entry.copyFullSheet);
    m_customSheetName = new QLineEdit(m_entry.customSheetName);
    m_customSheetName->setPlaceholderText("Target sheet name (optional)");
    m_customSheetName->setEnabled(m_entry.copyFullSheet);
    m_customSheetName->setMinimumWidth(180);
    m_insertAfterSheet = new QLineEdit(m_entry.insertAfterSheet);
    m_insertAfterSheet->setPlaceholderText("Insert after sheet name (optional)");
    m_insertAfterSheet->setEnabled(m_entry.copyFullSheet);
    m_insertAfterSheet->setMinimumWidth(200);

    connect(m_copyFullSheetCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_entry.copyFullSheet = checked;
        if (m_customSheetName) m_customSheetName->setEnabled(checked);
        if (m_insertAfterSheet) m_insertAfterSheet->setEnabled(checked);
        emit changed();
    });
    connect(m_customSheetName, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_entry.customSheetName = text;
        emit changed();
    });
    connect(m_insertAfterSheet, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_entry.insertAfterSheet = text;
        emit changed();
    });

    copyLayout->addWidget(m_copyFullSheetCheck);
    copyLayout->addWidget(m_customSheetName);
    copyLayout->addWidget(m_insertAfterSheet);
    copyLayout->addStretch();

    layout->addLayout(copyLayout);
}

QString MappingRow::getSourceSheet() const { return m_srcSheetEdit->text(); }
QString MappingRow::getSourceColumn() const { return m_srcColumnEdit->text(); }
QVector<int> MappingRow::getSourceRows() const { return m_entry.sourceRows; }
QString MappingRow::getDestSheet() const { return m_destSheetEdit->text(); }
QString MappingRow::getDestColumn() const { return m_destColumnEdit->text(); }
QVector<int> MappingRow::getDestRows() const { return m_entry.destRows; }

void MappingRow::setSourceSheet(const QString& sheet) { m_srcSheetEdit->setText(sheet); }
void MappingRow::setSourceColumn(const QString& col) { m_srcColumnEdit->setText(col); }
void MappingRow::setSourceRows(const QVector<int>& rows) { m_entry.sourceRows = rows; }
void MappingRow::setDestSheet(const QString& sheet) { m_destSheetEdit->setText(sheet); }
void MappingRow::setDestColumn(const QString& col) { m_destColumnEdit->setText(col); }
void MappingRow::setDestRows(const QVector<int>& rows) { m_entry.destRows = rows; }

void MappingRow::setCopyFullSheet(bool enabled, const QString& customName)
{
    m_entry.copyFullSheet = enabled;
    m_entry.customSheetName = customName;
    if (m_copyFullSheetCheck) m_copyFullSheetCheck->setChecked(enabled);
    if (m_customSheetName) {
        m_customSheetName->setText(customName);
        m_customSheetName->setEnabled(enabled);
    }
    if (m_insertAfterSheet) {
        m_insertAfterSheet->setText(m_entry.insertAfterSheet);
        m_insertAfterSheet->setEnabled(enabled);
    }
    if (m_sourceInfoLabel) {
        QString info = QString("JSON: %1 | Source: %2")
                           .arg(m_entry.sourceJson.isEmpty() ? "(unknown)" : m_entry.sourceJson)
                           .arg(m_entry.sourcePath.isEmpty() ? "(unknown)" : m_entry.sourcePath);
        m_sourceInfoLabel->setText(info);
    }
}

bool MappingRow::isCopyFullSheet() const { return m_copyFullSheetCheck && m_copyFullSheetCheck->isChecked(); }
QString MappingRow::getCustomSheetName() const { return m_customSheetName ? m_customSheetName->text() : QString(); }
QString MappingRow::getInsertAfterSheet() const { return m_insertAfterSheet ? m_insertAfterSheet->text() : QString(); }

void MappingRow::setDestSheetLabel(const QString& label)
{
    if (!label.isEmpty() && label != m_destSheetEdit->text()) {
        m_destSheetEdit->setPlaceholderText(label);
        m_destSheetEdit->setToolTip("Label: " + label);
    }
}

void MappingRow::openRowMapValidator()
{
    RowMapValidatorDialog dialog(this);
    dialog.exec();
}

void MappingRow::setTransferStatus(bool success, const QString& message)
{
    m_statusLabel->setVisible(true);
    if (success) {
        m_statusLabel->setText("OK");
        m_statusLabel->setStyleSheet("color: #16A34A; font-weight: 600;");
    } else {
        m_statusLabel->setText(message);
        m_statusLabel->setStyleSheet("color: #DC2626; font-weight: 600;");
    }
}

void MappingRow::clearTransferStatus()
{
    m_statusLabel->setVisible(false);
}

void MappingRow::onRunClicked() { emit runClicked(m_index); }
void MappingRow::onRemoveClicked() { emit removeClicked(m_index); }
void MappingRow::onEditRowsClicked() { emit editRowsClicked(m_index); }
void MappingRow::onExportRowMapClicked() { emit exportRowMapClicked(m_index); }
void MappingRow::onImportRowMapClicked() { emit importRowMapClicked(m_index); }

EditRowsDialog::EditRowsDialog(QWidget* parent, const QVector<int>& sourceRows,
                               const QVector<int>& destRows, const QString& srcSheet,
                               const QString& srcCol, const QString& destSheet, const QString& destCol)
    : QDialog(parent)
    , m_sourceRows(sourceRows)
    , m_destRows(destRows)
{
    buildUI();
    m_srcSheetEdit->setText(srcSheet);
    m_srcColumnEdit->setText(srcCol);
    m_destSheetEdit->setText(destSheet);
    m_destColumnEdit->setText(destCol);
}

EditRowsDialog::~EditRowsDialog() = default;

void EditRowsDialog::buildUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    QHBoxLayout* form = new QHBoxLayout();
    m_srcSheetEdit = new QLineEdit();
    m_srcColumnEdit = new QLineEdit();
    m_destSheetEdit = new QLineEdit();
    m_destColumnEdit = new QLineEdit();

    form->addWidget(new QLabel("Src Sheet"));
    form->addWidget(m_srcSheetEdit);
    form->addWidget(new QLabel("Src Col"));
    form->addWidget(m_srcColumnEdit);
    form->addWidget(new QLabel("Dest Sheet"));
    form->addWidget(m_destSheetEdit);
    form->addWidget(new QLabel("Dest Col"));
    form->addWidget(m_destColumnEdit);

    layout->addLayout(form);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({"Source Row", "Dest Row"});
    layout->addWidget(m_table);

    QHBoxLayout* actions = new QHBoxLayout();
    QPushButton* addBtn = new QPushButton("Add");
    QPushButton* deleteBtn = new QPushButton("Delete");
    QPushButton* clearBtn = new QPushButton("Clear");
    QPushButton* saveBtn = new QPushButton("Save");
    connect(addBtn, &QPushButton::clicked, this, &EditRowsDialog::onAddRow);
    connect(deleteBtn, &QPushButton::clicked, this, &EditRowsDialog::onDeleteSelected);
    connect(clearBtn, &QPushButton::clicked, this, &EditRowsDialog::onClearAll);
    connect(saveBtn, &QPushButton::clicked, this, &EditRowsDialog::onSave);

    actions->addWidget(addBtn);
    actions->addWidget(deleteBtn);
    actions->addWidget(clearBtn);
    actions->addStretch();
    actions->addWidget(saveBtn);

    layout->addLayout(actions);
}

void EditRowsDialog::onAddRow()
{
    int row = m_table->rowCount();
    m_table->insertRow(row);
    m_table->setItem(row, 0, new QTableWidgetItem(""));
    m_table->setItem(row, 1, new QTableWidgetItem(""));
}

void EditRowsDialog::onDeleteSelected()
{
    const QList<QTableWidgetItem*> selected = m_table->selectedItems();
    QSet<int> rows;
    for (auto* item : selected) {
        rows.insert(item->row());
    }
    QList<int> rowList = rows.values();
    std::sort(rowList.begin(), rowList.end(), std::greater<int>());
    for (int row : rowList) {
        m_table->removeRow(row);
    }
}

void EditRowsDialog::onClearAll()
{
    m_table->setRowCount(0);
}

void EditRowsDialog::onSave()
{
    m_sourceRows.clear();
    m_destRows.clear();
    for (int i = 0; i < m_table->rowCount(); ++i) {
        bool ok1 = false;
        bool ok2 = false;
        int src = m_table->item(i, 0) ? m_table->item(i, 0)->text().toInt(&ok1) : 0;
        int dest = m_table->item(i, 1) ? m_table->item(i, 1)->text().toInt(&ok2) : 0;
        if (ok1 && ok2) {
            m_sourceRows.append(src);
            m_destRows.append(dest);
        }
    }
    accept();
}
