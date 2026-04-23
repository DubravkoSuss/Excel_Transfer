#include "mappingupdater.h"
#include "../../services/excelhandler.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QDateTime>
#include <QDir>
#include <QDebug>

// ═══════════════════════════════════════════════════════════════════════════
// columnToNumber — "A"→1, "B"→2, "AA"→27, etc.
// ═══════════════════════════════════════════════════════════════════════════
int MappingUpdater::columnToNumber(const QString& col)
{
    int num = 0;
    for (const QChar& c : col.toUpper()) {
        if (c >= 'A' && c <= 'Z')
            num = num * 26 + (c.unicode() - 'A' + 1);
    }
    return num > 0 ? num : 1;
}

// ═══════════════════════════════════════════════════════════════════════════
// Helper: parse a JSON value that can be a string "B" or array ["B","C"]
// into a QStringList of column letters and QVector<int> of column numbers
// ═══════════════════════════════════════════════════════════════════════════
static void parseColumnSpec(const QJsonValue& val,
                            QStringList& letters, QVector<int>& nums)
{
    letters.clear();
    nums.clear();
    if (val.isString()) {
        QString s = val.toString().toUpper();
        letters << s;
        nums << MappingUpdater::columnToNumber(s);
    } else if (val.isArray()) {
        for (const QJsonValue& v : val.toArray()) {
            QString s = v.toString().toUpper();
            if (!s.isEmpty()) {
                letters << s;
                nums << MappingUpdater::columnToNumber(s);
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// loadConfig — read the "config" section from label_definitions.json
// ═══════════════════════════════════════════════════════════════════════════
LabelConfig MappingUpdater::loadConfig(const QString& path)
{
    LabelConfig cfg;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return cfg;

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return cfg;

    QJsonObject root = doc.object();
    QJsonObject cfgObj = root.value("config").toObject();
    if (cfgObj.isEmpty()) goto countEntries;

    if (cfgObj.contains("source_sheet"))
        cfg.sourceSheet = cfgObj.value("source_sheet").toString();
    if (cfgObj.contains("dest_sheet"))
        cfg.destSheet = cfgObj.value("dest_sheet").toString();

    // Source columns: "source_label_column" can be "B" or ["B","C"]
    if (cfgObj.contains("source_label_column")) {
        parseColumnSpec(cfgObj.value("source_label_column"),
                        cfg.sourceLabelCols, cfg.sourceLabelNums);
    } else if (cfgObj.contains("source_label_columns")) {
        parseColumnSpec(cfgObj.value("source_label_columns"),
                        cfg.sourceLabelCols, cfg.sourceLabelNums);
    } else if (cfgObj.contains("label_column")) {
        // backward compat: single column for both
        parseColumnSpec(cfgObj.value("label_column"),
                        cfg.sourceLabelCols, cfg.sourceLabelNums);
        cfg.destLabelCols = cfg.sourceLabelCols;
        cfg.destLabelNums = cfg.sourceLabelNums;
    }

    // Dest columns: "dest_label_column" can be "C" or ["B","C"]
    if (cfgObj.contains("dest_label_column")) {
        parseColumnSpec(cfgObj.value("dest_label_column"),
                        cfg.destLabelCols, cfg.destLabelNums);
    } else if (cfgObj.contains("dest_label_columns")) {
        parseColumnSpec(cfgObj.value("dest_label_columns"),
                        cfg.destLabelCols, cfg.destLabelNums);
    }

countEntries:
    // Count entries
    QJsonArray arr = root.value("pairs").toArray();
    for (const QJsonValue& v : arr) {
        QJsonObject o = v.toObject();
        if (o.value("type").toString() == "sum")
            cfg.sumCount++;
        else if (!o.value("label").toString().trimmed().isEmpty())
            cfg.simpleCount++;
    }

    return cfg;
}

// ═══════════════════════════════════════════════════════════════════════════
// normalize — strip everything except letters/digits, lowercase
// ═══════════════════════════════════════════════════════════════════════════
QString MappingUpdater::normalize(const QString& text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar& c : text) {
        if (c.isLetterOrNumber())
            out += c.toLower();
    }
    return out;
}

// ═══════════════════════════════════════════════════════════════════════════
// buildLabelIndex — scan MULTIPLE columns of a sheet, return {normalized → row}
// First occurrence wins. Columns are scanned left-to-right, all rows per column.
// ═══════════════════════════════════════════════════════════════════════════
QMap<QString, int> MappingUpdater::buildLabelIndex(
    ExcelHandler& handler, const QString& fileKey,
    const QString& sheetName, const QVector<int>& labelCols,
    int maxRow)
{
    QMap<QString, int> index;

    for (int col : labelCols) {
        for (int row = 1; row <= maxRow; ++row) {
            QVariant val = handler.getCellValue(fileKey, sheetName, row, col);
            if (val.isNull() || !val.isValid()) continue;

            QString text = val.toString().trimmed();
            if (text.isEmpty()) continue;

            QString norm = normalize(text);
            if (norm.isEmpty()) continue;

            // First occurrence wins (don't overwrite if already found)
            if (!index.contains(norm)) {
                index[norm] = row;
            }
        }
    }
    return index;
}

// ═══════════════════════════════════════════════════════════════════════════
// loadLabelDefinitions — reads the new simplified format:
//   Simple: { "label": "Landing Charges" }
//   Sum:    { "type": "sum", "dest_label": "X", "source_labels": ["A","B"] }
// ═══════════════════════════════════════════════════════════════════════════
QVector<LabelEntry> MappingUpdater::loadLabelDefinitions(const QString& path)
{
    QVector<LabelEntry> entries;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "[MappingUpdater] Cannot open label_definitions.json:" << path;
        return entries;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();

    if (err.error != QJsonParseError::NoError) {
        qWarning() << "[MappingUpdater] JSON parse error:" << err.errorString();
        return entries;
    }

    QJsonArray arr;
    if (doc.isObject())
        arr = doc.object().value("pairs").toArray();
    else if (doc.isArray())
        arr = doc.array();

    for (const QJsonValue& v : arr) {
        QJsonObject o = v.toObject();
        QString type = o.value("type").toString();
        if (type == "sum") {
            LabelEntry entry;
            entry.isSum = true;
            entry.destLabel = o.value("dest_label").toString().trimmed();
            QJsonArray srcArr = o.value("source_labels").toArray();
            for (const QJsonValue& sv : srcArr)
                entry.sourceLabels.append(sv.toString().trimmed());
            if (!entry.destLabel.isEmpty() && !entry.sourceLabels.isEmpty())
                entries.append(entry);
        } else {
            QString label = o.value("label").toString().trimmed();
            if (!label.isEmpty()) {
                LabelEntry entry;
                entry.isSum = false;
                entry.label = label;
                entries.append(entry);
            }
        }
    }

    qDebug() << "[MappingUpdater] Loaded" << entries.size() << "label entries from" << path;
    return entries;
}

// ═══════════════════════════════════════════════════════════════════════════
// findRowForLabel — search an index for a label.
//   1. Exact match on normalized text (always tried first)
//   2. "Starts with" prefix match — ONLY for labels ≥ 25 normalized chars
// Returns row number or -1 if not found.
// ═══════════════════════════════════════════════════════════════════════════
static int findRowForLabel(const QString& /*label*/,
                           const QMap<QString, int>& index,
                           const QString& normLabel)
{
    if (index.contains(normLabel))
        return index.value(normLabel);

    if (normLabel.size() >= 25) {
        for (auto it = index.begin(); it != index.end(); ++it) {
            if (it.key().startsWith(normLabel) || normLabel.startsWith(it.key()))
                return it.value();
        }
    }

    return -1;
}

// ═══════════════════════════════════════════════════════════════════════════
// updateMappings — the main function
// ═══════════════════════════════════════════════════════════════════════════
UpdateResult MappingUpdater::updateMappings(
    ExcelHandler& handler,
    const QString& sourceFile, const QString& sourceSheet,
    const QString& destFile,   const QString& destSheet,
    const QString& labelDefPath,
    const QString& mappingsJsonPath,
    const QVector<int>& srcLabelCols,
    const QVector<int>& dstLabelCols)
{
    UpdateResult result;

    // 1. Load label definitions
    QVector<LabelEntry> entries = loadLabelDefinitions(labelDefPath);
    if (entries.isEmpty()) {
        result.warnings << "No label definitions found in " + labelDefPath;
        return result;
    }
    result.total = entries.size();

    // 2. Load both Excel files
    QString srcKey = "update_src_" + QString::number(QDateTime::currentMSecsSinceEpoch());
    QString dstKey = "update_dst_" + QString::number(QDateTime::currentMSecsSinceEpoch());

    if (!handler.loadWorkbook(sourceFile, srcKey)) {
        result.warnings << QString("Failed to load source file: %1").arg(sourceFile);
        return result;
    }
    if (!handler.loadWorkbook(destFile, dstKey)) {
        result.warnings << QString("Failed to load dest file: %1").arg(destFile);
        handler.unloadWorkbook(srcKey);
        return result;
    }

    // 3. Build label indexes — scan multiple columns for each file
    QMap<QString, int> srcIndex = buildLabelIndex(handler, srcKey, sourceSheet, srcLabelCols, 3000);
    QMap<QString, int> dstIndex = buildLabelIndex(handler, dstKey, destSheet, dstLabelCols, 3000);

    qDebug() << "[MappingUpdater] Source index:" << srcIndex.size()
             << "labels (cols" << srcLabelCols << "), Dest index:" << dstIndex.size()
             << "labels (cols" << dstLabelCols << ")";

    // DEBUG: check specific cells
    {
        // Source B825 should contain "**          751180  GH Supporting staff"
        QVariant dbgVal = handler.getCellValue(srcKey, sourceSheet, 825, 2);
        qDebug() << "[MappingUpdater] DEBUG src B825 =" << dbgVal;
        // Dest C56 should contain "751180  GH Supporting staff"
        QVariant dbgVal2 = handler.getCellValue(dstKey, destSheet, 56, 3);
        qDebug() << "[MappingUpdater] DEBUG dst C56 =" << dbgVal2;
        // Dest B56 should contain "GH Supporting staff"
        QVariant dbgVal3 = handler.getCellValue(dstKey, destSheet, 56, 2);
        qDebug() << "[MappingUpdater] DEBUG dst B56 =" << dbgVal3;

        // Check if normalize matches
        QString normLabel = normalize("751180  GH Supporting staff");
        qDebug() << "[MappingUpdater] DEBUG norm label =" << normLabel;
        qDebug() << "[MappingUpdater] DEBUG srcIndex contains?" << srcIndex.contains(normLabel)
                 << "dstIndex contains?" << dstIndex.contains(normLabel);

        // Dump first 10 entries from srcIndex containing "gh"
        int cnt = 0;
        for (auto it = srcIndex.begin(); it != srcIndex.end() && cnt < 200; ++it) {
            if (it.key().contains("gh") || it.key().contains("751")) {
                qDebug() << "[MappingUpdater] DEBUG srcIdx:" << it.key() << "-> row" << it.value();
                cnt++;
            }
        }
        cnt = 0;
        for (auto it = dstIndex.begin(); it != dstIndex.end() && cnt < 200; ++it) {
            if (it.key().contains("gh") || it.key().contains("751")) {
                qDebug() << "[MappingUpdater] DEBUG dstIdx:" << it.key() << "-> row" << it.value();
                cnt++;
            }
        }
    }

    // 4. For each label entry, find rows in both files
    QJsonObject newRowMap;

    for (const LabelEntry& entry : entries) {
        if (!entry.isSum) {
            QString norm = normalize(entry.label);
            int srcRow = findRowForLabel(entry.label, srcIndex, norm);
            int dstRow = findRowForLabel(entry.label, dstIndex, norm);

            if (srcRow > 0 && dstRow > 0) {
                newRowMap[QString::number(dstRow)] = srcRow;
                result.matched++;
                result.details << QString::fromUtf8("✓ \"%1\" → source row %2, dest row %3")
                                      .arg(entry.label).arg(srcRow).arg(dstRow);
            } else {
                result.notFound++;
                QStringList missing;
                if (srcRow <= 0) missing << "source";
                if (dstRow <= 0) missing << "dest";
                result.warnings << QString("NOT FOUND in %1: \"%2\"")
                                       .arg(missing.join(" & "), entry.label);
            }
        } else {
            // SUM entry
            QString normDst = normalize(entry.destLabel);
            int dstRow = findRowForLabel(entry.destLabel, dstIndex, normDst);

            if (dstRow <= 0) {
                result.notFound++;
                result.warnings << QString("SUM dest NOT FOUND: \"%1\"").arg(entry.destLabel);
                continue;
            }

            QJsonArray sumArr;
            bool allFound = true;
            QStringList foundDetails;

            for (const QString& srcLabel : entry.sourceLabels) {
                QString normSrc = normalize(srcLabel);
                int srcRow = findRowForLabel(srcLabel, srcIndex, normSrc);
                if (srcRow > 0) {
                    sumArr.append(srcRow);
                    foundDetails << QString("\"%1\"→%2").arg(srcLabel).arg(srcRow);
                } else {
                    allFound = false;
                    sumArr.append(0);
                    foundDetails << QString("\"%1\"→NOT FOUND").arg(srcLabel);
                    result.warnings << QString("SUM source NOT FOUND: \"%1\" (dest \"%2\")")
                                           .arg(srcLabel, entry.destLabel);
                }
            }

            QJsonObject sumObj;
            sumObj["sum"] = sumArr;
            newRowMap[QString::number(dstRow)] = sumObj;

            if (allFound) {
                result.matched++;
                result.details << QString::fromUtf8("✓ SUM → dest row %1 \"%2\": [%3]")
                                      .arg(dstRow).arg(entry.destLabel)
                                      .arg(foundDetails.join(", "));
            } else {
                result.matched++;
                result.details << QString::fromUtf8("⚠ SUM (partial) → dest row %1 \"%2\": [%3]")
                                      .arg(dstRow).arg(entry.destLabel)
                                      .arg(foundDetails.join(", "));
            }
        }
    }

    handler.unloadWorkbook(srcKey);
    handler.unloadWorkbook(dstKey);

    // 5. Load existing mappings JSON
    QFile f(mappingsJsonPath);
    if (!f.open(QIODevice::ReadOnly)) {
        result.warnings << QString("Cannot read mappings JSON: %1").arg(mappingsJsonPath);
        return result;
    }
    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseErr);
    f.close();
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        result.warnings << QString("Invalid mappings JSON: %1").arg(parseErr.errorString());
        return result;
    }

    // 6. Backup old JSON
    QString backupPath = mappingsJsonPath + ".bak";
    if (QFile::exists(backupPath))
        QFile::remove(backupPath);
    QFile::copy(mappingsJsonPath, backupPath);

    // 7. Replace rowMap, save
    QJsonObject root = doc.object();
    root["rowMap"] = newRowMap;
    root["rowMap_updated"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["rowMap_matched"] = result.matched;
    root["rowMap_not_found"] = result.notFound;

    QFile outFile(mappingsJsonPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        result.warnings << QString("Cannot write mappings JSON: %1").arg(mappingsJsonPath);
        return result;
    }
    outFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    outFile.close();

    qDebug() << "[MappingUpdater] Updated" << mappingsJsonPath
             << "— matched:" << result.matched
             << "not_found:" << result.notFound;

    return result;
}