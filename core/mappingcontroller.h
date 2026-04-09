#ifndef MAPPINGCONTROLLER_H
#define MAPPINGCONTROLLER_H

#include <QObject>
#include <QVector>
#include <QMap>
#include "mappingmodel.h"

class QWidget;
class QVBoxLayout;
class MappingRow;

class MappingController : public QObject
{
    Q_OBJECT

public:
    explicit MappingController(MappingModel* model,
                               QWidget* parentWidget,
                               QVBoxLayout* containerLayout,
                               QObject* parent = nullptr);

    void addMappingRow(const QString& month, int year, const MappingEntry& entry);
    void removeMappingRow(int index);
    void clearAllMappings();

    // Convenience API
    void setMappings(const QVector<MappingEntry>& entries, const QString& month, int year);
    void clearMappings();
    void resetCounter();

    int mappingCount() const { return m_model ? m_model->count() : 0; }
    MappingRow* rowAt(int index) const;
    QVector<MappingItem> items() const { return m_model ? m_model->items() : QVector<MappingItem>(); }
    QVector<MappingItem> checkedItems() const;
    void setAllChecked(bool checked);
    void setDestinationLabels(const QMap<QString, QString>& labels);
    void setParentWidget(QWidget* parentWidget);
    void setSectionLayouts(QVBoxLayout* costControl,
                           QVBoxLayout* refi,
                           QVBoxLayout* pax,
                           QVBoxLayout* staff,
                           QVBoxLayout* sapYtd);
    void updateEntryAt(int index, const MappingEntry& entry);
    void syncRowToModel(int index);  // Sync widget state back to model

signals:
    void requestRun(int index);
    void requestRemove(int index);
    void requestEditRows(int index);
    void requestExportRowMap(int index);
    void requestImportRowMap(int index);
    void requestIgnoreRows(int index);
    void rowCountChanged(int count);
    void rowChanged(); // emitted when any row's checkbox/state changes

private:
    void rebuildUI();

    MappingModel* m_model;
    QWidget*      m_parentWidget;
    QVBoxLayout*  m_containerLayout;
    QVBoxLayout*  m_costControlLayout = nullptr;
    QVector<MappingRow*> m_rows;
    int m_counter;
    QMap<QString, QString> m_destinationLabels;
};

#endif // MAPPINGCONTROLLER_H
