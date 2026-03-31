#include "loadservice.h"
#include <QFile>

LoadService::LoadService(MappingsManager* manager, QObject* parent)
    : QObject(parent), m_manager(manager) {}

LoadService::Result LoadService::loadMappingsForPeriods(const QVector<QPair<QString, int>>& periods,
                                                        const QString& destFolder)
{
    Result result;
    if (!m_manager) {
        result.message = "Mappings manager not available";
        return result;
    }

    for (const auto& period : periods) {
        const QString& month = period.first;
        int year = period.second;
        QString fileName = QString("%1/%2/%3/mappings.json").arg(destFolder).arg(year).arg(month);
        if (QFile::exists(fileName)) {
            m_manager->loadMappings(fileName);
            result.success = true;
        }
    }

    if (!result.success) {
        result.message = "No mappings.json found for selected periods";
    }
    return result;
}
