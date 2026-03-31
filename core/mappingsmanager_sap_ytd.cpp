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

        // Optional traffic_mott additions
        if (obj.contains("traffic_source") && obj["traffic_source"].isObject()) {
            QJsonObject ts = obj["traffic_source"].toObject();
            entry.trafficSourceSheet = ts["sheet"].toString("TRAFFIC mott 2025");
            entry.trafficDestColumn = obj["dest_column2"].toString("JG");
            entry.trafficSourceColumn = obj["traffic_source_column"].toString("Q");
            // source_rows is inside traffic_source object
            for (const QJsonValue& rv : ts["source_rows"].toArray())
                entry.trafficSourceRows.append(rv.toInt());
            // traffic_dest_rows is at the TOP LEVEL of the JSON object (not inside traffic_source)
            for (const QJsonValue& rv : obj["traffic_dest_rows"].toArray())
                entry.trafficDestRows.append(rv.toInt());
            qDebug() << "[SAP_YTD] loaded traffic rows for" << month
                     << "srcRows=" << entry.trafficSourceRows
                     << "destRows=" << entry.trafficDestRows;
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
            }
            entry.rowMap[destRow] = srcRows;
        }

        mm.sources.append(entry);
        m_sapYtdMappings[month.toLower()] = mm;
    }

    qDebug() << "SAP YTD loaded months:" << m_sapYtdMappings.keys();
    return true;
}
