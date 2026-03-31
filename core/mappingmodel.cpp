#include "mappingmodel.h"

MappingModel::MappingModel(QObject* parent)
    : QObject(parent)
{
}

void MappingModel::addMapping(const QString& month, int year, const MappingEntry& entry)
{
    MappingItem item{month, year, entry};
    m_items.append(item);
    emit dataChanged();
}

void MappingModel::removeAt(int index)
{
    if (index < 0 || index >= m_items.size()) {
        return;
    }
    m_items.removeAt(index);
    emit dataChanged();
}

void MappingModel::clear()
{
    m_items.clear();
    emit dataChanged();
}

void MappingModel::setMappings(const QVector<MappingItem>& items)
{
    m_items = items;
    emit dataChanged();
}
