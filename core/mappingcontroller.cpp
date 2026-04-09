#include "mappingcontroller.h"
#include "../features/mappings/mappingrow.h"
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <memory>
#include <vector>

MappingController::MappingController(MappingModel* model,
                                     QWidget* parentWidget,
                                     QVBoxLayout* containerLayout,
                                     QObject* parent)
    : QObject(parent)
    , m_model(model)
    , m_parentWidget(parentWidget)
    , m_containerLayout(containerLayout)
    , m_costControlLayout(containerLayout)
    , m_counter(0)
{
    if (m_model) {
        connect(m_model, &MappingModel::dataChanged, this, &MappingController::rebuildUI);
    }
}

void MappingController::addMappingRow(const QString& month, int year, const MappingEntry& entry)
{
    if (!m_model) return;
    m_model->addMapping(month, year, entry);
}

void MappingController::removeMappingRow(int index)
{
    if (!m_model) return;
    m_model->removeAt(index);
}

void MappingController::clearAllMappings()
{
    if (m_model) {
        m_model->clear();
    }
}

MappingRow* MappingController::rowAt(int index) const
{
    if (index < 0 || index >= m_rows.size()) return nullptr;
    return m_rows[index];
}

void MappingController::rebuildUI()
{
    auto clearLayout = [](QVBoxLayout* layout) {
        if (!layout) return;
        while (QLayoutItem* item = layout->takeAt(0)) {
            if (item->widget()) {
                item->widget()->blockSignals(true); // prevent layout signals cascading
                item->widget()->hide();
                item->widget()->deleteLater();
            }
            delete item;
        }
    };

    clearLayout(m_costControlLayout);
    clearLayout(m_containerLayout);
    m_rows.clear();

    m_counter = 0;
    if (!m_model) {
        emit rowCountChanged(0);
        return;
    }

    const QVector<MappingItem> modelItems = m_model->items();
    for (int i = 0; i < modelItems.size(); ++i) {
        const MappingItem& item = modelItems[i];
        m_counter = i + 1;
        QVBoxLayout* targetLayout = m_costControlLayout ? m_costControlLayout : m_containerLayout;
        QWidget* parentWidget = targetLayout ? targetLayout->parentWidget() : nullptr;
        if (!parentWidget) {
            parentWidget = m_parentWidget;
        }
        if (!parentWidget) {
            qWarning() << "MappingRow created without parent; check layout wiring";
        }
        // MappingRow signals must use 0-based index because model/controller APIs are 0-based.
        MappingRow* row = new MappingRow(i, item.month, item.year, item.entry, parentWidget);
        row->setWindowFlags(Qt::Widget);
        if (parentWidget && row->parent() != parentWidget) {
            row->setParent(parentWidget);
        }

        std::vector<std::unique_ptr<QSignalBlocker>> blockers;
        blockers.reserve(8);
        auto addBlocker = [&blockers](QObject* obj) {
            if (obj) blockers.push_back(std::make_unique<QSignalBlocker>(obj));
        };
        addBlocker(row);
        for (QObject* child : row->findChildren<QObject*>()) {
            addBlocker(child);
        }

        // Sync copy-full-sheet UI state from entry
        row->setCopyFullSheet(item.entry.copyFullSheet, item.entry.customSheetName);

        QString label = m_destinationLabels.value(item.entry.destSheet);
        if (!label.isEmpty()) {
            row->setDestSheetLabel(label);
        }

        blockers.clear();

        connect(row, &MappingRow::runClicked,         this, &MappingController::requestRun);
        connect(row, &MappingRow::removeClicked,       this, &MappingController::requestRemove);
        connect(row, &MappingRow::editRowsClicked,     this, &MappingController::requestEditRows);
        connect(row, &MappingRow::exportRowMapClicked, this, &MappingController::requestExportRowMap);
        connect(row, &MappingRow::importRowMapClicked, this, &MappingController::requestImportRowMap);
        connect(row, &MappingRow::ignoreRowsClicked, this, &MappingController::requestIgnoreRows);
        connect(row, &MappingRow::changed,             this, [this]() { emit rowChanged(); emit rowCountChanged(m_rows.size()); });

        if (targetLayout) {
            targetLayout->addWidget(row);
        }
        m_rows.append(row);
    }

    emit rowCountChanged(m_rows.size());
}

void MappingController::setMappings(const QVector<MappingEntry>& entries, const QString& month, int year)
{
    if (!m_model) return;
    QVector<MappingItem> items;
    items.reserve(entries.size());
    for (const MappingEntry& entry : entries) {
        items.append({month, year, entry});
    }
    m_model->setMappings(items);
}

void MappingController::clearMappings()
{
    clearAllMappings();
}

void MappingController::resetCounter()
{
    m_counter = 0;
}

QVector<MappingItem> MappingController::checkedItems() const
{
    QVector<MappingItem> result;
    const QVector<MappingItem> allItems = m_model ? m_model->items() : QVector<MappingItem>();
    for (int i = 0; i < m_rows.size() && i < allItems.size(); ++i) {
        if (m_rows[i] && m_rows[i]->isChecked())
            result.append(allItems[i]);
    }
    return result;
}

void MappingController::setAllChecked(bool checked)
{
    for (MappingRow* row : m_rows) {
        if (row) row->setChecked(checked);
    }
}

void MappingController::setDestinationLabels(const QMap<QString, QString>& labels)
{
    m_destinationLabels = labels;
}

void MappingController::setParentWidget(QWidget* parentWidget)
{
    m_parentWidget = parentWidget;
}

void MappingController::setSectionLayouts(QVBoxLayout* costControl,
                                          QVBoxLayout* refi,
                                          QVBoxLayout* pax,
                                          QVBoxLayout* staff,
                                          QVBoxLayout* sapYtd)
{
    Q_UNUSED(refi); Q_UNUSED(pax); Q_UNUSED(staff); Q_UNUSED(sapYtd);
    m_costControlLayout = costControl ? costControl : m_containerLayout;
    if (!m_containerLayout && m_costControlLayout) {
        m_containerLayout = m_costControlLayout;
    }
}

void MappingController::updateEntryAt(int index, const MappingEntry& entry)
{
    if (m_model) {
        m_model->updateEntryAt(index, entry);
    }
}

void MappingController::syncRowToModel(int index)
{
    MappingRow* row = rowAt(index);
    if (!row || !m_model) return;
    
    // ✅ Start from existing model entry to preserve all JSON fields
    // (sourceFileType, sourcePath, rowMap, sourceJson, etc.)
    MappingEntry updated = m_model->entryAt(index);
    
    // ✅ Patch only the fields the UI can modify
    updated.ignoredDestRows = row->ignoredRows();
    updated.copyFullSheet = row->isCopyFullSheet();
    updated.customSheetName = row->getCustomSheetName();
    updated.insertAfterSheet = row->getInsertAfterSheet();
    
    // Also sync the basic mapping fields that can be edited in UI
    updated.sourceSheetTemplate = row->getSourceSheet();
    updated.sourceColumn = row->getSourceColumn();
    updated.sourceRows = row->getSourceRows();
    updated.destSheet = row->getDestSheet();
    updated.destColumn = row->getDestColumn();
    updated.destRows = row->getDestRows();
    
    m_model->updateEntryAt(index, updated);
    
    qInfo() << "[MappingController] Synced row" << index << "to model"
            << "| ignoredDestRows:" << updated.ignoredDestRows.size()
            << "| sourceFileType:" << updated.sourceFileType
            << "| preserved JSON fields";
}
