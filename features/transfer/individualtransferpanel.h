#ifndef FEATURES_TRANSFER_INDIVIDUALTRANSFERPANEL_H
#define FEATURES_TRANSFER_INDIVIDUALTRANSFERPANEL_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QTextEdit>
#include <QSpinBox>
#include <QTableWidget>
#include <QCheckBox>

class IndividualTransferPanel : public QWidget
{
    Q_OBJECT

public:
    explicit IndividualTransferPanel(QWidget *parent = nullptr);
    ~IndividualTransferPanel() override;

    struct TransferConfig {
        QString sourceFile;
        QString destFile;
        QString sourceSheet;
        QString sourceColumn;
        int sourceRow;
        QString destSheet;
        QString destColumn;
        int destRow;
        int cellCount;
        bool divideBy1000;
    };
    
    struct MappingEntry {
        QString sourceCell;
        QString destCell;
        double value;
        QString sheetName;
    };
    
    TransferConfig getTransferConfig() const;
    void setTransferConfig(const TransferConfig& config);
    void updateSourceSheets(const QStringList& sheets);
    void updateDestSheets(const QStringList& sheets);
    QVector<MappingEntry> getMappingData() const;
    void setMappingData(const QVector<MappingEntry>& data);

signals:
    void browseSourceClicked();
    void browseDestClicked();
    void selectSourceCellClicked();
    void selectDestCellClicked();
    void transferClicked(const TransferConfig& config);
    void resetClicked();

private slots:
    void onBrowseSourceClicked();
    void onBrowseDestClicked();
    void onSelectSourceClicked();
    void onSelectDestClicked();
    void onTransferClicked();
    void onResetClicked();
    void onAddMappingRow();
    void onRemoveSelectedMapping();
    void onClearMappingTable();

private:
    void setupUI();
    void updateTransferButtonState();
    void resetToDefaults();
    void refreshMappingTable();

    QLineEdit* m_sourceFileEdit;
    QLineEdit* m_destFileEdit;
    QPushButton* m_btnSelectSource;
    QPushButton* m_btnSelectDest;
    
    QTableWidget* m_mappingTable;
    QLabel* m_mappingCounter;
    QCheckBox* m_copySheetCheckbox;
    QPushButton* m_transferBtn;

    QVector<MappingEntry> m_mappingData;
    TransferConfig m_currentConfig;
};

#endif // FEATURES_TRANSFER_INDIVIDUALTRANSFERPANEL_H


