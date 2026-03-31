#ifndef ROWMAPVALIDATORDIALOG_H
#define ROWMAPVALIDATORDIALOG_H

#include <QDialog>
#include <QJsonObject>
#include <QPlainTextEdit>
#include <QLabel>
#include <QPushButton>

class RowMapValidatorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RowMapValidatorDialog(QWidget* parent = nullptr);
    ~RowMapValidatorDialog();

    void setJsonText(const QString& jsonText);
    QJsonObject getValidatedJson() const { return m_validatedJson; }
    bool isValid() const { return m_isValid; }

private slots:
    void onValidate();
    void onSave();

private:
    bool validateJson(const QString& json);

    QPlainTextEdit* m_jsonEdit;
    QLabel* m_resultLabel;
    QPushButton* m_btnValidate;
    QPushButton* m_btnSave;
    QJsonObject m_validatedJson;
    bool m_isValid;
};

#endif // ROWMAPVALIDATORDIALOG_H
