#ifndef LOADSERVICE_H
#define LOADSERVICE_H

#include <QObject>
#include "../core/mappingsmanager.h"

class LoadService : public QObject
{
    Q_OBJECT
public:
    struct Result {
        bool success = false;
        QString message;
    };

    explicit LoadService(MappingsManager* manager, QObject* parent = nullptr);

    Result loadMappingsForPeriods(const QVector<QPair<QString, int>>& periods,
                                  const QString& destFolder);

private:
    MappingsManager* m_manager;
};

#endif // LOADSERVICE_H
