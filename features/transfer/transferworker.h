#ifndef TRANSFERWORKER_H
#define TRANSFERWORKER_H

#include <QThread>
#include <QStringList>
#include <QMutex>
#include <QMutexLocker>
#include <QElapsedTimer>
#include <QSet>
#include "../../services/transferservice.h"
#include "../../services/excelhandler.h"
#include "../../core/mappingsmanager.h"

struct TransferMapping {
    MappingEntry entry;
    int          year  = 0;
    QString      month;
    QString      destPath;
    int          rowIndex = -1; // index in UI list
};

class TransferWorker : public QThread
{
    Q_OBJECT

public:
    explicit TransferWorker(QObject* parent = nullptr)
        : QThread(parent), m_paused(false), m_stopped(false), m_totalCells(0), m_executed(0) {}

    void setMappings(const QVector<TransferMapping>& mappings, TransferService* service, const QString& destFolder)
    {
        m_mappings = mappings;
        m_transferService = service;
        m_destFolder = destFolder;
    }

    // Graceful control requests: applied only after the current mapping/sheet finishes.
    void pause() {
        QMutexLocker locker(&m_stateMutex);
        m_paused = true;
    }
    void resume() {
        QMutexLocker locker(&m_stateMutex);
        m_paused = false;
    }
    void stop() {
        QMutexLocker locker(&m_stateMutex);
        m_stopped = true;
        m_paused = false;
    }

    bool isPauseRequested() const {
        QMutexLocker locker(&m_stateMutex);
        return m_paused;
    }

    bool isStopRequested() const {
        QMutexLocker locker(&m_stateMutex);
        return m_stopped;
    }

    void run() override
    {
        qDebug() << "=== WORKER START ===";
        // --- Phase 1: Pre-load all workbooks in parallel ---
        ExcelHandler* handler = m_transferService ? m_transferService->handler() : nullptr;
        if (handler) {
            struct LoadTask { QString key; QString path; };
            QVector<LoadTask> tasks;
            QSet<QString> seen;

            auto addTask = [&](const QString& key, const QString& path) {
                if (!path.isEmpty() && !seen.contains(key)) {
                    seen.insert(key);
                    if (!handler->isLoaded(key))
                        tasks.append({key, path});
                }
            };

            for (const TransferMapping& tm : m_mappings) {
                const QString& month = tm.entry.month;
                int year = tm.year;
                const QString& type = tm.entry.sourceFileType;

                // Source file — must match the key used in transferEntry()/handleSapYtd()
                if (type == "pax") {
                    addTask(QString("%1_%2_pax").arg(month).arg(year),
                            handler->findPaxFile(m_destFolder, month, year));
                } else if (type == "pax_transfer" || type == "traffic_mott") {
                    // These read from the PAX file (Sheet1) into cost_control
                    addTask(QString("%1_%2_pax").arg(month).arg(year),
                            handler->findPaxFile(m_destFolder, month, year));
                } else if (type == "staff") {
                    addTask(QString("%1_%2_staff").arg(month).arg(year),
                            handler->findStaffFile(m_destFolder, year));
                } else if (type == "sap_ytd") {
                    addTask(QString("%1_%2_sap_ytd").arg(month).arg(year),
                            handler->findSapYtdFile(m_destFolder, month, year));
                    // sap_ytd entries may also read from the PAX/traffic file — pre-load it too
                    if (!tm.entry.trafficSourceRows.isEmpty()) {
                        addTask(QString("%1_%2_pax").arg(month).arg(year),
                                handler->findPaxFile(m_destFolder, month, year));
                    }
                } else if (type == "sap") {
                    addTask(QString("%1_%2_sap").arg(month).arg(year),
                            handler->findSAPFile(m_destFolder, month, year));
                } else {
                    // cost_control or other — source is cost_control file
                    addTask(QString("%1_%2_cost").arg(month).arg(year),
                            handler->findCostControlFile(m_destFolder, month, year));
                }

                // Destination (cost control) file
                addTask(QString("%1_%2_cost_control").arg(month).arg(year),
                        tm.destPath.isEmpty()
                            ? handler->findCostControlFile(m_destFolder, month, year)
                            : tm.destPath);
            }

            // Build needed sheets per file key — keys must match those used in transferEntry()
            QMap<QString, QSet<QString>> keyToSheets;
            for (const TransferMapping& tm : m_mappings) {
                const QString& month = tm.entry.month;
                const int year = tm.year;
                const QString& type = tm.entry.sourceFileType;

                // Resolve the actual cache key for the source file (mirrors Phase 1 addTask logic)
                QString srcKey;
                if (type == "pax" || type == "pax_transfer" || type == "traffic_mott") {
                    srcKey = QString("%1_%2_pax").arg(month).arg(year);
                } else if (type == "staff") {
                    srcKey = QString("%1_%2_staff").arg(month).arg(year);
                } else if (type == "sap_ytd") {
                    srcKey = QString("%1_%2_sap_ytd").arg(month).arg(year);
                } else if (type == "sap") {
                    srcKey = QString("%1_%2_sap").arg(month).arg(year);
                } else {
                    srcKey = QString("%1_%2_cost").arg(month).arg(year);
                }

                QString destKey = QString("%1_%2_cost_control").arg(month).arg(year);
                if (!tm.entry.sourceSheetTemplate.isEmpty())
                    keyToSheets[srcKey].insert(tm.entry.sourceSheetTemplate);
                if (!tm.entry.destSheet.isEmpty())
                    keyToSheets[destKey].insert(tm.entry.destSheet);
            }

            // Load sequentially — parallel loading of large xlsm files causes OOM crashes
            emit progress(0, m_mappings.size(), QString("Pre-loading %1 workbooks...").arg(tasks.size()));
            for (const LoadTask& task : tasks) {
                if (isStopRequested()) break;
                handler->loadWorkbook(task.path, task.key, keyToSheets.value(task.key));
            }
        }

        // --- Phase 1b: Sort mappings so writes happen in correct dependency order ---
        // Order: traffic_mott → pax → pax_transfer → staff → sap → cost_control → ytd → sap_ytd (last)
        // copyFullSheet (SAP monthly sheet) must be absolute last within sap_ytd group
        auto typeOrder = [](const QString& type, bool copyFull) -> int {
            if (type == "traffic_mott")  return 0;
            if (type == "pax")           return 1;
            if (type == "pax_transfer")  return 2;
            if (type == "staff")         return 3;
            if (type == "sap")           return 4;
            if (type == "ytd")           return 5;
            if (type == "sap_ytd" && !copyFull) return 6;
            if (type == "sap_ytd" && copyFull)  return 7; // SAP monthly sheet copy is very last
            return 5; // cost_control and others in the middle
        };
        std::stable_sort(m_mappings.begin(), m_mappings.end(),
            [&typeOrder](const TransferMapping& a, const TransferMapping& b) {
                // First sort by month/year (keep same-period entries grouped)
                if (a.entry.month != b.entry.month) return false;
                if (a.year != b.year) return false;
                // Within same period: sort by type dependency order
                return typeOrder(a.entry.sourceFileType, a.entry.copyFullSheet)
                     < typeOrder(b.entry.sourceFileType, b.entry.copyFullSheet);
            });

        // --- Phase 2: Transfer sequentially (writes must be serial per file) ---
        int total = m_mappings.size();

        // Track destination workbooks to save once at the end
        QMap<QString, QString> destMap; // destKey -> destPath
        QSet<QString> comSaved;         // destKey saved by COM
        for (const TransferMapping& tm : m_mappings) {
            const QString month = tm.month.isEmpty() ? tm.entry.month : tm.month;
            QString destKey = QString("%1_%2_cost_control").arg(month).arg(tm.year);
            destMap[destKey] = tm.destPath;
        }

        for (int i = 0; i < total; ++i) {
            if (isStopRequested()) break;

            // Apply pause only at a safe boundary: after the previous mapping/sheet finished
            // and before starting the next one.
            while (isPauseRequested()) {
                QThread::msleep(100);
                if (isStopRequested()) break;
            }

            if (isStopRequested()) break;

            const TransferMapping& tm = m_mappings[i];

            emit progress(i + 1, total,
                          QString("Transferring %1/%2: %3 %4")
                              .arg(i + 1).arg(total).arg(tm.month).arg(tm.year));

            QElapsedTimer timer;
            timer.start();
            qDebug() << "TRANSFER START" << (i + 1) << "/" << total << tm.month << tm.year;
            qDebug() << "--- TRANSFER" << (i+1) << "/" << total << tm.month << tm.year << "START ---";
            int cells = doTransfer(tm);
            qDebug() << "TRANSFER END" << (i + 1) << "/" << total << "cells" << cells
                     << "elapsed" << timer.elapsed() << "ms";
            qDebug() << "--- TRANSFER" << (i+1) << "/" << total << "DONE cells=" << cells << "---";

            if (cells > 0) {
                m_executed++;
                m_totalCells += cells;
            }

            emit rowDone(tm.rowIndex, cells > 0, cells > 0 ? QString("%1 cells").arg(cells) : "Failed");
        }

        // Save all destination workbooks once at the end via OpenXML (no COM)
        qDebug() << "=== ALL TRANSFERS DONE, STARTING SAVES ===";
        qDebug() << "TransferWorker: starting save phase," << destMap.size() << "workbooks";
        if (m_transferService) {
            ExcelHandler* handler = m_transferService->handler();
            int saveIdx = 0;
            for (auto it = destMap.constBegin(); it != destMap.constEnd(); ++it) {
                ++saveIdx;
                qDebug() << "TransferWorker: saving" << saveIdx << "/" << destMap.size()
                         << it.key() << "->" << it.value();
                bool ok = handler->saveWorkbook(it.key(), it.value());
                qDebug() << "TransferWorker: save" << (ok ? "OK" : "FAILED") << it.key();
                if (!ok) {
                    qWarning() << "TransferWorker: save failed for" << it.key() << it.value();
                }
            }
        }
        qDebug() << "TransferWorker: save phase complete";

        qDebug() << "=== WORKER ABOUT TO EMIT FINISHED ===";
        emit finished(m_totalCells, m_executed, m_skipped);
        qDebug() << "=== WORKER EMIT DONE ===";
    }

signals:
    void progress(int current, int total, const QString& message);
    void rowDone(int index, bool success, const QString& message);
    void finished(int totalCells, int executed, const QStringList& skipped);

private:
    int doTransfer(const TransferMapping& tm)
    {
        if (!m_transferService) {
            m_skipped.append(QString("%1 %2").arg(tm.month).arg(tm.year));
            return 0;
        }

        QString destPath = tm.destPath.isEmpty() ? m_destFolder : tm.destPath;
        // Use tm.month (from MappingItem) not tm.entry.month (can be empty for budget/refi entries)
        const QString month = tm.month.isEmpty() ? tm.entry.month : tm.month;
        // All transfers write into cost_control workbooks (pax files are only sources)
        QString destKey = QString("%1_%2_cost_control").arg(month).arg(tm.year);

        // Load the destination workbook if not already loaded
        ExcelHandler* handler = m_transferService->handler();
        if (handler && !handler->isLoaded(destKey)) {
            if (!handler->loadWorkbook(destPath, destKey)) {
                qWarning() << "TransferWorker: failed to load destination workbook:" << destPath;
                m_skipped.append(QString("%1 %2").arg(tm.month).arg(tm.year));
                return 0;
            }
        }

        m_lastResult = m_transferService->transferEntry(
            tm.entry,
            tm.year,
            destKey,
            destPath,
            m_destFolder
        );
        if (m_lastResult.cellsTransferred == 0) {
            m_skipped.append(QString("%1 %2").arg(tm.month).arg(tm.year));
        }
        return m_lastResult.cellsTransferred;
    }

    QVector<TransferMapping> m_mappings;
    TransferService* m_transferService = nullptr;
    QString m_destFolder;
    mutable QMutex m_stateMutex;
    bool m_paused;
    bool m_stopped;
    int m_totalCells;
    int m_executed;
    QStringList m_skipped;
    TransferService::Result m_lastResult;
};

#endif // TRANSFERWORKER_H
