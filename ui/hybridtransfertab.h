#ifndef HYBRIDTRANSFERTAB_H
#define HYBRIDTRANSFERTAB_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QGroupBox>
#include <QRadioButton>
#include <QProgressBar>
#include <QFrame>
#include <QHeaderView>
#include <QMap>
#include <QVector>
#include <QSplitter>
#include <QScrollArea>
#include "../features/transfer/hybridtransferconfig.h"
#include "../core/mappingcontroller.h"
#include "../core/mappingmodel.h"

class MainWindow;

class HybridTransferTab : public QWidget
{
    Q_OBJECT
public:
    explicit HybridTransferTab(MainWindow* mainWindow, QWidget* parent = nullptr);

signals:
    void executeRequested(const HybridTransferConfig& config);

public slots:
    void onPhaseStarted(const QString& phaseName);
    void onPhaseFinished(const QString& phaseName, bool success);
    void onProgressUpdate(int percent, const QString& message);
    void onAllFinished(bool success, const QString& summary);

private slots:
    void onAddPeriod();
    void onRemovePeriod();
    void onClearAll();
    void onResetAll();
    void onExecute();
    void selectQuarter(int quarter);
    void selectAllMonths();
    void clearAllMonths();
    void onMoveUp();
    void onMoveDown();

private:
    void populateTable();
    void updateSummary();
    void updateExecuteButton();
    void updateMoveButtons();
    void setupMappingsSidebar();

    // Returns the contiguous block of RT-row indices that the given row belongs to.
    // For Execute All rows, returns just { rowIndex }.
    QVector<int> rtBlockFor(int rowIndex) const;

    MainWindow* m_mainWindow;

    // Main layout with splitter
    QSplitter* m_splitter;  // Left sidebar / right content

    // Mappings sidebar components
    QFrame* m_mappingsSidebar;
    QWidget* m_mappingsContainer;
    QVBoxLayout* m_mappingsLayout;
    QLabel* m_noMappingsLabel;
    MappingController* m_mappingController;
    MappingModel* m_mappingModel;

    // Year/Month selection
    QComboBox* m_yearCombo;
    QComboBox* m_transferTypeCombo;  // "Execute All" or "Execute RT"
    QVector<QCheckBox*> m_monthCheckboxes;  // Graphical month selection
    QPushButton* m_addBtn;
    QPushButton* m_removeBtn;
    QPushButton* m_clearBtn;
    QPushButton* m_moveUpBtn;
    QPushButton* m_moveDownBtn;

    // Table showing assigned periods
    QTableWidget* m_table;

    // Execution order
    QRadioButton* m_executeAllFirstRadio;
    QRadioButton* m_executeRTFirstRadio;

    // Status
    QLabel* m_summaryLabel;
    QLabel* m_phaseStatusLabel;  // Shows current phase execution
    QProgressBar* m_progressBar;
    QPushButton* m_executeBtn;

    // Internal config built from UI
    HybridTransferConfig m_config;

    // Track assignments: key = "month_year", value = "execute_all" or "execute_rt"
    QMap<QString, QString> m_assignments;

    // Ordered list of keys — defines execution order shown in table
    // (QMap sorts alphabetically; this vector preserves user intent)
    QVector<QString> m_orderedKeys;
};

#endif // HYBRIDTRANSFERTAB_H