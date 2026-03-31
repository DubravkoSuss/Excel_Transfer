#include "rowmapvalidatordialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonArray>
#include <QPushButton>

RowMapValidatorDialog::RowMapValidatorDialog(QWidget* parent)
    : QDialog(parent), m_isValid(false)
{
    setWindowTitle("RowMap Validator");
    setMinimumSize(500, 400);

    QVBoxLayout* layout = new QVBoxLayout(this);

    m_jsonEdit = new QPlainTextEdit(this);
    layout->addWidget(m_jsonEdit);

    m_resultLabel = new QLabel(this);
    layout->addWidget(m_resultLabel);

    QHBoxLayout* buttons = new QHBoxLayout();
    m_btnValidate = new QPushButton("Validate", this);
    m_btnSave = new QPushButton("Save", this);
    buttons->addWidget(m_btnValidate);
    buttons->addWidget(m_btnSave);
    buttons->addStretch();
    layout->addLayout(buttons);

    connect(m_btnValidate, &QPushButton::clicked, this, &RowMapValidatorDialog::onValidate);
    connect(m_btnSave, &QPushButton::clicked, this, &RowMapValidatorDialog::onSave);
}

RowMapValidatorDialog::~RowMapValidatorDialog() = default;

void RowMapValidatorDialog::setJsonText(const QString& jsonText)
{
    m_jsonEdit->setPlainText(jsonText);
}

bool RowMapValidatorDialog::validateJson(const QString& json)
{
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {
        m_resultLabel->setText("Invalid JSON: " + error.errorString());
        m_resultLabel->setStyleSheet("color: red;");
        return false;
    }
    if (!doc.isObject()) {
        m_resultLabel->setText("JSON must be an object");
        m_resultLabel->setStyleSheet("color: red;");
        return false;
    }

    QJsonObject obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        bool ok = false;
        it.key().toInt(&ok);
        if (!ok) {
            m_resultLabel->setText("Key is not an integer: " + it.key());
            m_resultLabel->setStyleSheet("color: red;");
            return false;
        }
        if (!(it.value().isDouble() || it.value().isArray())) {
            m_resultLabel->setText("Value must be int or array: " + it.key());
            m_resultLabel->setStyleSheet("color: red;");
            return false;
        }
        if (it.value().isArray()) {
            for (const QJsonValue& v : it.value().toArray()) {
                if (!v.isDouble()) {
                    m_resultLabel->setText("Array values must be int: " + it.key());
                    m_resultLabel->setStyleSheet("color: red;");
                    return false;
                }
            }
        }
    }

    m_validatedJson = obj;
    m_resultLabel->setText("Valid JSON");
    m_resultLabel->setStyleSheet("color: green;");
    return true;
}

void RowMapValidatorDialog::onValidate()
{
    m_isValid = validateJson(m_jsonEdit->toPlainText());
}

void RowMapValidatorDialog::onSave()
{
    if (!m_isValid) {
        m_isValid = validateJson(m_jsonEdit->toPlainText());
    }
    if (m_isValid) {
        accept();
    }
}
