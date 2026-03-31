#ifndef MAPPINGMODEL_H
#define MAPPINGMODEL_H

#include <QObject>
#include <QVector>
#include "mappingsmanager.h"

struct MappingItem {
    QString month;
    int year = 0;
    MappingEntry entry;
};

class MappingModel : public QObject
{
    Q_OBJECT

public:
    explicit MappingModel(QObject* parent = nullptr);

    int count() const { return m_items.size(); }
    const QVector<MappingItem>& items() const { return m_items; }

    void addMapping(const QString& month, int year, const MappingEntry& entry);
    void removeAt(int index);
    void clear();
    void setMappings(const QVector<MappingItem>& items);

signals:
    void dataChanged();

private:
    QVector<MappingItem> m_items;
};

#endif // MAPPINGMODEL_H
