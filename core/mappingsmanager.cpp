#include "mappingsmanager.h"
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

const QStringList MappingsManager::MONTHS_LIST = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

const QMap<QString, QString> MappingsManager::MONTH_TO_NUM = {{"January", "01"}, {"February", "02"}, {"March", "03"}, {"April", "04"}, {"May", "05"}, {"June", "06"}, {"July", "07"}, {"August", "08"}, {"September", "09"}, {"October", "10"}, {"November", "11"}, {"December", "12"}};

const QMap<int, QList<int>> MappingsManager::QUARTERS = {{1, {0, 1, 2}}, {2, {3, 4, 5}}, {3, {6, 7, 8}}, {4, {9, 10, 11}}};

MappingsManager::MappingsManager(QObject *parent) : QObject(parent) {}
MappingsManager::~MappingsManager() {}

bool MappingsManager::loadMappings(const QString& filePath) {
    m_currentFilePath = filePath;

    qDebug() << "MappingsManager: opening" << filePath;
    QFile file(filePath);
    if (!file.exists()) {
        qWarning() << "FILE DOES NOT EXIST:" << filePath;
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "CANNOT OPEN FILE:" << filePath;
        return false;
    }
    QByteArray data = file.readAll();
    file.close();
    qDebug() << "MappingsManager: read" << data.size() << "bytes";

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (doc.isNull()) {
        qWarning() << "JSON PARSE ERROR:" << error.errorString() << "at" << error.offset;
        return false;
    }

    m_currentFilePath = filePath;
    return parseJsonFile(filePath);
}

bool MappingsManager::loadPaxMappings(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QByteArray data = file.readAll();
    file.close();

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return false;

    QJsonObject root = doc.object();
    QJsonObject src  = root["source"].toObject();
    QJsonObject dest = root["dest"].toObject();

    QString sourceSheet = src["sheet"].toString("Sheet1");
    QString destSheet   = dest["sheet"].toString("MZLZ Consolidated");

    QVector<int> sourceRows, destRows;
    for (const QJsonValue& v : src["source_rows"].toArray())  sourceRows.append(v.toInt());
    for (const QJsonValue& v : dest["dest_rows"].toArray())   destRows.append(v.toInt());

    m_paxMappings.clear();

    // One entry per month — each has its own source/dest column
    for (const QJsonValue& mv : root["month_mappings"].toArray()) {
        QJsonObject mm = mv.toObject();
        PaxMappingEntry entry;
        entry.month        = mm["month"].toString();
        entry.sourceSheet  = sourceSheet;
        entry.destSheet    = destSheet;
        entry.sourceColumn = mm["source_column"].toString("G");
        entry.destColumn   = mm["dest_column"].toString("G");
        entry.sourceRows   = sourceRows;
        entry.destRows     = destRows;
        m_paxMappings[entry.month.toLower()] = entry;
    }
    return true;
}

bool MappingsManager::loadStaffMappings(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QByteArray data = file.readAll();
    file.close();

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return false;

    QJsonObject root = doc.object();
    QJsonObject src  = root["source"].toObject();
    QJsonObject dest = root["dest"].toObject();

    QString sourceSheetTemplate = src["sheet"].toString();   // e.g. "{year} FTE"
    int sourceRow = src["row"].toInt(33);
    QString destSheet = dest["sheet"].toString("MZLZ Consolidated");
    int destRow = dest["row"].toInt(16);

    m_staffMappings.clear();

    for (const QJsonValue& mv : root["month_mappings"].toArray()) {
        QJsonObject mm = mv.toObject();
        StaffMappingEntry entry;
        entry.month         = mm["month"].toString();
        entry.sourceSheet   = sourceSheetTemplate;           // resolved at transfer time with year
        entry.sourceRow     = sourceRow;
        entry.sourceColumn  = mm["source_column"].toString("D");
        entry.destSheet     = destSheet;
        entry.destRow       = destRow;
        entry.destColumn    = mm["dest_column"].toString("G");
        m_staffMappings[entry.month.toLower()] = entry;
    }
    return true;
}

bool MappingsManager::loadBudgetRefiMappings(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QByteArray data = file.readAll();
    file.close();

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError) {
        qWarning() << "loadBudgetRefiMappings: JSON parse error:" << error.errorString();
        return false;
    }

    // Helper: parse a rowMap value (int, array, or {sum:[...]})
    auto parseRowMapValue = [](const QJsonValue& value) -> QVector<int> {
        QVector<int> srcRows;
        if (value.isArray()) {
            for (const QJsonValue& rv : value.toArray())
                srcRows.append(rv.toInt());
        } else if (value.isObject()) {
            QJsonObject obj = value.toObject();
            if (obj.contains("sum") && obj["sum"].isArray()) {
                for (const QJsonValue& rv : obj["sum"].toArray())
                    srcRows.append(rv.toInt());
            }
        } else {
            srcRows.append(value.toInt());
        }
        return srcRows;
    };

    m_budgetRefiMappings.clear();

    // Support both old format (array of {month, sources[]})
    // and new format (single object with dest_sheet, sources[], month_mappings[])
    QJsonObject root = doc.object();

    if (root.contains("sources") && root.contains("month_mappings")) {
        // ── NEW FORMAT ──
        // sources[] define the rowMaps per source_sheet_template
        // month_mappings[] define per-month column assignments
        QString destSheet = root["dest_sheet"].toString("MZLZ Consolidated");

        // Build rowMap per source_sheet_template
        struct SourceDef {
            QString sourceSheetTemplate;
            QMap<int, QVector<int>> rowMap;
        };
        QVector<SourceDef> sourceDefs;
        for (const QJsonValue& sv : root["sources"].toArray()) {
            QJsonObject src = sv.toObject();
            SourceDef def;
            def.sourceSheetTemplate = src["source_sheet_template"].toString();
            QJsonObject rowMapObj = src["rowMap"].toObject();
            for (auto it = rowMapObj.constBegin(); it != rowMapObj.constEnd(); ++it) {
                bool ok = false;
                int destRow = it.key().toInt(&ok);
                if (!ok) continue;
                def.rowMap.insert(destRow, parseRowMapValue(it.value()));
            }
            sourceDefs.append(def);
        }

        qDebug() << "[BUDGETREFI] sourceDefs loaded:" << sourceDefs.size();
        for (const SourceDef& def : sourceDefs)
            qDebug() << "[BUDGETREFI]  source:" << def.sourceSheetTemplate << "rowMap size:" << def.rowMap.size();
        qDebug() << "[BUDGETREFI] month_mappings count:" << root["month_mappings"].toArray().size();

        // For each month in month_mappings, create MappingEntry per column_mapping
        for (const QJsonValue& mmv : root["month_mappings"].toArray()) {
            QJsonObject mm = mmv.toObject();
            QString month = mm["month"].toString().toLower();
            QVector<MappingEntry> entries;

            for (const QJsonValue& cmv : mm["column_mappings"].toArray()) {
                QJsonObject cm = cmv.toObject();
                QString srcSheetTemplate = cm["source_sheet_template"].toString().trimmed();
                QString srcCol = cm["source_column"].toString();
                QString dstCol = cm["dest_column"].toString();

                bool found = false;
                for (const SourceDef& def : sourceDefs) {
                    if (def.sourceSheetTemplate.trimmed() != srcSheetTemplate) continue;
                    found = true;
                    MappingEntry entry;
                    entry.sourceSheetTemplate = srcSheetTemplate;
                    entry.sourceColumn        = srcCol;
                    entry.destColumn          = dstCol;
                    entry.destSheet           = destSheet;
                    entry.sourceFileType      = "cost_control";
                    entry.copyFullSheet       = false;
                    entry.rowMap              = def.rowMap;
                    entries.append(entry);
                    qDebug() << "[BUDGETREFI] month=" << month << "matched:" << srcSheetTemplate << "rowMap size:" << entry.rowMap.size() << "srcCol=" << srcCol << "dstCol=" << dstCol;
                    break;
                }
                if (!found)
                    qWarning() << "[BUDGETREFI] NO MATCH for template:" << srcSheetTemplate << "month=" << month;
            }
            m_budgetRefiMappings[month] = entries;
        }
    } else {
        // ── OLD FORMAT ── (array of {month, sources[]})
        QJsonArray arr = doc.isArray() ? doc.array() : QJsonArray() << root;
        for (const QJsonValue &v : arr) {
            QJsonObject monthObj = v.toObject();
            QString month = monthObj["month"].toString().toLower();
            QVector<MappingEntry> entries;

            for (const QJsonValue &sv : monthObj["sources"].toArray()) {
                QJsonObject src = sv.toObject();
                MappingEntry entry;
                entry.sourceSheetTemplate = src["source_sheet_template"].toString();
                entry.sourceColumn        = src["source_column"].toString("N");
                entry.destSheet           = src["dest_sheet"].toString("MZLZ Consolidated");
                entry.destColumn          = src["dest_column"].toString("G");
                entry.sourceFileType      = "cost_control";
                entry.copyFullSheet       = false;

                if (src.contains("rowMap")) {
                    QJsonObject rowMapObj = src["rowMap"].toObject();
                    for (auto it = rowMapObj.constBegin(); it != rowMapObj.constEnd(); ++it) {
                        bool ok = false;
                        int destRow = it.key().toInt(&ok);
                        if (!ok) continue;
                        entry.rowMap.insert(destRow, parseRowMapValue(it.value()));
                    }
                } else {
                    for (const QJsonValue &rv : src["source_rows"].toArray()) entry.sourceRows.append(rv.toInt());
                    for (const QJsonValue &rv : src["dest_rows"].toArray())   entry.destRows.append(rv.toInt());
                }
                entries.append(entry);
            }
            m_budgetRefiMappings[month] = entries;
        }
    }

    qDebug() << "loadBudgetRefiMappings: loaded" << m_budgetRefiMappings.size() << "months from" << filePath;
    return true;
}

bool MappingsManager::loadPaxTransferMappings(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QByteArray data = file.readAll();
    file.close();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "loadPaxTransferMappings: parse error:" << err.errorString();
        return false;
    }

    QJsonObject root = doc.object();
    QString sourceSheet = root["source_sheet"].toString("Sheet1");
    QString destSheet   = root["dest_sheet"].toString("TRAFFIC mott 2025");

    // Parse rowMap
    QMap<int, QVector<int>> rowMap;
    QJsonObject rowMapObj = root["rowMap"].toObject();
    for (auto it = rowMapObj.constBegin(); it != rowMapObj.constEnd(); ++it) {
        bool ok = false;
        int destRow = it.key().toInt(&ok);
        if (!ok) continue;
        QVector<int> srcRows;
        if (it.value().isArray()) {
            for (const QJsonValue& rv : it.value().toArray())
                srcRows.append(rv.toInt());
        } else {
            srcRows.append(it.value().toInt());
        }
        rowMap.insert(destRow, srcRows);
    }

    m_paxTransferMappings.clear();
    for (const QJsonValue& mmv : root["month_mappings"].toArray()) {
        QJsonObject mm = mmv.toObject();
        QString month = mm["month"].toString().toLower();
        MappingEntry entry;
        entry.month               = mm["month"].toString();
        entry.sourceSheetTemplate = sourceSheet;
        entry.sourceColumn        = mm["source_column"].toString();
        entry.destSheet           = destSheet;
        entry.destColumn          = mm["dest_column"].toString();
        entry.sourceFileType      = "pax_transfer";
        entry.copyFullSheet       = false;
        entry.rowMap              = rowMap;
        m_paxTransferMappings[month] = {entry};
    }

    qDebug() << "loadPaxTransferMappings: loaded" << m_paxTransferMappings.size() << "months from" << filePath;
    return true;
}

bool MappingsManager::loadTrafficMottMappings(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QByteArray data = file.readAll();
    file.close();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "loadTrafficMottMappings: parse error:" << err.errorString();
        return false;
    }

    QJsonObject root = doc.object();
    QString sourceSheet = root["source_sheet"].toString("Sheet1");
    QString destSheet   = root["dest_sheet"].toString("TRAFFIC mott 2025");

    QMap<int, QVector<int>> rowMap;
    QJsonObject rowMapObj = root["rowMap"].toObject();
    for (auto it = rowMapObj.constBegin(); it != rowMapObj.constEnd(); ++it) {
        bool ok = false;
        int destRow = it.key().toInt(&ok);
        if (!ok) continue;
        QVector<int> srcRows;
        if (it.value().isArray()) {
            for (const QJsonValue& rv : it.value().toArray())
                srcRows.append(rv.toInt());
        } else {
            srcRows.append(it.value().toInt());
        }
        rowMap.insert(destRow, srcRows);
    }

    m_trafficMottMappings.clear();
    for (const QJsonValue& mmv : root["month_mappings"].toArray()) {
        QJsonObject mm = mmv.toObject();
        QString month = mm["month"].toString().toLower();
        MappingEntry entry;
        entry.month               = mm["month"].toString();
        entry.sourceSheetTemplate = sourceSheet;
        entry.sourceColumn        = mm["source_column"].toString();
        entry.destSheet           = destSheet;
        entry.destColumn          = mm["dest_column"].toString();
        entry.sourceFileType      = "traffic_mott";
        entry.copyFullSheet       = false;
        entry.rowMap              = rowMap;
        m_trafficMottMappings[month] = {entry};
    }

    qDebug() << "loadTrafficMottMappings: loaded" << m_trafficMottMappings.size() << "months from" << filePath;
    return true;
}

QVector<MappingEntry> MappingsManager::getPaxTransferMappingsForMonthYear(const QString& month, int year) {
    Q_UNUSED(year);
    QString monthLower = month.toLower().trimmed();
    if (m_paxTransferMappings.contains(monthLower)) {
        QVector<MappingEntry> entries = m_paxTransferMappings[monthLower];
        for (MappingEntry& e : entries) e.month = month;
        return entries;
    }
    return {};
}

QVector<MappingEntry> MappingsManager::getTrafficMottMappingsForMonthYear(const QString& month, int year) {
    Q_UNUSED(year);
    QString monthLower = month.toLower().trimmed();
    if (m_trafficMottMappings.contains(monthLower)) {
        QVector<MappingEntry> entries = m_trafficMottMappings[monthLower];
        for (MappingEntry& e : entries) e.month = month;
        return entries;
    }
    return {};
}

bool MappingsManager::saveMappings(const QString& filePath) {
    m_currentFilePath = filePath;
    return writeJsonFile(filePath);
}

QVector<MappingEntry> MappingsManager::getMappingsForMonthYear(const QString& month, int year) {
    QString monthLower = month.toLower().trimmed();
    QVector<MappingEntry> results;

    if (m_mappings.contains(monthLower)) {
        for (const MappingEntry& entry : m_mappings[monthLower].sources) {
            MappingEntry e = entry;
            e.month = month;
            if (!e.sourceSheetTemplate.isEmpty())
                e.sourceSheetTemplate = resolveSheetName(e.sourceSheetTemplate, year, month);
            results.append(e);
        }
    }
    return results;
}

QVector<MappingEntry> MappingsManager::getDynamicMappingsForMonthYear(const QString& month, int year) {
    QString monthLower = month.toLower().trimmed();
    
    if (m_budgetRefiMappings.contains(monthLower)) {
        QVector<MappingEntry> entries = m_budgetRefiMappings[monthLower];
        for (MappingEntry &entry : entries) {
            entry.month = month;  // ensure month is set — was empty for budget/refi entries
            entry.sourceSheetTemplate = resolveSheetName(entry.sourceSheetTemplate, year, month);
        }
        return entries;
    }
    return QVector<MappingEntry>();
}

QVector<MappingEntry> MappingsManager::getSapYtdMappingsForMonthYear(const QString& month, int year)
{
    Q_UNUSED(year);
    QString monthLower = month.toLower().trimmed();
    QVector<MappingEntry> results;

    if (m_sapYtdMappings.contains(monthLower)) {
        for (const MappingEntry& entry : m_sapYtdMappings[monthLower].sources) {
            MappingEntry e = entry;
            e.month = month;
            results.append(e);
        }
    }
    return results;
}

PaxMappingEntry MappingsManager::getPaxMappingForMonth(const QString& month) {
    QString monthLower = month.toLower().trimmed();
    return m_paxMappings.value(monthLower);
}

StaffMappingEntry MappingsManager::getStaffMappingForMonth(const QString& month) {
    QString monthLower = month.toLower().trimmed();
    return m_staffMappings.value(monthLower);
}

void MappingsManager::addMapping(const QString& month, const MappingEntry& entry) {
    QString monthLower = month.toLower().trimmed();
    if (!m_mappings.contains(monthLower)) {
        m_mappings[monthLower] = MonthMapping{monthLower, QVector<MappingEntry>()};
    }
    m_mappings[monthLower].sources.append(entry);
}

void MappingsManager::removeMapping(const QString& month, int index) {
    QString monthLower = month.toLower().trimmed();
    if (m_mappings.contains(monthLower) && index >= 0 && index < m_mappings[monthLower].sources.size()) {
        m_mappings[monthLower].sources.removeAt(index);
    }
}

void MappingsManager::updateMapping(const QString& month, int index, const MappingEntry& entry) {
    QString monthLower = month.toLower().trimmed();
    if (m_mappings.contains(monthLower) && index >= 0 && index < m_mappings[monthLower].sources.size()) {
        m_mappings[monthLower].sources[index] = entry;
    }
}

QStringList MappingsManager::getAvailableMonths() const {
    return m_mappings.keys();
}

QStringList MappingsManager::getSourceSheets() const {
    QSet<QString> sheets;
    for (auto it = m_mappings.constBegin(); it != m_mappings.constEnd(); ++it) {
        for (const MappingEntry &entry : it.value().sources) {
            sheets.insert(entry.sourceSheetTemplate);
        }
    }
    return QStringList(sheets.begin(), sheets.end());
}

QStringList MappingsManager::getDestSheets() const {
    QSet<QString> sheets;
    for (auto it = m_mappings.constBegin(); it != m_mappings.constEnd(); ++it) {
        for (const MappingEntry &entry : it.value().sources) {
            sheets.insert(entry.destSheet);
        }
    }
    return QStringList(sheets.begin(), sheets.end());
}

QString MappingsManager::resolveSheetName(const QString& template_, int year, const QString& month)
{
    static const QMap<QString, QString> MONTH_TO_NUM = {
        {"january","1"},{"february","2"},{"march","3"},{"april","4"},
        {"may","5"},{"june","6"},{"july","7"},{"august","8"},
        {"september","9"},{"october","10"},{"november","11"},{"december","12"}
    };

    QString result = template_;

    // {year-1} → previous year (e.g. 2024)
    result.replace("{year-1}", QString::number(year - 1), Qt::CaseInsensitive);

    // "{year} Budget" or "{year} BUDGET" → actual sheet name is "BUDGET {year}"
    {
        QRegularExpression re("\\{year\\}\\s*budget", QRegularExpression::CaseInsensitiveOption);
        if (re.match(result).hasMatch())
            return QString("BUDGET %1").arg(year);
    }

    // "{year} ACT" or similar patterns where year comes first
    // {year} FTE → "2025 FTE" (staff sheet — use actual year, not month number)
    // Plain {year} in cost control context → month number (e.g. "7" for July)
    // Detect: if template contains "FTE", "staff", "budget" (non-month sheets) → use year number
    QString lowerResult = result.toLower();
    bool useYearNumber = lowerResult.contains("fte") || lowerResult.contains("staff") ||
                         lowerResult.contains("refi") || lowerResult.contains("ytd");

    if (useYearNumber || month.isEmpty()) {
        result.replace("{year}", QString::number(year), Qt::CaseInsensitive);
    } else {
        QString monthNum = MONTH_TO_NUM.value(month.toLower().trimmed());
        result.replace("{year}", monthNum.isEmpty() ? QString::number(year) : monthNum,
                       Qt::CaseInsensitive);
    }

    return result;
}

bool MappingsManager::parseJsonFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    
    QByteArray data = file.readAll();
    file.close();
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        emit mappingsLoaded(false, parseError.errorString());
        return false;
    }

    // If this is SAP YTD, store separately (do not wipe main mappings)
    bool isSapYtd = filePath.contains("sap_ytd", Qt::CaseInsensitive);
    if (!isSapYtd) {
        m_mappings.clear();
    }

    auto parseRowMapValue = [](const QJsonValue& value) -> QVector<int> {
        QVector<int> srcRows;
        if (value.isArray()) {
            for (const QJsonValue& rv : value.toArray()) {
                srcRows.append(rv.toInt());
            }
        } else if (value.isObject()) {
            QJsonObject obj = value.toObject();
            if (obj.contains("sum") && obj["sum"].isArray()) {
                for (const QJsonValue& rv : obj["sum"].toArray()) {
                    srcRows.append(rv.toInt());
                }
            }
        } else {
            srcRows.append(value.toInt());
        }
        return srcRows;
    };

    // New mappings.json format (single object with source/dest/month_mappings)
    if (doc.isObject()) {
        QJsonObject root = doc.object();

        // New mappings.json variant (flat object with source_sheet/dest_sheet/rowMap/month_mappings)
        if (root.contains("source_sheet") && root.contains("dest_sheet") && root.contains("month_mappings")) {
            QString sourceSheetTemplate = root["source_sheet"].toString("{year}");
            QString sourceColumnDefault = root["source_column"].toString("C").toUpper();
            QString destSheet = root["dest_sheet"].toString("MZLZ Consolidated");

            QMap<int, QVector<int>> rowMap;
            if (root.contains("rowMap") && root["rowMap"].isObject()) {
                QJsonObject rowMapObj = root["rowMap"].toObject();
                for (auto it = rowMapObj.constBegin(); it != rowMapObj.constEnd(); ++it) {
                    bool ok = false;
                    int destRow = it.key().toInt(&ok);
                    if (!ok) continue;
                    rowMap.insert(destRow, parseRowMapValue(it.value()));
                }
            }

            QVector<int> cumulativeRows;
            if (root.contains("cumulative_rows") && root["cumulative_rows"].isArray()) {
                for (const QJsonValue& rv : root["cumulative_rows"].toArray()) {
                    cumulativeRows.append(rv.toInt());
                }
            }

            QJsonArray monthMappings = root["month_mappings"].toArray();
            for (const QJsonValue& mv : monthMappings) {
                if (!mv.isObject()) continue;
                QJsonObject mm = mv.toObject();
                QString month = mm["month"].toString();
                if (month.isEmpty()) continue;

                MonthMapping mmEntry;
                mmEntry.month = month;

                MappingEntry entry;
                entry.month = month;
                entry.sourceSheetTemplate = sourceSheetTemplate;
                entry.sourceColumn = mm["source_column"].toString(sourceColumnDefault).toUpper();
                entry.destSheet = destSheet;
                entry.destColumn = mm["dest_column"].toString("G").toUpper();
                entry.rowMap = rowMap;
                entry.cumulativeRows = cumulativeRows;

                if (filePath.contains("sap_ytd", Qt::CaseInsensitive)) {
                    entry.sourceFileType = "sap_ytd";
                } else if (filePath.contains("mappings_old", Qt::CaseInsensitive) ||
                           filePath.contains("mappings.json", Qt::CaseInsensitive)) {
                    entry.sourceFileType = "sap";
                } else {
                    entry.sourceFileType = "cost_control";
                }
                entry.copyFullSheet = false;

                mmEntry.sources.append(entry);
                if (isSapYtd) {
                    m_sapYtdMappings[month.toLower()] = mmEntry;
                } else {
                    m_mappings[month.toLower()] = mmEntry;
                }
            }
            return true;
        }

        if (root.contains("source") && root.contains("dest") && root.contains("month_mappings")) {
            QJsonObject src = root["source"].toObject();
            QJsonObject dest = root["dest"].toObject();

            QString sourceSheetTemplate = src["sheet"].toString("{year}");
            QString sourceColumnDefault = src["column"].toString("C").toUpper();
            QVector<int> sourceRows;
            for (const QJsonValue& v : src["source_rows"].toArray()) sourceRows.append(v.toInt());

            QString destSheet = dest["sheet"].toString("MZLZ Consolidated");
            QVector<int> destRows;
            for (const QJsonValue& v : dest["dest_rows"].toArray()) destRows.append(v.toInt());

            QJsonArray monthMappings = root["month_mappings"].toArray();
            for (const QJsonValue& mv : monthMappings) {
                if (!mv.isObject()) continue;
                QJsonObject mm = mv.toObject();
                QString month = mm["month"].toString();
                if (month.isEmpty()) continue;

                MonthMapping mmEntry;
                mmEntry.month = month;

                MappingEntry entry;
                entry.month = month;
                entry.sourceSheetTemplate = sourceSheetTemplate;
                entry.sourceColumn = mm["source_column"].toString(sourceColumnDefault).toUpper();
                entry.sourceRows = sourceRows;
                entry.destSheet = destSheet;
                entry.destColumn = mm["dest_column"].toString("G").toUpper();
                entry.destRows = destRows;
                // Determine source file type based on JSON file name
                if (filePath.contains("sap_ytd", Qt::CaseInsensitive)) {
                    entry.sourceFileType = "sap_ytd";
                } else if (filePath.contains("mappings_old", Qt::CaseInsensitive) ||
                           filePath.contains("mappings.json", Qt::CaseInsensitive)) {
                    entry.sourceFileType = "sap"; // SAP monthly file (e.g., 05_2025.xlsx)
                } else {
                    entry.sourceFileType = "cost_control";
                }
                entry.copyFullSheet = false;

                mmEntry.sources.append(entry);
                if (isSapYtd) {
                    m_sapYtdMappings[month.toLower()] = mmEntry;
                } else {
                    m_mappings[month.toLower()] = mmEntry;
                }
            }
            return true;
        }
    }

    if (doc.isArray()) {
        QJsonArray array = doc.array();
        for (const QJsonValue& val : array) {
            if (!val.isObject()) continue;

            QJsonObject obj = val.toObject();
            QString month = obj["month"].toString();
            if (month.isEmpty()) continue;

            MonthMapping mm;
            mm.month = month;

            if (obj.contains("sources") && obj["sources"].isArray()) {
                QJsonArray sources = obj["sources"].toArray();
                for (const QJsonValue& srcVal : sources) {
                    if (!srcVal.isObject()) continue;

                    QJsonObject srcObj = srcVal.toObject();
                    MappingEntry entry;
                    entry.month = month;
                    entry.sourceSheetTemplate = srcObj["source_sheet_template"].toString();
                    entry.sourceColumn = srcObj["source_column"].toString("N").toUpper();
                    entry.destSheet = srcObj["dest_sheet"].toString("MZLZ Consolidated");
                    entry.destColumn = srcObj["dest_column"].toString("G").toUpper();
                    entry.sourceFileType = srcObj["source_file_type"].toString("sap");
                    entry.copyFullSheet = srcObj["copy_full_sheet"].toBool(false);
                    entry.customSheetName = srcObj["custom_sheet_name"].toString();

                    if (srcObj.contains("rowMap") && srcObj["rowMap"].isObject()) {
                        QJsonObject rowMapObj = srcObj["rowMap"].toObject();
                        for (auto it = rowMapObj.begin(); it != rowMapObj.end(); ++it) {
                            bool ok = false;
                            int destRow = it.key().toInt(&ok);
                            if (!ok) continue;
                            entry.rowMap.insert(destRow, parseRowMapValue(it.value()));
                        }
                    }

                    if (srcObj.contains("source_rows") && srcObj["source_rows"].isArray()) {
                        QJsonArray rows = srcObj["source_rows"].toArray();
                        for (const QJsonValue& rowVal : rows) {
                            entry.sourceRows.append(rowVal.toInt());
                        }
                    }

                    if (srcObj.contains("dest_rows") && srcObj["dest_rows"].isArray()) {
                        QJsonArray rows = srcObj["dest_rows"].toArray();
                        for (const QJsonValue& rowVal : rows) {
                            entry.destRows.append(rowVal.toInt());
                        }
                    }

                    mm.sources.append(entry);
                }
                if (isSapYtd) {
                    m_sapYtdMappings[month.toLower()] = mm;
                } else {
                    m_mappings[month.toLower()] = mm;
                }
                continue;
            }

            // Legacy flat format
            if (obj.contains("source_rows") && obj.contains("dest_rows")) {
                MappingEntry entry;
                entry.month = month;
                entry.sourceSheetTemplate = obj["source_sheet"].toString();
                entry.sourceColumn = obj["source_column"].toString("D").toUpper();
                entry.destSheet = obj["dest_sheet"].toString("MZLZ Consolidated");
                entry.destColumn = obj["dest_column"].toString("G").toUpper();
                entry.sourceFileType = obj["source_file_type"].toString("sap");
                entry.copyFullSheet = obj["copy_full_sheet"].toBool(false);

                if (obj.contains("source_rows") && obj["source_rows"].isArray()) {
                    for (const QJsonValue& rowVal : obj["source_rows"].toArray()) {
                        entry.sourceRows.append(rowVal.toInt());
                    }
                }
                if (obj.contains("dest_rows") && obj["dest_rows"].isArray()) {
                    for (const QJsonValue& rowVal : obj["dest_rows"].toArray()) {
                        entry.destRows.append(rowVal.toInt());
                    }
                }

                addMapping(entry.month, entry);
                continue;
            }

            m_mappings[month.toLower()] = mm;
        }
    } else if (doc.isObject()) {
        QJsonObject obj = doc.object();
        QString month = obj["month"].toString();

        MonthMapping mm;
        mm.month = month;

        if (obj.contains("sources") && obj["sources"].isArray()) {
            QJsonArray sources = obj["sources"].toArray();
            for (const QJsonValue& srcVal : sources) {
                if (!srcVal.isObject()) continue;

                QJsonObject srcObj = srcVal.toObject();
                MappingEntry entry;
                entry.month = month;
                entry.sourceSheetTemplate = srcObj["source_sheet_template"].toString();
                entry.sourceColumn = srcObj["source_column"].toString("N").toUpper();
                entry.destSheet = srcObj["dest_sheet"].toString("MZLZ Consolidated");
                entry.destColumn = srcObj["dest_column"].toString("G").toUpper();
                entry.sourceFileType = srcObj["source_file_type"].toString("cost_control");
                entry.copyFullSheet = srcObj["copy_full_sheet"].toBool(false);
                entry.customSheetName = srcObj["custom_sheet_name"].toString();
                entry.insertAfterSheet = srcObj["insert_after"].toString();

                if (srcObj.contains("rowMap") && srcObj["rowMap"].isObject()) {
                    QJsonObject rowMapObj = srcObj["rowMap"].toObject();
                    for (auto it = rowMapObj.begin(); it != rowMapObj.end(); ++it) {
                        bool ok = false;
                        int destRow = it.key().toInt(&ok);
                        if (!ok) continue;
                        QVector<int> srcRows;
                        if (it.value().isArray()) {
                            for (const QJsonValue& rv : it.value().toArray()) {
                                srcRows.append(rv.toInt());
                            }
                        } else {
                            srcRows.append(it.value().toInt());
                        }
                        entry.rowMap.insert(destRow, srcRows);
                    }
                }

                if (srcObj.contains("source_rows") && srcObj["source_rows"].isArray()) {
                    QJsonArray rows = srcObj["source_rows"].toArray();
                    for (const QJsonValue& rowVal : rows) {
                        entry.sourceRows.append(rowVal.toInt());
                    }
                }

                if (srcObj.contains("dest_rows") && srcObj["dest_rows"].isArray()) {
                    QJsonArray rows = srcObj["dest_rows"].toArray();
                    for (const QJsonValue& rowVal : rows) {
                        entry.destRows.append(rowVal.toInt());
                    }
                }

                mm.sources.append(entry);
            }
        }

        m_mappings[month.toLower()] = mm;
    }

    emit mappingsLoaded(true, QString());
    return true;
}

bool MappingsManager::writeJsonFile(const QString& filePath)
{
    if (m_mappings.isEmpty()) {
        emit mappingsSaved(false, "No mappings to save");
        return false;
    }

    auto buildRowMapJson = [](const QMap<int, QVector<int>>& rowMap) {
        QJsonObject obj;
        for (auto it = rowMap.constBegin(); it != rowMap.constEnd(); ++it) {
            int destRow = it.key();
            const QVector<int>& srcRows = it.value();
            if (srcRows.size() == 1) {
                obj[QString::number(destRow)] = srcRows.first();
            } else {
                QJsonArray rows;
                for (int row : srcRows) {
                    rows.append(row);
                }
                QJsonObject sumObj;
                sumObj["sum"] = rows;
                obj[QString::number(destRow)] = sumObj;
            }
        }
        return obj;
    };

    const auto firstEntryForMonth = [&](const MonthMapping& mm) -> MappingEntry {
        return mm.sources.isEmpty() ? MappingEntry() : mm.sources.first();
    };

    const MappingEntry baseEntry = firstEntryForMonth(m_mappings.constBegin().value());

    QJsonObject root;
    root["source_sheet"] = baseEntry.sourceSheetTemplate.isEmpty() ? "{year}" : baseEntry.sourceSheetTemplate;
    root["source_column"] = baseEntry.sourceColumn.isEmpty() ? "C" : baseEntry.sourceColumn;
    root["dest_sheet"] = baseEntry.destSheet.isEmpty() ? "MZLZ Consolidated" : baseEntry.destSheet;

    if (!baseEntry.rowMap.isEmpty()) {
        root["rowMap"] = buildRowMapJson(baseEntry.rowMap);
    }
    if (!baseEntry.cumulativeRows.isEmpty()) {
        QJsonArray cumRows;
        for (int row : baseEntry.cumulativeRows) {
            cumRows.append(row);
        }
        root["cumulative_rows"] = cumRows;
    }

    QJsonArray monthMappings;
    for (const QString& month : MONTHS_LIST) {
        const QString key = month.toLower();
        if (!m_mappings.contains(key)) continue;
        const MonthMapping& mm = m_mappings[key];
        if (mm.sources.isEmpty()) continue;
        const MappingEntry entry = firstEntryForMonth(mm);

        QJsonObject mmObj;
        mmObj["month"] = month;
        if (!entry.sourceColumn.isEmpty()) {
            mmObj["source_column"] = entry.sourceColumn;
        }
        mmObj["dest_column"] = entry.destColumn.isEmpty() ? "G" : entry.destColumn;
        monthMappings.append(mmObj);
    }
    root["month_mappings"] = monthMappings;

    QJsonDocument doc(root);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit mappingsSaved(false, "Cannot open file for writing");
        return false;
    }

    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    emit mappingsSaved(true, QString());
    return true;
}

QString MappingsManager::cleanSheetName(const QString& sheetName) const {
    QString cleaned = sheetName;
    cleaned.replace("/", "_");
    cleaned.replace(":", "_");
    cleaned.replace("*", "_");
    cleaned.replace("?", "_");
    cleaned.replace("\"", "_");
    cleaned.replace("<", "_");
    cleaned.replace(">", "_");
    cleaned.replace("|", "_");
    cleaned.replace("\\", "_");
    return cleaned;
}
