#ifndef FEATURES_TRANSFER_LOADWORKER_H
#define FEATURES_TRANSFER_LOADWORKER_H

#include <QThread>
#include <QString>
#include <QVector>
#include "../../core/mappingsmanager.h"
#include "../../services/excelhandler.h"

struct LoadResult {
    QString month;
    int year = 0;
    QString destPath;
    bool success = false;
    QString error;
};

class LoadWorker : public QThread
{
    Q_OBJECT

public:
    explicit LoadWorker(QObject* parent = nullptr)
        : QThread(parent), m_progress(0), m_total(0), m_success(false) {}

    void setPeriods(const QVector<QPair<QString, int>>& periods, const QString& sourceFolder,
                    const QString& destFolder, MappingsManager* manager, ExcelHandler* handler = nullptr)
    {
        m_periods = periods;
        m_sourceFolder = sourceFolder;
        m_destFolder = destFolder;
        m_manager = manager;
        m_handler = handler;
    }

    void stop() { m_stopped = true; }
    void run() override;

signals:
    void progress(int current, int total, const QString& message);
    void finished(const QVector<LoadResult>& results);

private:
    QVector<QPair<QString, int>> m_periods;
    QString m_sourceFolder;
    QString m_destFolder;
    MappingsManager* m_manager = nullptr;
    ExcelHandler*    m_handler = nullptr;
    int m_progress = 0;
    int m_total = 0;
    bool m_success = false;
    bool m_stopped = false;
    QVector<LoadResult> m_results;
};

#endif // FEATURES_TRANSFER_LOADWORKER_H
