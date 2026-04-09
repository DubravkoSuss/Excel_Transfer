#include "mappingsmanager.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

bool MappingsManager::loadSapYtdMappings(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QByteArray data = file.readAll();
    file.close();

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !doc.isArray()) {
        qWarning() << "SAP YTD JSON invalid:" << filePath << error.errorString();
        return false;
    }

    m_sapYtdMappings.clear();

    for (const QJsonValue& v : doc.array()) {
        QJsonObject obj = v.toObject();
        QString month = obj["month"].toString();
        if (month.isEmpty()) continue;

        MonthMapping mm;
        mm.month = month;

        MappingEntry entry;
        entry.month = month;
        entry.sourceSheetTemplate = obj["source_sheet"].toString("Report");
        entry.sourceColumn = obj["source_column"].toString("C");
        entry.destSheet = obj["dest_sheet"].toString("MZLZ Consolidated");
        entry.destColumn = obj["dest_column"].toString("JG");
        entry.sourceFileType = "sap_ytd";

        // traffic_dest_rows: map of destRow (MZLZ) → sourceRow (PAX Sheet1 col Q)
        if (obj.contains("traffic_dest_rows") && obj["traffic_dest_rows"].isObject()) {
            entry.trafficSourceSheet  = "Sheet1";
            entry.trafficDestColumn   = obj["dest_column2"].toString("JG");
            entry.trafficSourceColumn = obj["traffic_source_column"].toString("Q");

            QJsonObject destRowMap = obj["traffic_dest_rows"].toObject();
            for (auto it = destRowMap.constBegin(); it != destRowMap.constEnd(); ++it) {
                int destRow = it.key().toInt();
                int srcRow  = it.value().toInt();
                entry.trafficPaxRowMap[destRow] = srcRow;
            }
            qDebug() << "[SAP_YTD] loaded trafficPaxRowMap for" << month
                     << "entries=" << entry.trafficPaxRowMap.size()
                     << entry.trafficPaxRowMap;
        }

        QJsonObject rowMapObj = obj["rowMap"].toObject();
        for (auto it = rowMapObj.constBegin(); it != rowMapObj.constEnd(); ++it) {
            int destRow = it.key().toInt();
            QVector<int> srcRows;
            if (it.value().isDouble()) {
                srcRows.append(it.value().toInt());
            } else if (it.value().isArray()) {
                for (const QJsonValue& sv : it.value().toArray())
                    srcRows.append(sv.toInt());
            } else if (it.value().isObject()) {
                // { "sum": [row1, row2, ...] } — sum multiple source rows
                QJsonObject subObj = it.value().toObject();
                if (subObj.contains("sum") && subObj["sum"].isArray()) {
                    for (const QJsonValue& sv : subObj["sum"].toArray())
                        srcRows.append(sv.toInt());
                }
            }
            if (!srcRows.isEmpty())
                entry.rowMap[destRow] = srcRows;
        }

        mm.sources.append(entry);
        m_sapYtdMappings[month.toLower()] = mm;
    }

    qDebug() << "SAP YTD loaded months:" << m_sapYtdMappings.keys();
    return true;
}
