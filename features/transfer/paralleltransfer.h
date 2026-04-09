#pragma once
// ═══════════════════════════════════════════════════════════════════
// paralleltransfer.h — Parallel Transfer System
// Self-contained: zero dependencies on UI layer.
// Safe to delete this entire module without affecting existing code.
// ═══════════════════════════════════════════════════════════════════

#include <QObject>
#include <QThread>
#include <QVector>
#include <QString>

// MappingEntry is a value type — include its header
#include "../../core/mappingsmanager.h"

// Forward-declare only what we need — no transitive UI includes
class ExcelHandler;
class TransferService;

// ───────────────────────────────────────────────────────────────────
// MonthTransferRequest — the shared data contract
// Built by the controller, consumed READ-ONLY by both workers.
// Value type (deep copy) — no raw pointers, no shared ownership.
// ───────────────────────────────────────────────────────────────────
struct MonthTransferRequest {
    QString month;                    // "January" .. "December"
    int     year          = 0;        // e.g. 2025
    QString destKey;                  // "November_2025_cost_control"
    QString destFilePath;             // full path to cost_control .xlsm
    QString baseFolder;               // root folder for source file discovery
    QVector<MappingEntry> mappings;   // all mapping entries for this month (value copy)
};

// ───────────────────────────────────────────────────────────────────
// MonthResult — emitted per completed month, consumed by UI log
// ───────────────────────────────────────────────────────────────────
struct MonthResult {
    QString month;
    int     year             = 0;
    int     cellsTransferred = 0;
    bool    success          = false;
    QString errorMessage;
    qint64  elapsedMs        = 0;
};
Q_DECLARE_METATYPE(MonthResult)

// ═══════════════════════════════════════════════════════════════════
// IndividualTransferWorker
// Runs each month INDEPENDENTLY — continues on error.
// Friend implements: paralleltransfer_individual.cpp
// ═══════════════════════════════════════════════════════════════════
class IndividualTransferWorker : public QObject {
    Q_OBJECT
public:
    explicit IndividualTransferWorker(const QVector<MonthTransferRequest>& requests,
                                      QObject* parent = nullptr);
    ~IndividualTransferWorker() override;

public slots:
    void run();
    void cancel();

signals:
    void monthStarted(const QString& month, int year);
    void monthFinished(const MonthResult& result);
    void progressUpdate(int current, int total, const QString& status);
    void allFinished(int totalCells, int completedMonths);

private:
    QVector<MonthTransferRequest> m_requests;
    ExcelHandler*    m_handler  = nullptr;  // OWN — never share across threads
    TransferService* m_service  = nullptr;  // OWN — never share across threads
    bool m_cancelled = false;
};

// ═══════════════════════════════════════════════════════════════════
// RollingTransferWorker
// Runs months SEQUENTIALLY in calendar order.
// Save + reload between months (next month reads previous month's output).
// Aborts on first error — subsequent months would produce wrong data.
// Rovo Dev implements: paralleltransfer_rolling.cpp
// ═══════════════════════════════════════════════════════════════════
class RollingTransferWorker : public QObject {
    Q_OBJECT
public:
    explicit RollingTransferWorker(const QVector<MonthTransferRequest>& requests,
                                   QObject* parent = nullptr);
    ~RollingTransferWorker() override;

    // Validate BEFORE creating thread — call from UI thread for instant feedback.
    // Checks: ≥2 months AND contiguous calendar order (no gaps).
    static bool validateRequests(const QVector<MonthTransferRequest>& requests,
                                 QString* errorOut = nullptr);

public slots:
    void run();
    void cancel();

signals:
    void monthStarted(const QString& month, int year);
    void monthFinished(const MonthResult& result);
    void progressUpdate(int current, int total, const QString& status);
    void allFinished(int totalCells, int completedMonths);
    void rollingStepSaved(const QString& month, int year); // emitted after save+reload

private:
    bool executeMonth(const MonthTransferRequest& req, int stepIdx);
    void saveAndReload(const MonthTransferRequest& req);

    QVector<MonthTransferRequest> m_requests; // sorted by calendar order in ctor
    ExcelHandler*    m_handler  = nullptr;    // OWN — never share across threads
    TransferService* m_service  = nullptr;    // OWN — never share across threads
    bool m_cancelled = false;
};

// ═══════════════════════════════════════════════════════════════════
// ParallelTransferController
// Owned by any QObject (e.g. MainWindow). Manages both threads.
// Pure QObject — zero QWidget dependency. Fully decoupled from UI.
// Rovo Dev implements: paralleltransfer_controller.cpp
// ═══════════════════════════════════════════════════════════════════
class ParallelTransferController : public QObject {
    Q_OBJECT
public:
    explicit ParallelTransferController(QObject* parent = nullptr);
    ~ParallelTransferController() override;

    // Launch — validates before creating thread.
    // Returns false + emits errorOccurred if validation fails.
    bool startIndividual(const QVector<MonthTransferRequest>& requests);
    bool startRolling   (const QVector<MonthTransferRequest>& requests);

    // State
    bool isIndividualRunning() const;
    bool isRollingRunning()    const;
    bool isAnyRunning()        const { return isIndividualRunning() || isRollingRunning(); }

    // Cancel both (waits up to 5s per thread)
    void cancelAll();

signals:
    void individualProgress(const QString& status);
    void rollingProgress   (const QString& status);
    void individualDone    (int totalCells, int completedMonths);
    void rollingDone       (int totalCells, int completedMonths);
    void monthResultReady  (const MonthResult& result, const QString& mode); // "individual"/"rolling"
    void errorOccurred     (const QString& context, const QString& message);

private:
    void cleanupIndividual();
    void cleanupRolling();

    QThread*                  m_individualThread  = nullptr;
    QThread*                  m_rollingThread     = nullptr;
    IndividualTransferWorker* m_individualWorker  = nullptr;
    RollingTransferWorker*    m_rollingWorker     = nullptr;
};
