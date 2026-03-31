#include "mappingservice.h"

MappingService::MappingService(MappingsManager* manager, QObject* parent)
    : QObject(parent), m_manager(manager) {}

QVector<MappingService::MappingSelection> MappingService::collectMappings(const QVector<QPair<QString, int>>& periods)
{
    QVector<MappingSelection> results;
    if (!m_manager) {
        return results;
    }

    for (const auto& period : periods) {
        const QString& month = period.first;
        int year = period.second;
        QVector<MappingEntry> entries = m_manager->getMappingsForMonthYear(month, year);
        for (const MappingEntry& entry : entries) {
            results.append({month, year, entry});
        }
    }

    return results;
}

QVector<MappingService::MappingSelection> MappingService::collectPaxMappings(const QVector<QPair<QString, int>>& periods)
{
    QVector<MappingSelection> results;
    if (!m_manager) {
        return results;
    }

    for (const auto& period : periods) {
        const QString& month = period.first;
        int year = period.second;
        PaxMappingEntry pax = m_manager->getPaxMappingForMonth(month);
        if (pax.month.isEmpty()) {
            continue;
        }

        MappingEntry entry;
        entry.month = month;
        entry.sourceSheetTemplate = pax.sourceSheet;
        entry.sourceColumn = pax.sourceColumn;
        entry.sourceRows = pax.sourceRows;
        entry.destSheet = pax.destSheet;
        entry.destColumn = pax.destColumn;
        entry.destRows = pax.destRows;
        entry.sourceFileType = "pax";
        results.append({month, year, entry});
    }

    return results;
}

QVector<MappingService::MappingSelection> MappingService::collectStaffMappings(const QVector<QPair<QString, int>>& periods)
{
    QVector<MappingSelection> results;
    if (!m_manager) {
        return results;
    }

    for (const auto& period : periods) {
        const QString& month = period.first;
        int year = period.second;
        StaffMappingEntry staff = m_manager->getStaffMappingForMonth(month);
        if (staff.month.isEmpty()) {
            continue;
        }

        MappingEntry entry;
        entry.month = month;
        // Resolve {year} in source sheet template e.g. "{year} FTE" → "2025 FTE"
        entry.sourceSheetTemplate = m_manager->resolveSheetName(staff.sourceSheet, year, month);
        entry.sourceColumn = staff.sourceColumn;
        entry.sourceRows   = {staff.sourceRow};
        entry.destSheet    = staff.destSheet;
        entry.destColumn   = staff.destColumn;
        entry.destRows     = {staff.destRow};
        entry.sourceFileType = "staff";
        results.append({month, year, entry});
    }

    return results;
}

QVector<MappingService::MappingSelection> MappingService::collectSapYtdMappings(const QVector<QPair<QString, int>>& periods)
{
    QVector<MappingSelection> results;
    if (!m_manager) {
        return results;
    }

    for (const auto& period : periods) {
        const QString& month = period.first;
        int year = period.second;
        QVector<MappingEntry> entries = m_manager->getMappingsForMonthYear(month, year);
        for (const MappingEntry& entry : entries) {
            if (entry.sourceFileType == "sap_ytd") {
                results.append({month, year, entry});
            }
        }
    }

    return results;
}
