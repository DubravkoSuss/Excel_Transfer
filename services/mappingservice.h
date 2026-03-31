#ifndef MAPPINGSERVICE_H
#define MAPPINGSERVICE_H

#include <QObject>
#include <QVector>
#include <QPair>
#include "../core/mappingsmanager.h"

class MappingService : public QObject
{
    Q_OBJECT
public:
    struct MappingSelection {
        QString month;
        int year = 0;
        MappingEntry entry;
    };

    explicit MappingService(MappingsManager* manager, QObject* parent = nullptr);

    QVector<MappingSelection> collectMappings(const QVector<QPair<QString, int>>& periods);
    QVector<MappingSelection> collectPaxMappings(const QVector<QPair<QString, int>>& periods);
    QVector<MappingSelection> collectStaffMappings(const QVector<QPair<QString, int>>& periods);
    QVector<MappingSelection> collectSapYtdMappings(const QVector<QPair<QString, int>>& periods);

private:
    MappingsManager* m_manager;
};

#endif // MAPPINGSERVICE_H
