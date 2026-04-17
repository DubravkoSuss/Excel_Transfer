#ifndef YEARROLLOVERTAB_H
#define YEARROLLOVERTAB_H

#include <QWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QVBoxLayout>

class MainWindow;
class RolloverWorker;

class YearRolloverTab : public QWidget
{
    Q_OBJECT

public:
    explicit YearRolloverTab(MainWindow* mainWindow, QWidget* parent = nullptr);

private slots:
    void onBrowseSource();
    void onBrowseDest();
    void onExecute();
    void onWorkerProgress(int current, int total, const QString& message);
    void onWorkerFinished(bool success, const QString& message);

private:
    void setupUI();

    MainWindow*     m_mainWindow = nullptr;
    RolloverWorker* m_worker     = nullptr;

    QLineEdit*    m_sourceFileEdit = nullptr;
    QPushButton*  m_sourceBtn      = nullptr;

    QLineEdit*    m_destFileEdit   = nullptr;
    QPushButton*  m_destBtn        = nullptr;

    QSpinBox*     m_yearPicker     = nullptr;

    QSpinBox*     m_startRowPicker = nullptr;
    QSpinBox*     m_endRowPicker   = nullptr;

    QPushButton*  m_executeBtn     = nullptr;
    QProgressBar* m_progressBar    = nullptr;
    QLabel*       m_statusLabel    = nullptr;
};

#endif // YEARROLLOVERTAB_H
