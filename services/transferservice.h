#ifndef TRANSFERSERVICE_H
#define TRANSFERSERVICE_H

#include <QObject>
#include <QMap>
#include <QString>
#include <QVector>
#include "../core/mappingsmanager.h"

class ExcelHandler;

class TransferService : public QObject {
    Q_OBJECT
public:
    struct Result {
        int cellsTransferred = 0;
        bool usedComCopy = false;
        QString error;
    };

    explicit TransferService(ExcelHandler* handler, QObject* parent = nullptr);

    ExcelHandler* handler() const { return m_handler; }

    Result transferEntry(const MappingEntry& entry,
                         int year,
                         const QString& destKey,
                         const QString& destFilePath,
                         const QString& baseFolder);

private:
    Result handleSapYtd(const MappingEntry& entry,
                        int year,
                        const QString& destKey,
                        const QString& destFilePath,
                        const QString& baseFolder);

    Result handleYtd(const MappingEntry& entry,
                     int year,
                     const QString& destKey,
                     const QString& destFilePath);

    ExcelHandler* m_handler;
};

#endif // TRANSFERSERVICE_H
