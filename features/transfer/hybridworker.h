#ifndef HYBRIDWORKER_H
#define HYBRIDWORKER_H

#include <QObject>
#include "hybridtransferconfig.h"

class MainWindow;

// HybridWorker is NOT a QThread. It's a sequencer that calls
// existing MainWindow methods in order and chains their completion.
class HybridWorker : public QObject
{
    Q_OBJECT
public:
    explicit HybridWorker(MainWindow* mainWindow, QObject* parent = nullptr);
    
    void execute(const HybridTransferConfig& config);
    void stop();

signals:
    void phaseStarted(const QString& phaseName);
    void phaseFinished(const QString& phaseName, bool success);
    void allFinished(bool success, const QString& summary);
    void progressUpdate(int percent, const QString& message);

private slots:
    void onPhase1Finished();
    void onPhase2Finished();

private:
    void startPhase1();
    void startPhase2();
    void finishAll();
    
    void runExecuteAll(const QVector<QPair<QString, int>>& periods);
    void runExecuteRT(const QVector<QPair<QString, int>>& periods);
    
    MainWindow* m_mainWindow;
    HybridTransferConfig m_config;
    bool m_stopped;
    bool m_phase1IsExecuteAll;
    
    // Track which phase is currently running
    enum class Phase { None, Phase1, Phase2 };
    Phase m_currentPhase;
    
    // Track results
    bool m_phase1Success;
    bool m_phase2Success;
    QString m_phase1Summary;
    QString m_phase2Summary;
};

#endif // HYBRIDWORKER_H
