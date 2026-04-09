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

void MappingModel::updateEntryAt(int index, const MappingEntry& entry)
{
    if (index >= 0 && index < m_items.size()) {
        m_items[index].entry = entry;
        // Emit fine-grained signal instead of dataChanged() to avoid full UI rebuild
        emit entryUpdated(index);
    }
}

MappingEntry MappingModel::entryAt(int index) const
{
    if (index >= 0 && index < m_items.size()) {
        return m_items[index].entry;
    }
    return MappingEntry();  // Return empty entry if index out of range
}
