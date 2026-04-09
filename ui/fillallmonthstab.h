#ifndef FILLALLMONTHSTAB_H
#define FILLALLMONTHSTAB_H

#include <QWidget>
#include <QVector>
#include <QComboBox>
#include <QPushButton>
#include <QTableWidget>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include "../features/transfer/fillallmodels.h"
#include "../core/mappingcontroller.h"
#include "../core/mappingmodel.h"

class MainWindow;

class FillAllMonthsTab : public QWidget
{
    Q_OBJECT

public:
    void setExecuteEnabled(bool enabled) { if (m_executeBtn) m_executeBtn->setEnabled(enabled); }
    explicit FillAllMonthsTab(MainWindow* mainWindow, QWidget* parent = nullptr);

    // Expose scan result if MainWindow needs it for execute
    const FillAllScanResult& scanResult() const { return m_scanResult; }

signals:
    void executeRequested(const FillAllScanResult& result);

private slots:
    void onScan();
    void onExecute();

private:
    void populateTable();

    MainWindow*       m_mainWindow = nullptr;
    QComboBox*        m_yearCombo = nullptr;
    QComboBox*        m_monthCombo = nullptr;
    QPushButton*      m_scanBtn = nullptr;
    QPushButton*      m_executeBtn = nullptr;
    QTableWidget*     m_table = nullptr;
    QLabel*           m_statusLabel = nullptr;
    FillAllScanResult m_scanResult;


    // Mapping cards (same as Execute All)
    MappingModel*      m_mappingModel      = nullptr;
    MappingController* m_mappingController = nullptr;
    QVBoxLayout*       m_cardsLayout       = nullptr;
    QWidget*           m_cardsContainer    = nullptr;
};

#endif // FILLALLMONTHSTAB_H
