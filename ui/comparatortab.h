#ifndef COMPARATORTAB_H
#define COMPARATORTAB_H

#include <QWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QFrame>
#include <QSplitter>
#include <QDialog>

class MainWindow;
class MappingModel;
class MappingController;
class ExcelHandler;

struct CompareResult {
    QString month;         // e.g. "January"
    QString mappingType;   // e.g. "sap", "budget_refi"
    QString sheetName;     // e.g. "MZLZ Consolidated"
    int row = 0;
    QString column;        // e.g. "W"
    double workingValue = 0.0;
    double compareValue = 0.0;
    double difference = 0.0;
};

class ComparatorTab : public QWidget
{
    Q_OBJECT

public:
    explicit ComparatorTab(MainWindow* mainWindow, QWidget* parent = nullptr);

private slots:
    void onLoad();
    void onCompare();
    void applyFilters();
    void onUndock();
    void onDock();

private:
    void setupUI();
    void populateResultsTable(const QVector<CompareResult>& mismatches);
    void populateFilterCombos();
    QString buildComparePath(const QString& workingPath) const;
    double toleranceFromPrecision() const;
    double roundToPrecision(double val, int decimals) const;

    MainWindow*        m_mainWindow = nullptr;
    MappingModel*      m_mappingModel = nullptr;
    MappingController* m_mappingController = nullptr;

    // UI widgets — top controls
    QSplitter*     m_splitter = nullptr;
    QComboBox*     m_yearCombo = nullptr;
    QPushButton*   m_loadBtn = nullptr;
    QPushButton*   m_compareBtn = nullptr;
    QLabel*        m_workingFileLabel = nullptr;
    QLabel*        m_compareFileLabel = nullptr;
    QLabel*        m_statusLabel = nullptr;
    QLabel*        m_summaryLabel = nullptr;
    QProgressBar*  m_progressBar = nullptr;

    // Month checkboxes (12)
    QCheckBox*     m_monthChecks[12] = {};

    // Precision selector
    QComboBox*     m_precisionCombo = nullptr;

    // Filter bar
    QComboBox*     m_filterMapping = nullptr;
    QComboBox*     m_filterSheet = nullptr;
    QComboBox*     m_filterColumn = nullptr;
    QComboBox*     m_filterMonth = nullptr;

    // Results table + undock
    QTableWidget*  m_resultsTable = nullptr;
    QPushButton*   m_undockBtn = nullptr;
    QWidget*       m_resultsContainer = nullptr;  // parent widget that holds table + undock btn
    QVBoxLayout*   m_resultsContainerLayout = nullptr;
    QVBoxLayout*   m_mainRightLayout = nullptr;   // layout the container lives in
    QDialog*       m_floatingDialog = nullptr;

    // Sidebar
    QWidget*       m_cardsContainer = nullptr;
    QVBoxLayout*   m_cardsLayout = nullptr;
    QLabel*        m_noMappingsLabel = nullptr;

    // State
    QVector<CompareResult> m_allMismatches;
    // Per-month file paths: month name → {working, compare}
    QMap<QString, QPair<QString, QString>> m_monthFiles;

    static const QStringList s_monthNames;
    static const QMap<QString, QString> s_monthToNum;
};

#endif // COMPARATORTAB_H