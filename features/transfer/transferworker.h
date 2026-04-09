#ifndef TRANSFERWORKER_H
#define TRANSFERWORKER_H

#include <QThread>
#include <QStringList>
#include <QMutex>
#include <QMutexLocker>
#include <QElapsedTimer>
#include <QSet>
#include <algorithm>
#include "../../services/transferservice.h"
#include "../../services/excelhandler.h"
#include "../../core/mappingsmanager.h"
#include "../../core/crashguard.h"

struct TransferMapping {
    MappingEntry entry;
    int          year = 0;
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
        try {
            qDebug() << "=== WORKER START ===";

            ExcelHandler* handler = m_transferService ? m_transferService->handler() : nullptr;
            if (!m_transferService || !handler) {
                m_skipped.append("Transfer service or Excel handler is not initialized");
                emit finished(m_totalCells, m_executed, m_skipped);
                return;
            }

            // --- Phase 1: Pre-load all workbooks ---
            struct LoadTask { QString key; QString path; };
            QVector<LoadTask> tasks;
            QSet<QString> seen;

            auto addTask = [&](const QString& key, const QString& path) {
                if (key.isEmpty() || path.isEmpty() || seen.contains(key))
                    return;
                seen.insert(key);
                if (!handler->isLoaded(key))
                    tasks.append({key, path});
            };

            for (const TransferMapping& tm : m_mappings) {
                const QString month = tm.month.isEmpty() ? tm.entry.month : tm.month;
                if (month.trimmed().isEmpty() || tm.year <= 0) {
                    qWarning() << "TransferWorker: skipping invalid mapping period" << month << tm.year;
                    continue;
                }

                const QString& type = tm.entry.sourceFileType;
                const QString explicitSourcePath = tm.entry.sourcePath.trimmed();
                auto sourcePath = [&](const QString& fallback) -> QString {
                    return explicitSourcePath.isEmpty() ? fallback : explicitSourcePath;
                };

                if (type == "pax") {
                    addTask(QString("%1_%2_pax").arg(month).arg(tm.year),
                            sourcePath(handler->findPaxFile(m_destFolder, month, tm.year)));
                } else if (type == "pax_transfer" || type == "traffic_mott") {
                    addTask(QString("%1_%2_pax").arg(month).arg(tm.year),
                            sourcePath(handler->findPaxFile(m_destFolder, month, tm.year)));
                } else if (type == "staff") {
                    addTask(QString("%1_%2_staff").arg(month).arg(tm.year),
                            sourcePath(handler->findStaffFile(m_destFolder, tm.year)));
                } else if (type == "sap_ytd") {
                    addTask(QString("%1_%2_sap_ytd").arg(month).arg(tm.year),
                            sourcePath(handler->findSapYtdFile(m_destFolder, month, tm.year)));
                    if (!tm.entry.trafficSourceRows.isEmpty()) {
                        addTask(QString("%1_%2_pax").arg(month).arg(tm.year),
                                handler->findPaxFile(m_destFolder, month, tm.year));
                    }
                } else if (type == "sap") {
                    addTask(QString("%1_%2_sap").arg(month).arg(tm.year),
                            sourcePath(handler->findSAPFile(m_destFolder, month, tm.year)));
                } else {
                    addTask(QString("%1_%2_cost").arg(month).arg(tm.year),
                            sourcePath(handler->findCostControlFile(m_destFolder, month, tm.year)));
                }

                addTask(QString("%1_%2_cost_control").arg(month).arg(tm.year),
                        tm.destPath.isEmpty()
                            ? handler->findCostControlFile(m_destFolder, month, tm.year)
                            : tm.destPath);
            }

            QMap<QString, QSet<QString>> keyToSheets;
            for (const TransferMapping& tm : m_mappings) {
                const QString month = tm.month.isEmpty() ? tm.entry.month : tm.month;
                if (month.trimmed().isEmpty() || tm.year <= 0)
                    continue;

                const QString& type = tm.entry.sourceFileType;
                QString srcKey;
                if (type == "pax" || type == "pax_transfer" || type == "traffic_mott") {
                    srcKey = QString("%1_%2_pax").arg(month).arg(tm.year);
                } else if (type == "staff") {
                    srcKey = QString("%1_%2_staff").arg(month).arg(tm.year);
                } else if (type == "sap_ytd") {
                    srcKey = QString("%1_%2_sap_ytd").arg(month).arg(tm.year);
                } else if (type == "sap") {
                    srcKey = QString("%1_%2_sap").arg(month).arg(tm.year);
                } else {
                    srcKey = QString("%1_%2_cost").arg(month).arg(tm.year);
                }

                const QString destKey = QString("%1_%2_cost_control").arg(month).arg(tm.year);
                if (!tm.entry.sourceSheetTemplate.isEmpty())
                    keyToSheets[srcKey].insert(tm.entry.sourceSheetTemplate);
                if (!tm.entry.destSheet.isEmpty())
                    keyToSheets[destKey].insert(tm.entry.destSheet);
            }

            emit progress(0, m_mappings.size(), QString("Pre-loading %1 workbooks...").arg(tasks.size()));
            for (const LoadTask& task : tasks) {
                if (isStopRequested())
                    break;
                QString loadError;
                if (!handler->loadWorkbook(task.path, task.key, keyToSheets.value(task.key), &loadError)) {
                    qWarning() << "TransferWorker: preload failed for" << task.key << task.path
                               << ":" << loadError;
                }
            }

            // --- Phase 1b: Sort mappings in deterministic dependency order ---
            auto typeOrder = [](const QString& type, bool copyFull) -> int {
                if (type == "traffic_mott") return 0;
                if (type == "pax") return 1;
                if (type == "pax_transfer") return 2;
                if (type == "staff") return 3;
                if (type == "sap") return 4;
                if (type == "ytd") return 5;
                if (type == "sap_ytd" && !copyFull) return 6;
                if (type == "sap_ytd" && copyFull) return 7;
                return 5;
            };
            auto monthOrder = [](const QString& month) -> int {
                const QString mm = ExcelHandler::MONTH_TO_NUM.value(month, QString());
                bool ok = false;
                const int idx = mm.toInt(&ok);
                return ok ? idx : 99;
            };
            std::stable_sort(m_mappings.begin(), m_mappings.end(),
                [&typeOrder, &monthOrder](const TransferMapping& a, const TransferMapping& b) {
                    if (a.year != b.year)
                        return a.year < b.year;
                    const QString aMonth = a.month.isEmpty() ? a.entry.month : a.month;
                    const QString bMonth = b.month.isEmpty() ? b.entry.month : b.month;
                    const int aMonthIdx = monthOrder(aMonth);
                    const int bMonthIdx = monthOrder(bMonth);
                    if (aMonthIdx != bMonthIdx)
                        return aMonthIdx < bMonthIdx;
                    return typeOrder(a.entry.sourceFileType, a.entry.copyFullSheet)
                         < typeOrder(b.entry.sourceFileType, b.entry.copyFullSheet);
                });

            // --- Phase 2: Transfer sequentially ---
            const int total = m_mappings.size();
            QMap<QString, QString> destMap; // destKey -> destPath
            for (const TransferMapping& tm : m_mappings) {
                const QString month = tm.month.isEmpty() ? tm.entry.month : tm.month;
                if (month.trimmed().isEmpty() || tm.year <= 0)
                    continue;
                const QString destKey = QString("%1_%2_cost_control").arg(month).arg(tm.year);
                destMap[destKey] = tm.destPath;
            }

            for (int i = 0; i < total; ++i) {
                if (isStopRequested())
                    break;

                while (isPauseRequested()) {
                    QThread::msleep(100);
                    if (isStopRequested())
                        break;
                }
                if (isStopRequested())
                    break;

                const TransferMapping& tm = m_mappings[i];
                const QString month = tm.month.isEmpty() ? tm.entry.month : tm.month;

                emit progress(i + 1, total,
                              QString("Transferring %1/%2: %3 %4")
                                  .arg(i + 1).arg(total).arg(month).arg(tm.year));

                QElapsedTimer timer;
                timer.start();
                qDebug() << "TRANSFER START" << (i + 1) << "/" << total << month << tm.year;
                qDebug() << "--- TRANSFER" << (i + 1) << "/" << total << month << tm.year << "START ---";
                const int cells = doTransfer(tm);
                qDebug() << "TRANSFER END" << (i + 1) << "/" << total << "cells" << cells
                         << "elapsed" << timer.elapsed() << "ms";
                qDebug() << "--- TRANSFER" << (i + 1) << "/" << total << "DONE cells=" << cells << "---";

                if (cells > 0) {
                    m_executed++;
                    m_totalCells += cells;
                }

                const bool success = (cells > 0);
                QString rowMessage = success ? QString("%1 cells").arg(cells) : "Failed";
                if (!success && !m_lastResult.error.isEmpty())
                    rowMessage = m_lastResult.error;
                emit rowDone(tm.rowIndex, success, rowMessage);
            }

            // Save all destination workbooks once at the end via OpenXML.
            qDebug() << "=== ALL TRANSFERS DONE, STARTING SAVES ===";
            qDebug() << "TransferWorker: starting save phase," << destMap.size() << "workbooks";
            int saveIdx = 0;
            for (auto it = destMap.constBegin(); it != destMap.constEnd(); ++it) {
                ++saveIdx;
                qDebug() << "TransferWorker: saving" << saveIdx << "/" << destMap.size()
                         << it.key() << "->" << it.value();
                const bool ok = handler->saveWorkbook(it.key(), it.value());
                qDebug() << "TransferWorker: save" << (ok ? "OK" : "FAILED") << it.key();
                if (!ok)
                    qWarning() << "TransferWorker: save failed for" << it.key() << it.value();
            }
            qDebug() << "TransferWorker: save phase complete";

            qDebug() << "=== WORKER ABOUT TO EMIT FINISHED ===";
            emit finished(m_totalCells, m_executed, m_skipped);
            qDebug() << "=== WORKER EMIT DONE ===";
        } catch (...) {
            const QString error = CrashGuard::format("TransferWorker::run");
            qCritical() << "[CRASH_GUARD]" << error;
            m_skipped.append(error);
            emit finished(m_totalCells, m_executed, m_skipped);
        }
    }

signals:
    void progress(int current, int total, const QString& message);
    void rowDone(int index, bool success, const QString& message);
    void finished(int totalCells, int executed, const QStringList& skipped);

private:
    int doTransfer(const TransferMapping& tm)
    {
        try {
            m_lastResult = TransferService::Result{};

            if (!m_transferService) {
                m_lastResult.error = "Transfer service unavailable";
                m_skipped.append(QString("%1 %2").arg(tm.month).arg(tm.year));
                return 0;
            }

            const QString month = tm.month.isEmpty() ? tm.entry.month : tm.month;
            if (month.trimmed().isEmpty() || tm.year <= 0) {
                m_lastResult.error = "Invalid transfer period";
                m_skipped.append(QString("%1 %2").arg(month).arg(tm.year));
                return 0;
            }

            QString destPath = tm.destPath.isEmpty() ? m_destFolder : tm.destPath;
            if (destPath.trimmed().isEmpty()) {
                m_lastResult.error = "Destination path is empty";
                m_skipped.append(QString("%1 %2").arg(month).arg(tm.year));
                return 0;
            }

            const QString destKey = QString("%1_%2_cost_control").arg(month).arg(tm.year);

            ExcelHandler* handler = m_transferService->handler();
            if (!handler) {
                m_lastResult.error = "Excel handler unavailable";
                m_skipped.append(QString("%1 %2").arg(month).arg(tm.year));
                return 0;
            }

            if (!handler->isLoaded(destKey)) {
                QString loadError;
                if (!handler->loadWorkbook(destPath, destKey, {}, &loadError)) {
                    if (loadError.isEmpty())
                        loadError = "unknown workbook load error";
                    m_lastResult.error = QString("Failed to load destination workbook: %1 (%2)")
                                             .arg(destPath, loadError);
                    qWarning() << "TransferWorker:" << m_lastResult.error;
                    m_skipped.append(QString("%1 %2").arg(month).arg(tm.year));
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
            if (m_lastResult.cellsTransferred == 0)
                m_skipped.append(QString("%1 %2").arg(month).arg(tm.year));
            return m_lastResult.cellsTransferred;
        } catch (...) {
            m_lastResult.error = CrashGuard::format("TransferWorker::doTransfer");
            qWarning() << "[CRASH_GUARD]" << m_lastResult.error;
            m_skipped.append(m_lastResult.error);
            return 0;
        }
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
