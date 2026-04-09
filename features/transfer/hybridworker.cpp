#include "hybridworker.h"
#include "../../ui/mainwindow.h"
#include <QTimer>
#include <QDebug>

HybridWorker::HybridWorker(MainWindow* mainWindow, QObject* parent)
    : QObject(parent)
    , m_mainWindow(mainWindow)
    , m_stopped(false)
    , m_phase1IsExecuteAll(true)
    , m_currentPhase(Phase::None)
    , m_phase1Success(false)
    , m_phase2Success(false)
{
}

void HybridWorker::execute(const HybridTransferConfig& config)
{
    m_config = config;
    m_stopped = false;
    m_currentPhase = Phase::None;
    m_phase1Success = false;
    m_phase2Success = false;
    m_phase1Summary.clear();
    m_phase2Summary.clear();

    if (config.isEmpty()) {
        emit allFinished(false, "No periods assigned.");
        return;
    }

    // Determine execution order
    m_phase1IsExecuteAll = config.executeAllFirst;

    qInfo() << "[HybridWorker] Starting hybrid transfer:"
            << config.executeAllPeriods.size() << "Execute All periods,"
            << config.executeRTPeriods.size() << "Execute RT periods,"
            << "Order:" << (config.executeAllFirst
                                ? "Execute All first"
                                : "Execute RT first");

    // Start phase 1
    QTimer::singleShot(0, this, &HybridWorker::startPhase1);
}

void HybridWorker::stop()
{
    m_stopped = true;
    qInfo() << "[HybridWorker] Stop requested";
}

void HybridWorker::startPhase1()
{
    if (m_stopped) {
        emit allFinished(false, "Hybrid transfer stopped before phase 1.");
        return;
    }

    m_currentPhase = Phase::Phase1;

    if (m_phase1IsExecuteAll) {
        // Phase 1 = Execute All
        if (m_config.hasExecuteAll()) {
            emit phaseStarted("Execute All");
            emit progressUpdate(10, "Phase 1: Running Execute All...");
            runExecuteAll(m_config.executeAllPeriods);
        } else {
            // Skip to phase 2
            m_phase1Success = true;
            m_phase1Summary = "Skipped (no Execute All periods)";
            onPhase1Finished();
        }
    } else {
        // Phase 1 = Execute RT
        if (m_config.hasExecuteRT()) {
            emit phaseStarted("Execute RT");
            emit progressUpdate(10, "Phase 1: Running Execute RT...");
            runExecuteRT(m_config.executeRTPeriods);
        } else {
            m_phase1Success = true;
            m_phase1Summary = "Skipped (no Execute RT periods)";
            onPhase1Finished();
        }
    }
}

void HybridWorker::onPhase1Finished()
{
    if (m_stopped) {
        emit allFinished(false, "Hybrid transfer stopped after phase 1.");
        return;
    }

    emit phaseFinished(
        m_phase1IsExecuteAll ? "Execute All" : "Execute RT",
        m_phase1Success);
    emit progressUpdate(50, "Phase 1 complete. Starting phase 2...");

    // Brief pause between phases to let UI update
    QTimer::singleShot(500, this, &HybridWorker::startPhase2);
}

void HybridWorker::startPhase2()
{
    if (m_stopped) {
        emit allFinished(false, "Hybrid transfer stopped before phase 2.");
        return;
    }

    m_currentPhase = Phase::Phase2;

    if (m_phase1IsExecuteAll) {
        // Phase 2 = Execute RT
        if (m_config.hasExecuteRT()) {
            emit phaseStarted("Execute RT");
            emit progressUpdate(55, "Phase 2: Running Execute RT...");
            runExecuteRT(m_config.executeRTPeriods);
        } else {
            m_phase2Success = true;
            m_phase2Summary = "Skipped (no Execute RT periods)";
            onPhase2Finished();
        }
    } else {
        // Phase 2 = Execute All
        if (m_config.hasExecuteAll()) {
            emit phaseStarted("Execute All");
            emit progressUpdate(55, "Phase 2: Running Execute All...");
            runExecuteAll(m_config.executeAllPeriods);
        } else {
            m_phase2Success = true;
            m_phase2Summary = "Skipped (no Execute All periods)";
            onPhase2Finished();
        }
    }
}

void HybridWorker::onPhase2Finished()
{
    emit phaseFinished(
        m_phase1IsExecuteAll ? "Execute RT" : "Execute All",
        m_phase2Success);

    finishAll();
}

void HybridWorker::finishAll()
{
    bool overallSuccess = m_phase1Success && m_phase2Success;

    QString summary = QString(
        "Hybrid Transfer Complete\n"
        "Phase 1 (%1): %2\n"
        "Phase 2 (%3): %4")
        .arg(m_phase1IsExecuteAll ? "Execute All" : "Execute RT")
        .arg(m_phase1Summary.isEmpty() ? (m_phase1Success ? "Success" : "Failed") : m_phase1Summary)
        .arg(m_phase1IsExecuteAll ? "Execute RT" : "Execute All")
        .arg(m_phase2Summary.isEmpty() ? (m_phase2Success ? "Success" : "Failed") : m_phase2Summary);

    qInfo().noquote() << "[HybridWorker]" << summary;
    emit progressUpdate(100, "Hybrid transfer complete.");
    emit allFinished(overallSuccess, summary);
}

void HybridWorker::runExecuteAll(
    const QVector<QPair<QString, int>>& periods)
{
    if (!m_mainWindow) {
        m_phase1IsExecuteAll
            ? (m_phase1Success = false)
            : (m_phase2Success = false);
        m_phase1IsExecuteAll ? onPhase1Finished() : onPhase2Finished();
        return;
    }

    // Step 1: Temporarily select only the specified periods
    //         in the year cards (deselect everything else)
    m_mainWindow->clearAllSelections();
    for (const auto& period : periods) {
        m_mainWindow->selectPeriod(period.first, period.second);
    }

    // Step 2: Load mappings for those periods
    m_mainWindow->onLoadSelectedPeriods();

    // Step 3: Connect to transfer finished signal (one-shot)
    QMetaObject::Connection conn;
    conn = connect(m_mainWindow, &MainWindow::transferFinished,
        this, [this, conn](bool success, const QString& summary) {
            disconnect(conn);

            // Use current phase to determine which phase just finished
            if (m_currentPhase == Phase::Phase1) {
                m_phase1Success = success;
                m_phase1Summary = summary;
                qInfo() << "[HybridWorker] Phase 1 (Execute All) finished:" << success;
                onPhase1Finished();
            } else if (m_currentPhase == Phase::Phase2) {
                m_phase2Success = success;
                m_phase2Summary = summary;
                qInfo() << "[HybridWorker] Phase 2 (Execute All) finished:" << success;
                onPhase2Finished();
            }
        });

    // Step 4: Trigger Execute All
    //         Use a short delay so Load finishes first
    QTimer::singleShot(2000, m_mainWindow, [this]() {
        if (!m_stopped) {
            m_mainWindow->onExecuteAll();
        }
    });
}

void HybridWorker::runExecuteRT(
    const QVector<QPair<QString, int>>& periods)
{
    if (!m_mainWindow) {
        m_phase1IsExecuteAll
            ? (m_phase2Success = false)
            : (m_phase1Success = false);
        m_phase1IsExecuteAll ? onPhase2Finished() : onPhase1Finished();
        return;
    }

    // Step 1: Select periods
    m_mainWindow->clearAllSelections();
    for (const auto& period : periods) {
        m_mainWindow->selectPeriod(period.first, period.second);
    }

    // Step 2: Load RT
    m_mainWindow->onLoadRT();

    // Step 3: Connect to RT finished signal (one-shot)
    QMetaObject::Connection conn;
    conn = connect(m_mainWindow, &MainWindow::rollingTransferFinished,
        this, [this, conn](bool success, const QString& summary) {
            disconnect(conn);

            // Use current phase to determine which phase just finished
            if (m_currentPhase == Phase::Phase1) {
                m_phase1Success = success;
                m_phase1Summary = summary;
                qInfo() << "[HybridWorker] Phase 1 (Execute RT) finished:" << success;
                onPhase1Finished();
            } else if (m_currentPhase == Phase::Phase2) {
                m_phase2Success = success;
                m_phase2Summary = summary;
                qInfo() << "[HybridWorker] Phase 2 (Execute RT) finished:" << success;
                onPhase2Finished();
            }
        });

    // Step 4: Trigger Execute RT after Load RT completes
    QTimer::singleShot(2000, m_mainWindow, [this]() {
        if (!m_stopped) {
            m_mainWindow->onRollingTransfer();
        }
    });
}
