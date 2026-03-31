#pragma once
#include <QThread>
#include <QMap>
#include "rollingtransferservice.h"

class RollingWorker : public QThread
{
    Q_OBJECT
public:
    explicit RollingWorker(RollingTransferService* service,
                           const QVector<RollingStep>& steps,
                           const QString& destFolder,
                           const QString& jsonBase,
                           const QMap<QString, QVector<MappingEntry>>& allEntriesByMonth,
                           QObject* parent = nullptr)
        : QThread(parent), m_service(service), m_steps(steps)
        , m_destFolder(destFolder), m_jsonBase(jsonBase)
        , m_allEntriesByMonth(allEntriesByMonth) {}

    void run() override {
        RollingResult result = m_service->executeChain(m_steps, m_destFolder, m_jsonBase, m_allEntriesByMonth);
        emit finished(result);
    }

signals:
    void finished(RollingResult result);

private:
    RollingTransferService* m_service;
    QVector<RollingStep> m_steps;
    QString m_destFolder;
    QString m_jsonBase;
    QMap<QString, QVector<MappingEntry>> m_allEntriesByMonth;
};
