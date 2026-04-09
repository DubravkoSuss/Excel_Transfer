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
    MappingEntry entryAt(int index) const;  // Get entry at index (preserves all fields)

    void addMapping(const QString& month, int year, const MappingEntry& entry);
    void removeAt(int index);
    void clear();
    void setMappings(const QVector<MappingItem>& items);
    void updateEntryAt(int index, const MappingEntry& entry);

signals:
    void dataChanged();
    void entryUpdated(int index);  // Fine-grained signal for single entry updates

private:
    QVector<MappingItem> m_items;
};

#endif // MAPPINGMODEL_H
