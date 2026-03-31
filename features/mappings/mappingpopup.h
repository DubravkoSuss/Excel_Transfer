#ifndef FEATURES_MAPPINGS_MAPPINGPOPUP_H
#define FEATURES_MAPPINGS_MAPPINGPOPUP_H

// Include necessary Qt headers at the beginning of mappingpopup.h
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QTextEdit>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QListWidget>
#include <QListWidgetItem>

class MappingPopup : public QDialog
{
    Q_OBJECT

public:
    explicit MappingPopup(QWidget *parent = nullptr);
    ~MappingPopup() override;

    struct MappingInfo {
        QString name;
        QString sourceType;
        QString description;
        QString sourceSheet;
        QString sourceColumn;
        QString destSheet;
        QString destColumn;
    };
    void setMapping(const MappingInfo& mapping);
    MappingInfo getMapping() const;

signals:
    void mappingSaved(const MappingInfo& mapping);

private slots:
    void onSaveClicked();
    void onCancelClicked();
    void onSourceTypeChanged(int index);

private:
    void setupUI();
    void populateSourceTypeCombo();

    QLineEdit* m_nameEdit;
    QComboBox* m_sourceTypeCombo;
    QTextEdit* m_descriptionEdit;
    QLineEdit* m_sourceSheetEdit;
    QLineEdit* m_sourceColumnEdit;
    QLineEdit* m_destSheetEdit;
    QLineEdit* m_destColumnEdit;

    QPushButton* m_saveBtn;
    QPushButton* m_cancelBtn;

    MappingInfo m_currentMapping;
};

#endif // FEATURES_MAPPINGS_MAPPINGPOPUP_H

