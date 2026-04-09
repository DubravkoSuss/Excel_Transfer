#include "loadworker.h"
#include <QFile>
#include <QDir>
#include <QtConcurrent>
#include <QFuture>
#include <QMutex>
#include "../../core/crashguard.h"

void LoadWorker::run()
{
    try {
        m_results.clear();
        m_total = m_periods.size();
        m_progress = 0;

        if (!m_handler) {
            emit finished(m_results);
            return;
        }

        struct LoadTask {
            QString key;
            QString filePath;
            QString month;
            int year = 0;
        };

        QVector<LoadTask> tasks;
        QSet<QString> seenPaths; // avoid loading same file twice (src==dest folder case)

        for (const auto& period : m_periods) {
            const QString& month = period.first;
            int year = period.second;
            if (month.trimmed().isEmpty() || year <= 0)
                continue;

            QString srcPath  = m_handler->findCostControlFile(m_sourceFolder, month, year);
            QString destPath = m_handler->findCostControlFile(m_destFolder, month, year);

            // Destination key (_cost_control) is what isLoaded() checks.
            QString destKey = QString("%1_%2_cost_control").arg(month).arg(year);
            if (!destPath.isEmpty() && !seenPaths.contains(destPath)) {
                seenPaths.insert(destPath);
                tasks.append({destKey, destPath, month, year});
            }

            // Source key only needed if it's a different file from dest.
            if (!srcPath.isEmpty() && srcPath != destPath && !seenPaths.contains(srcPath)) {
                QString srcKey = QString("%1_%2_cost").arg(month).arg(year);
                seenPaths.insert(srcPath);
                tasks.append({srcKey, srcPath, month, year});
            }
        }

        int loaded = 0;
        const int totalTasks = tasks.size();
        QMutex progressMutex;

        QThreadPool pool;
        pool.setMaxThreadCount(2);

        const int batchSize = 3;
        for (int i = 0; i < tasks.size(); i += batchSize) {
            if (m_stopped)
                break;

            QVector<QFuture<QPair<bool, LoadTask>>> futures;
            const int end = qMin(i + batchSize, tasks.size());

            for (int j = i; j < end; ++j) {
                if (m_stopped)
                    break;
                const LoadTask task = tasks[j];
                futures.append(QtConcurrent::run(&pool, [this, task]() -> QPair<bool, LoadTask> {
                    bool ok = m_handler->loadWorkbook(task.filePath, task.key);
                    return {ok, task};
                }));
            }

            for (auto& future : futures) {
                auto [ok, task] = future.result();
                QMutexLocker lock(&progressMutex);
                ++loaded;
                emit progress(loaded, totalTasks,
                              QString("%1 %2 %3").arg(ok ? "Loaded" : "Failed").arg(task.month).arg(task.year));
            }
        }

        for (const auto& period : m_periods) {
            LoadResult result;
            result.month = period.first;
            result.year = period.second;
            result.destPath = m_handler->findCostControlFile(m_destFolder, period.first, period.second);
            result.success = m_handler->isLoaded(QString("%1_%2_cost_control").arg(period.first).arg(period.second));
            m_results.append(result);
        }
    } catch (...) {
        LoadResult fail;
        fail.success = false;
        fail.error = CrashGuard::format("LoadWorker::run");
        qCritical() << "[CRASH_GUARD]" << fail.error;
        m_results.append(fail);
    }

    emit finished(m_results);
}
