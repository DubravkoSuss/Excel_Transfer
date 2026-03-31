// Include necessary Qt headers at the beginning of mappingpopup.cpp
#include "mappingpopup.h"
#include "../../core/mappingsmanager.h"
#include <QDebug>

MappingPopup::MappingPopup(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    populateSourceTypeCombo();
}

MappingPopup::~MappingPopup()
{
}

void MappingPopup::setupUI()
{
    setWindowTitle("Mapping Details");
    setMinimumSize(500, 400);
    setModal(true);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);

    // Title
    QLabel* titleLabel = new QLabel("Mapping Configuration");
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

    m_nameEdit = new QLineEdit();
    m_nameEdit->setPlaceholderText("Enter mapping name");
    m_nameEdit->setStyleSheet("padding: 8px;");
    formLayout->addRow("Name:", m_nameEdit);

    m_sourceTypeCombo = new QComboBox();
    m_sourceTypeCombo->setStyleSheet("padding: 8px;");
    connect(m_sourceTypeCombo, &QComboBox::currentIndexChanged, this, &MappingPopup::onSourceTypeChanged);
    formLayout->addRow("Source Type:", m_sourceTypeCombo);

    m_descriptionEdit = new QTextEdit();
    m_descriptionEdit->setPlaceholderText("Enter description");
    m_descriptionEdit->setMaximumHeight(80);
    m_descriptionEdit->setStyleSheet("padding: 8px;");
    formLayout->addRow("Description:", m_descriptionEdit);

    // Source Configuration
    QGroupBox* sourceGroup = new QGroupBox("Source Configuration");
    sourceGroup->setStyleSheet(
        "QGroupBox {"
        "    font-weight: 600;"
        "    margin-top: 10px;"
        "}"
    );

    QFormLayout* sourceLayout = new QFormLayout(sourceGroup);
    sourceLayout->setLabelAlignment(Qt::AlignRight);

    m_sourceSheetEdit = new QLineEdit();
    m_sourceSheetEdit->setPlaceholderText("Source sheet name");
    m_sourceSheetEdit->setStyleSheet("padding: 8px;");
    sourceLayout->addRow("Sheet:", m_sourceSheetEdit);

    m_sourceColumnEdit = new QLineEdit();
    m_sourceColumnEdit->setPlaceholderText("Source column");
    m_sourceColumnEdit->setStyleSheet("padding: 8px;");
    sourceLayout->addRow("Column:", m_sourceColumnEdit);

    // Destination Configuration
    QGroupBox* destGroup = new QGroupBox("Destination Configuration");
    destGroup->setStyleSheet(
        "QGroupBox {"
        "    font-weight: 600;"
        "    margin-top: 10px;"
        "}"
    );

    QFormLayout* destLayout = new QFormLayout(destGroup);
    destLayout->setLabelAlignment(Qt::AlignRight);

    m_destSheetEdit = new QLineEdit();
    m_destSheetEdit->setPlaceholderText("Destination sheet name");
    m_destSheetEdit->setStyleSheet("padding: 8px;");
    destLayout->addRow("Sheet:", m_destSheetEdit);

    m_destColumnEdit = new QLineEdit();
    m_destColumnEdit->setPlaceholderText("Destination column");
    m_destColumnEdit->setStyleSheet("padding: 8px;");
    destLayout->addRow("Column:", m_destColumnEdit);

    mainLayout->addLayout(formLayout);
    mainLayout->addWidget(sourceGroup);
    mainLayout->addWidget(destGroup);

    // Button Box
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    m_saveBtn = new QPushButton("Save Mapping");
    m_saveBtn->setStyleSheet(
        "background-color: #059669;"
        "color: white;"
        "font-weight: 600;"
        "padding: 10px 20px;"
        "border-radius: 6px;"
    );
    connect(m_saveBtn, &QPushButton::clicked, this, &MappingPopup::onSaveClicked);
    buttonLayout->addWidget(m_saveBtn);

    m_cancelBtn = new QPushButton("Cancel");
    m_cancelBtn->setStyleSheet(
        "background-color: #F3F4F6;"
        "color: #374151;"
        "padding: 10px 20px;"
        "border-radius: 6px;"
    );
    connect(m_cancelBtn, &QPushButton::clicked, this, &MappingPopup::onCancelClicked);
    buttonLayout->addWidget(m_cancelBtn);

    mainLayout->addLayout(buttonLayout);

    // Set dialog style
    setStyleSheet(
        "MappingPopup {"
        "    background-color: white;"
        "    border-radius: 8px;"
        "}"
    );
}

void MappingPopup::populateSourceTypeCombo()
{
    m_sourceTypeCombo->addItem("Cost Control", "cost_control");
    m_sourceTypeCombo->addItem("SAP", "sap");
    m_sourceTypeCombo->addItem("PAX", "pax");
    m_sourceTypeCombo->addItem("Staff", "staff");
}

void MappingPopup::setMapping(const MappingInfo& mapping)
{
    m_currentMapping = mapping;
    m_nameEdit->setText(mapping.name);
    m_descriptionEdit->setPlainText(mapping.description);
    m_sourceSheetEdit->setText(mapping.sourceSheet);
    m_sourceColumnEdit->setText(mapping.sourceColumn);
    m_destSheetEdit->setText(mapping.destSheet);
    m_destColumnEdit->setText(mapping.destColumn);

    int index = m_sourceTypeCombo->findData(mapping.sourceType);
    if (index != -1) {
        m_sourceTypeCombo->setCurrentIndex(index);
    }
}

MappingPopup::MappingInfo MappingPopup::getMapping() const
{
    return m_currentMapping;
}

void MappingPopup::onSaveClicked()
{
    m_currentMapping.name = m_nameEdit->text();
    m_currentMapping.sourceType = m_sourceTypeCombo->currentData().toString();
    m_currentMapping.description = m_descriptionEdit->toPlainText();
    m_currentMapping.sourceSheet = m_sourceSheetEdit->text();
    m_currentMapping.sourceColumn = m_sourceColumnEdit->text();
    m_currentMapping.destSheet = m_destSheetEdit->text();
    m_currentMapping.destColumn = m_destColumnEdit->text();

    emit mappingSaved(m_currentMapping);
    accept();
}

void MappingPopup::onCancelClicked()
{
    reject();
}

void MappingPopup::onSourceTypeChanged(int index)
{
    Q_UNUSED(index);

    // Optional: Add logic to set default values based on source type
    QString sourceType = m_sourceTypeCombo->currentData().toString();
    if (sourceType == "sap") {
        m_sourceSheetEdit->setText("SAP Data");
    } else if (sourceType == "pax") {
        m_sourceSheetEdit->setText("PAX Data");
    } else if (sourceType == "staff") {
        m_sourceSheetEdit->setText("Staff Data");
    }
}
