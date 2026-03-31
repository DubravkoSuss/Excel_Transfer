#include "excelhandler.h"
#include <QAxObject>
#include <QDirIterator>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QDebug>
#include <QRegularExpression>
#include <QFile>
#include <QDir>
#include <QBuffer>
#include <QElapsedTimer>
#include <QtCore/qglobal.h>
#include <QtCore/private/qzipreader_p.h>
#include <QtCore/private/qzipwriter_p.h>

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────────────────────

static QPair<QString, int> splitCellRef(const QString& ref)
{
    QRegularExpression re("^([A-Za-z]+)(\\d+)$");
    QRegularExpressionMatch match = re.match(ref);
    if (!match.hasMatch())
        return {QString(), -1};
    return {match.captured(1).toUpper(), match.captured(2).toInt()};
}

static QString buildCellRef(const QString& col, int row)
{
    return QString("%1%2").arg(col.toUpper()).arg(row);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Static data
// ─────────────────────────────────────────────────────────────────────────────

const QMap<QString, QString> ExcelHandler::MONTH_TO_NUM = {
    {"January","01"},{"February","02"},{"March","03"},{"April","04"},
    {"May","05"},{"June","06"},{"July","07"},{"August","08"},
    {"September","09"},{"October","10"},{"November","11"},{"December","12"}
};

// ─────────────────────────────────────────────────────────────────────────────
//  Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

ExcelHandler::ExcelHandler(QObject *parent) : QObject(parent) {}
ExcelHandler::~ExcelHandler() {}

// ─────────────────────────────────────────────────────────────────────────────
//  Column helpers
// ─────────────────────────────────────────────────────────────────────────────

QString ExcelHandler::staticColumnToLetter(int col)
{
    QString result;
    while (col > 0) {
        col--;
        result.prepend(QChar('A' + (col % 26)));
        col /= 26;
    }
    return result;
}

int ExcelHandler::staticLetterToColumn(const QString& letter)
{
    int col = 0;
    for (int i = 0; i < letter.length(); ++i)
        col = col * 26 + (letter[i].toUpper().toLatin1() - 'A' + 1);
    return col;
}

QString ExcelHandler::columnToLetter(int col) { return staticColumnToLetter(col); }
int ExcelHandler::letterToColumn(const QString& letter) { return staticLetterToColumn(letter); }

// No PowerShell extract/compress needed — we use QZipReader/QZipWriter directly in-memory.

// ─────────────────────────────────────────────────────────────────────────────
//  OpenXML: load shared strings from raw XML bytes
// ─────────────────────────────────────────────────────────────────────────────

static QVector<QString> parseSharedStrings(const QByteArray& data)
{
    QVector<QString> table;
    if (data.isEmpty()) return table;

    QXmlStreamReader xml(data);
    QString current;
    bool inT = false;

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            if (xml.name() == QLatin1String("si"))       { current.clear(); }
            else if (xml.name() == QLatin1String("t"))   { inT = true; }
        } else if (xml.isCharacters() && inT) {
            current += xml.text().toString();
        } else if (xml.isEndElement()) {
            if (xml.name() == QLatin1String("t"))        { inT = false; }
            else if (xml.name() == QLatin1String("si"))  { table.append(current); }
        }
    }
    return table;
}

// ─────────────────────────────────────────────────────────────────────────────
//  OpenXML: parse one sheet XML into SheetData
// ─────────────────────────────────────────────────────────────────────────────

static SheetData parseSheet(const QByteArray& data, const QString& sheetName,
                             const QVector<QString>& sharedStrings)
{
    SheetData sd;
    sd.name   = sheetName;
    sd.maxRow = 0;
    sd.maxCol = 0;

    QXmlStreamReader r(data);
    while (!r.atEnd()) {
        r.readNext();
        if (!r.isStartElement() || r.name() != QLatin1String("c"))
            continue;

        QString cellRef  = r.attributes().value("r").toString();
        QString cellType = r.attributes().value("t").toString();

        auto refParts = splitCellRef(cellRef);
        int rowNum = refParts.second;
        int colNum = ExcelHandler::staticLetterToColumn(refParts.first);

        if (rowNum > 0) sd.maxRow = qMax(sd.maxRow, rowNum);
        if (colNum > 0) sd.maxCol = qMax(sd.maxCol, colNum);

        CellData cell;
        cell.row      = rowNum;
        cell.col      = colNum;
        cell.dataType = cellType;

        while (!(r.isEndElement() && r.name() == QLatin1String("c")) && !r.atEnd()) {
            r.readNext();
            if (!r.isStartElement()) continue;
            if (r.name() == QLatin1String("v")) {
                r.readNext();
                QString raw = r.text().toString();
                if (cellType == "s") {
                    bool ok = false;
                    int si = raw.toInt(&ok);
                    cell.value = (ok && si >= 0 && si < sharedStrings.size())
                                     ? sharedStrings[si] : raw;
                } else {
                    cell.value = raw;
                }
            } else if (r.name() == QLatin1String("is")) {
                while (!(r.isEndElement() && r.name() == QLatin1String("is")) && !r.atEnd()) {
                    r.readNext();
                    if (r.isStartElement() && r.name() == QLatin1String("t")) {
                        r.readNext();
                        cell.value += r.text().toString();
                    }
                }
                cell.dataType = "str";
            } else if (r.name() == QLatin1String("f")) {
                r.readNext();
                cell.formula = r.text().toString();
            }
        }
        sd.cells.insert(cellRef, cell);
    }
    return sd;
}

// ─────────────────────────────────────────────────────────────────────────────
//  OpenXML: load workbook metadata ONLY — no sheet parsing (fast)
// ─────────────────────────────────────────────────────────────────────────────

bool ExcelHandler::loadOpenXML(const QString& filePath, WorkbookData& wb,
                               const QSet<QString>& sheetsNeeded)
{
    qDebug() << "loadOpenXML START" << filePath;
    QZipReader zip(filePath);
    if (!zip.isReadable()) {
        qWarning() << "ExcelHandler: cannot open ZIP:" << filePath;
        return false;
    }
    qDebug() << "loadOpenXML: ZIP opened, reading workbook.xml...";

    // --- workbook.xml → sheet names + sheetId map ---
    QByteArray wbData = zip.fileData("xl/workbook.xml");
    if (wbData.isEmpty()) return false;
    qDebug() << "loadOpenXML: workbook.xml size=" << wbData.size();

    QStringList sheetNames;
    QMap<int, QString> sheetIdToName;
    {
        QXmlStreamReader r(wbData);
        while (!r.atEnd()) {
            r.readNext();
            if (r.isStartElement() && r.name() == QLatin1String("sheet")) {
                QString name = r.attributes().value("name").toString();
                int id       = r.attributes().value("sheetId").toInt();
                if (!name.isEmpty()) {
                    sheetNames.append(name);
                    sheetIdToName.insert(id, name);
                }
            }
        }
    }
    if (sheetNames.isEmpty()) return false;

    // --- shared strings (small, needed for all sheets, load once) ---
    qDebug() << "loadOpenXML: loading sharedStrings.xml...";
    QByteArray ssData = zip.fileData("xl/sharedStrings.xml");
    qDebug() << "loadOpenXML: sharedStrings.xml size=" << ssData.size();
    wb.sharedStrings = parseSharedStrings(ssData);
    qDebug() << "loadOpenXML: sharedStrings count=" << wb.sharedStrings.size();

    // Cache all ZIP entries so saveOpenXML never needs to re-open the network file.
    // Only cache if this workbook is marked as a save target (isSaveTarget flag set by caller).
    if (wb.isSaveTarget) {
        qDebug() << "loadOpenXML: caching ZIP entries for save target...";
        for (const QZipReader::FileInfo& fi : zip.fileInfoList()) {
            if (!fi.isDir)
                wb.cachedZipEntries[fi.filePath] = zip.fileData(fi.filePath);
        }
        qDebug() << "loadOpenXML: cached" << wb.cachedZipEntries.size() << "entries";
    }

    // --- build sheetName → ZIP path map (no sheet parsing yet!) ---
    wb.filePath   = filePath;
    wb.sheetNames = sheetNames; // will be replaced with rels-resolved names below if available
    wb.sheets.clear();
    wb.sheetPathMap.clear();
    wb.rawSheetOverrides.clear();
    wb.modifiedSheets.clear();

    // Also build rId → sheetName from workbook.xml rels for robust mapping
    QMap<QString, QString> rIdToZipPath; // rId → xl/worksheets/sheetN.xml
    {
        QByteArray relsData = zip.fileData("xl/_rels/workbook.xml.rels");
        if (!relsData.isEmpty()) {
            QXmlStreamReader rr(relsData);
            while (!rr.atEnd()) {
                rr.readNext();
                if (rr.isStartElement() && rr.name() == QLatin1String("Relationship")) {
                    QString id     = rr.attributes().value("Id").toString();
                    QString target = rr.attributes().value("Target").toString();
                    if (target.startsWith("worksheets/"))
                        rIdToZipPath.insert(id, "xl/" + target);
                }
            }
        }
    }

    // Re-parse workbook.xml with r:id to get correct sheetName → ZIP path
    QMap<QString, QString> sheetNameToZipPath;
    {
        QXmlStreamReader r(wbData);
        while (!r.atEnd()) {
            r.readNext();
            if (r.isStartElement() && r.name() == QLatin1String("sheet")) {
                QString name = r.attributes().value("name").toString();
                QString rId  = r.attributes().value(QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships"), "id").toString();
                if (rId.isEmpty())
                    rId = r.attributes().value("r:id").toString();
                if (!name.isEmpty() && rIdToZipPath.contains(rId))
                    sheetNameToZipPath.insert(name, rIdToZipPath[rId]);
            }
        }
    }

    for (const QZipReader::FileInfo& fi : zip.fileInfoList()) {
        if (!fi.filePath.startsWith("xl/worksheets/sheet") || !fi.filePath.endsWith(".xml"))
            continue;
        QString base = QFileInfo(fi.filePath).baseName();
        int idx = base.mid(5).toInt();
        // Prefer rId-based name resolution; fall back to sheetId
        QString sheetName;
        for (auto it = sheetNameToZipPath.constBegin(); it != sheetNameToZipPath.constEnd(); ++it) {
            if (it.value() == fi.filePath) { sheetName = it.key(); break; }
        }
        if (sheetName.isEmpty())
            sheetName = sheetIdToName.value(idx, QString("Sheet%1").arg(idx));
        wb.sheetPathMap.insert(sheetName, fi.filePath);

        // Pre-parse only sheets in sheetsNeeded (if specified) — skip the rest (lazy on demand)
        if (!sheetsNeeded.isEmpty() && sheetsNeeded.contains(sheetName)) {
            QByteArray data = zip.fileData(fi.filePath);
            if (!data.isEmpty())
                wb.sheets.insert(sheetName, parseSheet(data, sheetName, wb.sharedStrings));
        }
    }

    // Use rels-resolved names if we got them (more reliable than sheetId order)
    if (!sheetNameToZipPath.isEmpty())
        wb.sheetNames = sheetNameToZipPath.keys();

    wb.isValid = true;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Lazy sheet loader — called on first access to a sheet
// ─────────────────────────────────────────────────────────────────────────────

SheetData ExcelHandler::loadSheetLazy(const WorkbookData& wb, const QString& sheetName)
{
    QString zipPath = wb.sheetPathMap.value(sheetName);
    if (zipPath.isEmpty()) {
        qWarning() << "ExcelHandler::loadSheetLazy: sheet not found:" << sheetName
                   << "— available sheets:" << wb.sheetPathMap.keys();
        return SheetData{sheetName, {}, 0, 0};
    }

    QByteArray data;
    if (!wb.cachedZipEntries.isEmpty()) {
        // Fast path: use in-memory cache — no network I/O
        data = wb.cachedZipEntries.value(zipPath);
    } else {
        // Fallback: read from network ZIP
        QZipReader zip(wb.filePath);
        data = zip.fileData(zipPath);
    }

    if (data.isEmpty()) {
        qWarning() << "ExcelHandler::loadSheetLazy: empty data for" << zipPath;
        return SheetData{sheetName, {}, 0, 0};
    }

    return parseSheet(data, sheetName, wb.sharedStrings);
}

// ─────────────────────────────────────────────────────────────────────────────
//  getSheet — returns cached sheet or lazy-loads it (caller must hold write lock)
// ─────────────────────────────────────────────────────────────────────────────

SheetData& ExcelHandler::getSheet(const QString& key, const QString& sheetName)
{
    WorkbookData& wb = m_workbooks[key];
    QString resolvedName = sheetName;
    if (!wb.sheets.contains(sheetName)) {
        for (const QString& name : wb.sheetNames) {
            if (name.trimmed().compare(sheetName.trimmed(), Qt::CaseInsensitive) == 0) {
                resolvedName = name;
                break;
            }
        }
    }

    if (!wb.sheets.contains(resolvedName)) {
        // Release write lock temporarily? No — we hold it, just load inline.
        // loadSheetLazy only reads the ZIP file (no shared state), so it's safe.
        wb.sheets[resolvedName] = loadSheetLazy(wb, resolvedName);
    }
    return wb.sheets[resolvedName];
}

// ─────────────────────────────────────────────────────────────────────────────
//  OpenXML: save workbook — fully in-memory via QZipReader + QZipWriter
//  Strategy: read all entries from original ZIP, replace only modified sheets
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
//  mergeSheetXml — patches specific cells into original sheet XML bytes
//  Works directly on raw bytes — no re-serialization, preserves everything.
//  Only the exact cells in SheetData.cells are replaced/inserted.
// ─────────────────────────────────────────────────────────────────────────────

// Forward declarations
static QByteArray applyRowHighlights(const QByteArray& sheetXml, const QSet<int>& sourceRows, int styleBlue, int styleGrey);
static QPair<int,int> injectHighlightStyles(QByteArray& stylesXml);

static QString escapeXmlValue(const QString& val)
{
    QString s = val;
    s.replace("&", "&amp;");
    s.replace("<", "&lt;");
    s.replace(">", "&gt;");
    s.replace("\"", "&quot;");
    s.replace("'", "&apos;");
    return s;
}

static QByteArray convertSharedToInline(const QByteArray& sheetXml, const QVector<QString>& sharedStrings)
{
    if (sheetXml.isEmpty() || sharedStrings.isEmpty())
        return sheetXml;

    QByteArray out;
    out.reserve(sheetXml.size());

    int pos = 0;
    const int len = sheetXml.size();
    while (pos < len) {
        int cStart = sheetXml.indexOf("<c ", pos);
        if (cStart == -1) {
            out.append(sheetXml.constData() + pos, len - pos);
            break;
        }
        out.append(sheetXml.constData() + pos, cStart - pos);
        int tagEnd = sheetXml.indexOf('>', cStart);
        if (tagEnd == -1) { out.append(sheetXml.constData() + cStart, len - cStart); break; }

        bool selfClosing = (tagEnd > 0 && sheetXml[tagEnd - 1] == '/');
        int cEnd = selfClosing ? tagEnd + 1 : sheetXml.indexOf("</c>", tagEnd + 1) + 4;
        if (cEnd < 4) { out.append(sheetXml.constData() + cStart, len - cStart); break; }

        QByteArray cellXml = sheetXml.mid(cStart, cEnd - cStart);

        // Only handle shared-string cells: t="s" or t='s'
        if (cellXml.contains("t=\"s\"") || cellXml.contains("t='s'")) {
            // Extract <v>index</v> BEFORE any replacements so offsets are valid
            int vStart = cellXml.indexOf("<v>");
            int vEnd = cellXml.indexOf("</v>");
            if (vStart != -1 && vEnd != -1 && vEnd > vStart + 3) {
                QByteArray idxBytes = cellXml.mid(vStart + 3, vEnd - (vStart + 3));
                bool ok = false;
                int idx = idxBytes.toInt(&ok);
                if (ok && idx >= 0 && idx < sharedStrings.size()) {
                    const QString& raw = sharedStrings[idx];
                    QString str = escapeXmlValue(raw);
                    bool preserve = raw.startsWith(' ') || raw.endsWith(' ');
                    QByteArray tNode = preserve
                        ? QString("<t xml:space=\"preserve\">%1</t>").arg(str).toUtf8()
                        : QString("<t>%1</t>").arg(str).toUtf8();
                    QByteArray isNode = "<is>" + tNode + "</is>";

                    // Replace <v>idx</v> with <is><t>...</t></is> FIRST (uses original offsets)
                    cellXml.replace(vStart, vEnd + 4 - vStart, isNode);

                    // Now replace the type attribute (offsets before <v> haven't shifted)
                    cellXml.replace("t=\"s\"", "t=\"inlineStr\"");
                    cellXml.replace("t='s'", "t=\"inlineStr\"");
                }
            }
        }

        out.append(cellXml);
        pos = cEnd;
    }

    return out;
}

static QByteArray stripCellWatches(const QByteArray& sheetXml)
{
    QString input = QString::fromUtf8(sheetXml);

    QRegularExpression reBlock(
        R"(<cellWatches\b[^>]*>.*?</cellWatches>)",
        QRegularExpression::DotMatchesEverythingOption
    );
    QRegularExpression reSelfClose(R"(<cellWatches\s*/>)");

    bool found = false;
    if (reBlock.match(input).hasMatch()) { input.remove(reBlock); found = true; }
    if (reSelfClose.match(input).hasMatch()) { input.remove(reSelfClose); found = true; }

    if (found)
        qInfo() << "[CELL WATCHES] Stripped <cellWatches> section from sheet XML";

    return input.toUtf8();
}

static QByteArray removeDuplicateCells(const QByteArray& xml)
{
    // === PASS 1: Find all cell refs, track last occurrence index ===
    QMap<QString, int> refCount;
    QMap<QString, int> lastOccIdx;
    int totalCells = 0;

    {
        QXmlStreamReader reader(xml);
        while (!reader.atEnd()) {
            reader.readNext();
            if (reader.isStartElement() && reader.name() == QStringLiteral("c")) {
                QString ref = reader.attributes().value("r").toString().trimmed().toUpper();
                if (!ref.isEmpty()) {
                    refCount[ref]++;
                    lastOccIdx[ref] = totalCells;
                    totalCells++;
                }
            }
        }
    }

    bool hasDuplicates = false;
    for (auto it = refCount.constBegin(); it != refCount.constEnd(); ++it) {
        if (it.value() > 1) {
            hasDuplicates = true;
            qWarning() << "[DUP DETECT]" << it.key() << "appears" << it.value() << "times";
        }
    }
    if (!hasDuplicates) return xml;

    // === PASS 2: Rebuild XML, skip earlier duplicates, keep last ===
    QByteArray output;
    QBuffer buffer(&output);
    buffer.open(QIODevice::WriteOnly);

    QXmlStreamReader reader(xml);
    QXmlStreamWriter writer(&buffer);
    writer.setAutoFormatting(false);

    int cellIndex = 0;
    bool skipCell = false;
    int skipDepth = 0;

    while (!reader.atEnd()) {
        reader.readNext();

        if (reader.isStartElement() && reader.name() == QStringLiteral("c")) {
            QString ref = reader.attributes().value("r").toString().trimmed().toUpper();
            bool isDuplicate = false;
            if (!ref.isEmpty()) {
                if (refCount[ref] > 1 && lastOccIdx[ref] != cellIndex) {
                    isDuplicate = true;
                    qWarning() << "[DUP CELL REMOVED]" << ref
                               << "occurrence" << cellIndex
                               << "(keeping" << lastOccIdx[ref] << ")";
                }
                cellIndex++;
            }
            if (isDuplicate) {
                skipCell = true;
                skipDepth = 1;
                continue;
            }
        }

        if (skipCell) {
            if (reader.isStartElement()) skipDepth++;
            else if (reader.isEndElement()) {
                skipDepth--;
                if (skipDepth <= 0) { skipCell = false; skipDepth = 0; }
            }
            continue;
        }

        switch (reader.tokenType()) {
        case QXmlStreamReader::StartDocument:
            writer.writeStartDocument(reader.documentVersion().toString(), reader.isStandaloneDocument());
            break;
        case QXmlStreamReader::EndDocument:
            writer.writeEndDocument();
            break;
        case QXmlStreamReader::StartElement:
            writer.writeStartElement(reader.namespaceUri().toString(), reader.name().toString());
            writer.writeAttributes(reader.attributes());
            break;
        case QXmlStreamReader::EndElement:
            writer.writeEndElement();
            break;
        case QXmlStreamReader::Characters:
            if (reader.isCDATA()) writer.writeCDATA(reader.text().toString());
            else writer.writeCharacters(reader.text().toString());
            break;
        case QXmlStreamReader::ProcessingInstruction:
            writer.writeProcessingInstruction(
                reader.processingInstructionTarget().toString(),
                reader.processingInstructionData().toString());
            break;
        case QXmlStreamReader::Comment:
            writer.writeComment(reader.text().toString());
            break;
        default:
            break;
        }
    }

    buffer.close();

    if (reader.hasError()) {
        qWarning() << "[XML ERROR] removeDuplicateCells:" << reader.errorString() << "— falling back";
        return xml;
    }

    qDebug() << "[DUP CLEANUP] Output size:" << output.size() << "vs original:" << xml.size();
    return output;
}

static QByteArray mergeSheetXml(const QByteArray& originalXml, const SheetData& changes)
{
    if (originalXml.isEmpty() || changes.cells.isEmpty())
        return originalXml;

    // Linear byte scan — O(n) single pass through the XML.
    const QMap<QString, CellData>& changedCells = changes.cells;
    QByteArray output;
    output.reserve(originalXml.size() + changedCells.size() * 64);
    QSet<QString> writtenCells; // track which changed cells were found in original XML
    QSet<QString> seenCells;    // track existing cells to drop duplicates

    int pos = 0;
    const int len = originalXml.size();

    while (pos < len) {
        int cStart = -1;
        for (int i = pos; i < len - 1; ++i) {
            if (originalXml[i] == '<' && originalXml[i+1] == 'c') {
                char next = (i + 2 < len) ? originalXml[i+2] : 0;
                if (next == ' ' || next == '\t' || next == '\r' || next == '\n' || next == '/' || next == '>') {
                    cStart = i;
                    break;
                }
            }
        }
        if (cStart == -1) {
            output.append(originalXml.constData() + pos, len - pos);
            break;
        }

        output.append(originalXml.constData() + pos, cStart - pos);

        int tagEnd = originalXml.indexOf('>', cStart);
        if (tagEnd == -1) {
            output.append(originalXml.constData() + cStart, len - cStart);
            break;
        }

        bool selfClosing = (tagEnd > 0 && originalXml[tagEnd - 1] == '/');
        int cEnd = selfClosing ? tagEnd + 1 : originalXml.indexOf("</c>", tagEnd + 1) + 4;
        if (cEnd < 4) {
            output.append(originalXml.constData() + cStart, len - cStart);
            break;
        }

        QByteArray openTag = originalXml.mid(cStart, tagEnd - cStart + 1);
        QString cellRef;
        int rIdx = openTag.indexOf("r=\"");
        if (rIdx != -1) {
            int rEnd = openTag.indexOf('"', rIdx + 3);
            if (rEnd != -1)
                cellRef = QString::fromLatin1(openTag.constData() + rIdx + 3, rEnd - rIdx - 3);
        } else {
            rIdx = openTag.indexOf("r='");
            if (rIdx != -1) {
                int rEnd = openTag.indexOf('\'', rIdx + 3);
                if (rEnd == -1)
                    rEnd = openTag.indexOf('>', rIdx + 3);
                if (rEnd != -1)
                    cellRef = QString::fromLatin1(openTag.constData() + rIdx + 3, rEnd - rIdx - 3);
            }
        }

        if (!cellRef.isEmpty()) {
            cellRef = cellRef.trimmed().toUpper();
            if (seenCells.contains(cellRef)) {
                qWarning() << "[DUP CELL] ref=" << cellRef;
                pos = cEnd;
                continue;
            }
            seenCells.insert(cellRef);
        }

        if (!cellRef.isEmpty() && changedCells.contains(cellRef)) {
            const CellData& cell = changedCells[cellRef];
            writtenCells.insert(cellRef);
            if (writtenCells.size() <= 3)
                qDebug() << "mergeSheetXml: replacing cell" << cellRef << "value=" << cell.value.left(30);

            QString styleAttr;
            int sIdx = openTag.indexOf("s=\"");
            if (sIdx != -1) {
                int sEnd = openTag.indexOf('"', sIdx + 3);
                if (sEnd != -1)
                    styleAttr = QString(" s=\"%1\"").arg(
                        QString::fromLatin1(openTag.constData() + sIdx + 3, sEnd - sIdx - 3));
            }

            QString safeVal = escapeXmlValue(cell.value);
            QByteArray newCell;
            if (cell.value.isEmpty() && cell.formula.isEmpty()) {
                newCell = QString("<c r=\"%1\"%2/>").arg(cellRef, styleAttr).toUtf8();
            } else if (cell.dataType == "str") {
                newCell = QString("<c r=\"%1\"%2 t=\"inlineStr\"><is><t>%3</t></is></c>")
                              .arg(cellRef, styleAttr, safeVal).toUtf8();
            } else if (!cell.formula.isEmpty()) {
                newCell = QString("<c r=\"%1\"%2><f>%3</f></c>")
                              .arg(cellRef, styleAttr, escapeXmlValue(cell.formula)).toUtf8();
            } else {
                newCell = QString("<c r=\"%1\"%2><v>%3</v></c>")
                              .arg(cellRef, styleAttr, safeVal).toUtf8();
            }
            output.append(newCell);
        } else {
            output.append(originalXml.constData() + cStart, cEnd - cStart);
        }

        pos = cEnd;
    }

    // Insert any new cells (not found in original XML) into their correct rows.
    // Group by row, then inject after the last cell of that row, or create new rows.
    QMap<int, QMap<int, const CellData*>> newCellsByRow;
    for (auto it = changedCells.constBegin(); it != changedCells.constEnd(); ++it) {
        if (!writtenCells.contains(it.key())) {
            newCellsByRow[it.value().row][it.value().col] = &it.value();
        }
    }

    if (!newCellsByRow.isEmpty()) {
        // Find </sheetData> and inject new rows before it
        int sdEnd = output.lastIndexOf("</sheetData>");
        if (sdEnd == -1) sdEnd = output.size();

        QByteArray newRows;
        for (auto rowIt = newCellsByRow.constBegin(); rowIt != newCellsByRow.constEnd(); ++rowIt) {
            int rowNum = rowIt.key();
            // Check if this row already exists in output
            QByteArray rowMarker = QString("r=\"%1\"").arg(rowNum).toUtf8();
            bool rowExists = output.contains("<row " + rowMarker) || output.contains("<row\t" + rowMarker);

            QByteArray rowCells;
            for (auto colIt = rowIt.value().constBegin(); colIt != rowIt.value().constEnd(); ++colIt) {
                const CellData* cell = colIt.value();
                QString ref = buildCellRef(ExcelHandler::staticColumnToLetter(cell->col), cell->row);
                const QString refUpper = ref.trimmed().toUpper();
                if (seenCells.contains(refUpper)) {
                    qWarning() << "[DUP CELL BLOCKED] ref=" << ref;
                    continue;
                }
                seenCells.insert(refUpper);
                QString safeVal = escapeXmlValue(cell->value);
                QByteArray cellXml;
                if (cell->value.isEmpty() && cell->formula.isEmpty()) {
                    cellXml = QString("<c r=\"%1\"/>").arg(ref).toUtf8();
                } else if (cell->dataType == "str") {
                    cellXml = QString("<c r=\"%1\" t=\"inlineStr\"><is><t>%2</t></is></c>").arg(ref, safeVal).toUtf8();
                } else if (!cell->formula.isEmpty()) {
                    cellXml = QString("<c r=\"%1\"><f>%2</f></c>").arg(ref, escapeXmlValue(cell->formula)).toUtf8();
                } else {
                    cellXml = QString("<c r=\"%1\"><v>%2</v></c>").arg(ref, safeVal).toUtf8();
                }
                rowCells.append(cellXml);
            }

            if (rowExists) {
                // Insert cells before </row> for this row number
                QByteArray closeRow = "</row>";
                // Find the specific row tag and its close
                int rowTagPos = output.indexOf("<row ");
                while (rowTagPos != -1) {
                    int rowTagEnd = output.indexOf('>', rowTagPos);
                    if (output.mid(rowTagPos, rowTagEnd - rowTagPos + 1).contains(rowMarker)) {
                        int rowClose = output.indexOf("</row>", rowTagEnd);
                        if (rowClose != -1) {
                            output.insert(rowClose, rowCells);
                        }
                        break;
                    }
                    rowTagPos = output.indexOf("<row ", rowTagPos + 1);
                }
            } else {
                newRows.append(QString("<row r=\"%1\">").arg(rowNum).toUtf8());
                newRows.append(rowCells);
                newRows.append("</row>");
            }
        }

        if (!newRows.isEmpty()) {
            output.insert(sdEnd, newRows);
        }
    }

    return output;
}

static QByteArray buildSheetXml(const SheetData& sheet)
{
    QByteArray out;
    QXmlStreamWriter w(&out);
    w.setAutoFormatting(false);
    w.writeStartDocument();
    w.writeStartElement("worksheet");
    w.writeAttribute("xmlns", "http://schemas.openxmlformats.org/spreadsheetml/2006/main");
    w.writeStartElement("sheetData");

    QMap<int, QList<CellData>> rows;
    for (const auto& cell : sheet.cells)
        rows[cell.row].append(cell);

    for (auto rowIt = rows.cbegin(); rowIt != rows.cend(); ++rowIt) {
        w.writeStartElement("row");
        w.writeAttribute("r", QString::number(rowIt.key()));
        for (const auto& cell : rowIt.value()) {
            QString ref = buildCellRef(ExcelHandler::staticColumnToLetter(cell.col), cell.row);
            w.writeStartElement("c");
            w.writeAttribute("r", ref);
            if (!cell.dataType.isEmpty()) {
                if (cell.dataType == "str") {
                    w.writeAttribute("t", "inlineStr");
                    w.writeStartElement("is");
                    w.writeTextElement("t", cell.value);
                    w.writeEndElement();
                } else {
                    w.writeAttribute("t", cell.dataType);
                    if (!cell.value.isEmpty())
                        w.writeTextElement("v", cell.value);
                }
            } else {
                if (!cell.value.isEmpty())
                    w.writeTextElement("v", cell.value);
            }
            if (!cell.formula.isEmpty())
                w.writeTextElement("f", cell.formula);
            w.writeEndElement(); // c
        }
        w.writeEndElement(); // row
    }

    w.writeEndElement(); // sheetData
    w.writeEndElement(); // worksheet
    w.writeEndDocument();
    return out;
}

static int findMaxRidInWorkbookRels(const QByteArray& relsXml)
{
    if (relsXml.isEmpty())
        return 0;

    int maxRid = 0;
    QXmlStreamReader r(relsXml);
    while (!r.atEnd()) {
        r.readNext();
        if (!r.isStartElement() || r.name() != QLatin1String("Relationship"))
            continue;

        const QString id = r.attributes().value("Id").toString();
        if (!id.startsWith(QLatin1String("rId")))
            continue;

        bool ok = false;
        const int n = id.mid(3).toInt(&ok);
        if (ok)
            maxRid = qMax(maxRid, n);
    }
    return maxRid;
}

// workbook.xml.rels contains relationships for BOTH sheets and other features (styles/theme/externalLinks/etc).
// When adding new sheets, we must choose an rId that doesn't collide with any existing relationship Id.
// Additionally, some workbooks can already contain duplicate relationship Ids; Excel may "repair" those by
// effectively dropping one target (often making the affected sheet appear blank). This function repairs the
// common case where a worksheet relationship Id collides with a non-worksheet relationship Id.
static bool repairDuplicateWorksheetRids(QByteArray& workbookXml, QByteArray& relsXml, int& maxRid)
{
    if (workbookXml.isEmpty() || relsXml.isEmpty())
        return false;

    struct Rel {
        QString id;
        QString type;
        QString target;
        QString targetMode;
        int idNum = 0;
        bool idOk = false;
        bool isWorksheet = false;
    };

    QVector<Rel> rels;
    QHash<int, QVector<int>> byId;
    QSet<int> used;

    {
        QXmlStreamReader r(relsXml);
        while (!r.atEnd()) {
            r.readNext();
            if (!r.isStartElement() || r.name() != QLatin1String("Relationship"))
                continue;

            Rel rel;
            const auto a = r.attributes();
            rel.id = a.value("Id").toString();
            rel.type = a.value("Type").toString();
            rel.target = a.value("Target").toString();
            rel.targetMode = a.value("TargetMode").toString();
            rel.isWorksheet = rel.type.contains(QLatin1String("/worksheet"));

            if (rel.id.startsWith(QLatin1String("rId"))) {
                bool ok = false;
                rel.idNum = rel.id.mid(3).toInt(&ok);
                rel.idOk = ok;
                if (ok) {
                    byId[rel.idNum].append(rels.size());
                    used.insert(rel.idNum);
                    maxRid = qMax(maxRid, rel.idNum);
                }
            }

            rels.append(rel);
        }

        if (r.hasError()) {
            qWarning() << "saveOpenXML: cannot parse workbook.xml.rels:" << r.errorString();
            return false;
        }
    }

    // Limit workbook.xml edits to the <sheets>...</sheets> block only (avoid touching externalReferences).
    const int sheetsTagStart = workbookXml.indexOf("<sheets");
    const int sheetsTagEnd = workbookXml.indexOf("</sheets>", sheetsTagStart);
    if (sheetsTagStart < 0 || sheetsTagEnd < 0)
        return false;

    const int sheetsLen = sheetsTagEnd - sheetsTagStart + 9;
    QByteArray sheetsBlock = workbookXml.mid(sheetsTagStart, sheetsLen);

    bool changed = false;

    for (auto it = byId.constBegin(); it != byId.constEnd(); ++it) {
        const int oldNum = it.key();
        const QVector<int>& idxs = it.value();
        if (idxs.size() <= 1)
            continue;

        QVector<int> ws;
        bool hasNonWs = false;
        for (int i : idxs) {
            if (rels[i].isWorksheet)
                ws.append(i);
            else
                hasNonWs = true;
        }

        if (!hasNonWs || ws.isEmpty())
            continue;

        // Safe/common case: exactly one worksheet relationship collides with something else (e.g. externalLink).
        if (ws.size() != 1) {
            qWarning() << "saveOpenXML: duplicate relationship Id rId" << oldNum
                       << "has" << ws.size() << "worksheet entries — skipping repair";
            continue;
        }

        const QByteArray needle = QByteArray("r:id=\"rId") + QByteArray::number(oldNum) + "\"";
        const int occurrences = sheetsBlock.count(needle);
        if (occurrences != 1) {
            qWarning() << "saveOpenXML: expected 1 sheet reference to" << needle
                       << "but found" << occurrences << "— skipping repair";
            continue;
        }

        int newNum = maxRid + 1;
        while (used.contains(newNum))
            ++newNum;
        used.insert(newNum);
        maxRid = qMax(maxRid, newNum);

        rels[ws[0]].id = QString("rId%1").arg(newNum);
        rels[ws[0]].idNum = newNum;
        rels[ws[0]].idOk = true;

        const QByteArray replacement = QByteArray("r:id=\"rId") + QByteArray::number(newNum) + "\"";
        sheetsBlock.replace(needle, replacement);

        qWarning() << "saveOpenXML: repaired duplicate worksheet relationship rId" << oldNum
                   << "-> rId" << newNum << "(target=" << rels[ws[0]].target << ")";
        changed = true;
    }

    if (!changed)
        return false;

    workbookXml.replace(sheetsTagStart, sheetsLen, sheetsBlock);

    // Rebuild relsXml with updated IDs (keeps it valid even if source had duplicate Ids).
    QByteArray out;
    QXmlStreamWriter w(&out);
    w.setAutoFormatting(false);
    w.writeStartDocument();
    w.writeStartElement("Relationships");
    w.writeDefaultNamespace("http://schemas.openxmlformats.org/package/2006/relationships");
    for (const Rel& rel : rels) {
        w.writeEmptyElement("Relationship");
        if (!rel.id.isEmpty()) w.writeAttribute("Id", rel.id);
        if (!rel.type.isEmpty()) w.writeAttribute("Type", rel.type);
        if (!rel.target.isEmpty()) w.writeAttribute("Target", rel.target);
        if (!rel.targetMode.isEmpty()) w.writeAttribute("TargetMode", rel.targetMode);
    }
    w.writeEndElement();
    w.writeEndDocument();

    relsXml = out;
    return true;
}

bool ExcelHandler::saveOpenXML(const QString& filePath, const WorkbookData& wb)
{
    qDebug() << "saveOpenXML [1] START" << filePath;
    qDebug() << "saveOpenXML START" << filePath;
    qDebug() << "saveOpenXML: cached entries=" << wb.cachedZipEntries.size();

    // Use cached ZIP entries if available — avoids re-opening the network file
    // which can crash with 0xC0000005 on large xlsm files.
    QByteArray wbXmlRaw;
    if (!wb.cachedZipEntries.isEmpty()) {
        wbXmlRaw = wb.cachedZipEntries.value("xl/workbook.xml");
    } else {
        // Fallback: open network file (may crash on large files)
        qWarning() << "saveOpenXML: no cached ZIP entries — falling back to network read";
        QZipReader reader(wb.filePath);
        if (!reader.isReadable()) {
            qWarning() << "ExcelHandler::saveOpenXML: cannot open" << wb.filePath;
            return false;
        }
        wbXmlRaw = reader.fileData("xl/workbook.xml");
    }
    qDebug() << "saveOpenXML: workbook.xml size=" << wbXmlRaw.size();
    qDebug() << "saveOpenXML [2] workbook.xml read OK";
    // Strip corrupted #REF! definedNames that Excel accumulates over time.
    // These have names like "\0" and point to #REF! — completely safe to remove.
    // This runs on every save to prevent the 3MB bloat from recurring.
    {
        int dnStart = wbXmlRaw.indexOf("<definedNames>");
        int dnEnd   = wbXmlRaw.indexOf("</definedNames>");
        if (dnStart >= 0 && dnEnd > dnStart) {
            QByteArray block = wbXmlRaw.mid(dnStart + 14, dnEnd - dnStart - 14);
            // Only strip if majority are #REF! entries (corrupted)
            int refCount  = block.count("#REF!");
            int nameCount = block.count("<definedName ");
            if (nameCount > 0 && refCount * 2 >= nameCount) {
                // More than 50% are #REF! — strip the whole block
                wbXmlRaw.remove(dnStart, dnEnd - dnStart + 15);
                qDebug() << "saveOpenXML: stripped" << nameCount
                         << "corrupted definedNames entries (" << refCount << "#REF!)";
            }
        }
    }

    // Note: workbook.xml may be 3MB+ but we never rebuild it from scratch.
    // We only modify it when new sheets are added (rare), by doing a targeted
    // QByteArray::replace("</sheets>", ...) which is safe on large buffers.

    // Build sheetName → sheetIndex from workbook.xml.
    // IMPORTANT: workbook.xml may be 3MB+ single-line XML — QXmlStreamReader crashes on it
    // (fixed-size line buffer overflow on newline-free XML, Qt known issue).
    // Solution: extract only the <sheets>...</sheets> block (~2KB) and parse with QRegularExpression.
    QMap<QString, int> nameToIdx;
    int maxSheetId = 0;
    int maxRid = 0;
    {
        int sheetsTagStart = wbXmlRaw.indexOf("<sheets");
        int sheetsTagEnd   = wbXmlRaw.indexOf("</sheets>", sheetsTagStart);
        if (sheetsTagStart < 0 || sheetsTagEnd < 0) {
            qWarning() << "saveOpenXML: <sheets> block not found in workbook.xml";
            return false;
        }

        QByteArray sheetsBlock = wbXmlRaw.mid(sheetsTagStart, sheetsTagEnd - sheetsTagStart + 9);
        qDebug() << "saveOpenXML: sheetsBlock size=" << sheetsBlock.size()
                 << "(full wbXml=" << wbXmlRaw.size() << ")";

        // Use QRegularExpression instead of QXmlStreamReader to avoid line-buffer crash.
        // NOTE: Avoid C++ raw string literals here because patterns like ..."... can accidentally
        // terminate the raw string delimiter and break compilation.
        static const QRegularExpression sheetTagRx(QStringLiteral("<sheet\\b[^/]*/?>"));
        static const QRegularExpression nameRx(QStringLiteral("\\bname=\"([^\"]*)\""));
        static const QRegularExpression idRx(QStringLiteral("\\bsheetId=\"(\\d+)\""));
        static const QRegularExpression ridRx(QStringLiteral("\\br:id=\"rId(\\d+)\""));

        const QString sheetsStr = QString::fromUtf8(sheetsBlock);
        auto it = sheetTagRx.globalMatch(sheetsStr);
        while (it.hasNext()) {
            const QString tag = it.next().captured(0);
            auto nm = nameRx.match(tag);   QString name = nm.hasMatch() ? nm.captured(1) : QString();
            auto im = idRx.match(tag);     int sid = im.hasMatch() ? im.captured(1).toInt() : 0;
            auto rm = ridRx.match(tag);    int rid = rm.hasMatch() ? rm.captured(1).toInt() : 0;
            if (!name.isEmpty() && sid > 0) {
                nameToIdx.insert(name, sid);
                maxSheetId = qMax(maxSheetId, sid);
            }
            maxRid = qMax(maxRid, rid);
        }
        qDebug() << "saveOpenXML: parsed" << nameToIdx.size() << "sheets"
                 << "maxSheetId=" << maxSheetId << "maxRid=" << maxRid;
    }

    // Only rewrite sheets that were actually modified — merge changes into original XML
    QMap<QString, QByteArray> replacements; // zip path → new XML bytes
    QByteArray workbookXml = wbXmlRaw; // already read above (deduplicated)
    // Use cached entries where available, fall back to reading from ZIP if needed
    auto getEntry = [&](const QString& path) -> QByteArray {
        if (!wb.cachedZipEntries.isEmpty())
            return wb.cachedZipEntries.value(path);
        QZipReader r(wb.filePath);
        return r.isReadable() ? r.fileData(path) : QByteArray();
    };
    QByteArray relsXml = getEntry("xl/_rels/workbook.xml.rels");
    QByteArray contentTypesXml = getEntry("[Content_Types].xml");

    // IMPORTANT: relationship IDs (rIdN) are shared across sheets AND other workbook relationships
    // (styles/theme/externalLinks/etc). If we only scan <sheets>, we can pick an rId that collides
    // with an existing relationship, producing duplicate Ids in workbook.xml.rels. Excel will then
    // "repair" the file and sheets may appear blank.
    const int relsMaxRid = findMaxRidInWorkbookRels(relsXml);
    maxRid = qMax(maxRid, relsMaxRid);

    // Repair existing duplicate rIds (common in already-corrupted workbooks).
    // This updates BOTH workbookXml (sheet r:id) and relsXml.
    repairDuplicateWorksheetRids(workbookXml, relsXml, maxRid);

    // relsXml and contentTypesXml are only written to replacements when new sheets are added.
    // No deduplication needed — they are already clean from the source file.

    // If any sheet has highlight rows, inject styles once
    int blueXfIdx = -1, greyXfIdx = -1;
    bool needsHighlight = !wb.highlightSourceRows.isEmpty();
    QByteArray stylesXml;
    if (needsHighlight) {
        stylesXml = getEntry("xl/styles.xml");
        if (!stylesXml.isEmpty()) {
            auto [bi, gi] = injectHighlightStyles(stylesXml);
            blueXfIdx = bi;
            greyXfIdx = gi;
            if (blueXfIdx >= 0)
                replacements["xl/styles.xml"] = stylesXml;
        }
    }

    // Write all modified sheets + raw overrides
    QSet<QString> sheetsToWrite = QSet<QString>(wb.modifiedSheets.begin(), wb.modifiedSheets.end());
    for (auto it = wb.rawSheetOverrides.constBegin(); it != wb.rawSheetOverrides.constEnd(); ++it)
        sheetsToWrite.insert(it.key());

    for (const QString& rawSheetName : sheetsToWrite) {
        QString sheetName = rawSheetName.trimmed();
        if (sheetName.isEmpty()) {
            qWarning() << "saveOpenXML: empty sheet name in modified set";
            continue;
        }

        int idx = nameToIdx.value(sheetName, -1);
        QString zipPath = wb.sheetPathMap.value(sheetName);

        if (idx == -1 || zipPath.isEmpty()) {
            // Try case-insensitive match to avoid creating duplicate sheets
            for (auto it = nameToIdx.constBegin(); it != nameToIdx.constEnd(); ++it) {
                if (it.key().trimmed().compare(sheetName, Qt::CaseInsensitive) == 0) {
                    sheetName = it.key();
                    idx = it.value();
                    zipPath = wb.sheetPathMap.value(sheetName);
                    break;
                }
            }
        }

        // New sheet not in workbook yet — create a new sheet part
        if (idx == -1 || zipPath.isEmpty()) {
            idx = ++maxSheetId;
            int newRid = ++maxRid;
            zipPath = QString("xl/worksheets/sheet%1.xml").arg(idx);

            // Insert sheet node into workbook.xml (after specified sheet if configured)
            QString sheetNode = QString("<sheet name=\"%1\" sheetId=\"%2\" r:id=\"rId%3\"/>")
                                    .arg(sheetName).arg(idx).arg(newRid);

            // TEMP: disable sheet arranging due to crash; always append at end
            workbookXml.replace("</sheets>", sheetNode.toUtf8() + "</sheets>");

            // Insert relationship into workbook.xml.rels
            QString relNode = QString("<Relationship Id=\"rId%1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet%2.xml\"/>")
                                  .arg(newRid).arg(idx);
            relsXml.replace("</Relationships>", relNode.toUtf8() + "</Relationships>");

            // Insert content type override
            QString ctNode = QString("<Override PartName=\"/xl/worksheets/sheet%1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>")
                                 .arg(idx);
            contentTypesXml.replace("</Types>", ctNode.toUtf8() + "</Types>");

            // Keep sheetPathMap in sync for future saves
            const_cast<WorkbookData&>(wb).sheetPathMap[sheetName] = zipPath;
        }

        // Raw overrides take precedence (copyFullSheet)
        if (wb.rawSheetOverrides.contains(sheetName)) {
            QByteArray xml = wb.rawSheetOverrides.value(sheetName);

            // IMPORTANT: wb is const; QMap::operator[] on a const map returns a temporary copy.
            // Never take begin()/end() from that temporary — it will crash/UB.
            if (blueXfIdx >= 0 && greyXfIdx >= 0) {
                auto rowsIt = wb.highlightSourceRows.constFind(sheetName);
                if (rowsIt != wb.highlightSourceRows.constEnd()) {
                    const QVector<int>& rows = rowsIt.value();
                    const QSet<int> srcSet(rows.cbegin(), rows.cend());
                    xml = applyRowHighlights(xml, srcSet, blueXfIdx, greyXfIdx);
                }
            }

            replacements.insert(zipPath, xml);
            continue;
        }

        if (!wb.sheets.contains(sheetName)) {
            qWarning() << "saveOpenXML: sheet not in cache" << sheetName << "(skipping)";
            continue;
        }

        QByteArray originalXml = getEntry(zipPath);
        const SheetData& fullSheet = wb.sheets[sheetName];

        // Build a SheetData containing ONLY dirty cells (explicitly written via setCellValue/setCellFormula).
        // The full sheet may have 100k+ cells from lazy loading — we must not merge all of them.
        SheetData changesOnly;
        changesOnly.name = sheetName;
        if (wb.dirtyCells.contains(sheetName)) {
            const QSet<QString>& dirty = wb.dirtyCells[sheetName];
            for (const QString& ref : dirty) {
                if (fullSheet.cells.contains(ref)) {
                    CellData cell = fullSheet.cells[ref];
                    cell.formula = ""; // value was explicitly set — clear any loaded formula
                    changesOnly.cells.insert(ref, cell);
                }
            }
        }

        qDebug() << "saveOpenXML: processing sheet" << sheetName
                 << "zipPath=" << zipPath
                 << "originalXml size=" << originalXml.size()
                 << "dirty cells=" << changesOnly.cells.size();

        if (originalXml.isEmpty()) {
            replacements.insert(zipPath, buildSheetXml(changesOnly));
            qDebug() << "saveOpenXML: built new sheet XML";
        } else {
            QByteArray merged = stripCellWatches(mergeSheetXml(originalXml, changesOnly));

            // Dump ALL occurrences of JE10–JE17 for inspection
            {
                QFile dump("C:/Users/dposavac/Desktop/exceltransfer_JE10_dump.txt");
                if (dump.open(QIODevice::WriteOnly)) {
                    int searchPos = 0;
                    int count = 0;
                    while (searchPos < merged.size()) {
                        int jePos = merged.indexOf("JE1", searchPos);
                        if (jePos == -1) break;
                        int start = qMax(0, jePos - 50);
                        int len = qMin(200, merged.size() - start);
                        dump.write(QByteArray("\n--- OCCURRENCE ") + QByteArray::number(++count) + QByteArray(" ---\n"));
                        dump.write(merged.mid(start, len));
                        searchPos = jePos + 1;
                    }
                    dump.write(QByteArray("\nTotal occurrences of JE1x: ") + QByteArray::number(count));
                    dump.close();
                    qDebug() << "[JE10 DUMP] Total JE1x occurrences:" << count;
                }
            }

            // Deduplicate using regex (same pattern as diagnostic — guaranteed to catch all variants)
            {
                // Match ONLY actual <c r="..."> cell elements, not formula text or cellWatch
                QRegularExpression cellRe("(?<![A-Za-z])<c\\s[^>]*r=([\"'])([A-Z]+[0-9]+)\\1[^>]*(?:/>|>.*?</c>)",
                                         QRegularExpression::DotMatchesEverythingOption);
                QString input = QString::fromUtf8(merged);
                struct CellMatch { qsizetype start, end; QString full, ref; };
                QList<CellMatch> matches;
                QMap<QString, int> lastIdx;
                auto it = cellRe.globalMatch(input);
                while (it.hasNext()) {
                    auto m = it.next();
                    QString ref = m.captured(2).trimmed().toUpper();
                    matches.append({m.capturedStart(), m.capturedEnd(), m.captured(0), ref});
                    lastIdx[ref] = matches.size() - 1;
                }
                if (!matches.isEmpty()) {
                    QString out;
                    out.reserve(input.size());
                    int lastPos = 0, dupes = 0;
                    for (int i = 0; i < matches.size(); ++i) {
                        const auto& cm = matches[i];
                        out.append(input.mid(lastPos, cm.start - lastPos));
                        if (lastIdx[cm.ref] == i) {
                            out.append(cm.full);
                        } else {
                            dupes++;
                            qWarning() << "[DUP CELL REMOVED]" << cm.ref;
                        }
                        lastPos = cm.end;
                    }
                    out.append(input.mid(lastPos));
                    if (dupes > 0) {
                        qInfo() << "[DUP CELL] Removed" << dupes << "duplicates";
                        merged = out.toUtf8();
                    }
                }
            }

            qDebug() << "saveOpenXML: merged size=" << merged.size()
                 << "dirty cells=" << changesOnly.cells.size()
                 << "sample cells:" << changesOnly.cells.keys().mid(0, 5)
                 << "inserting into replacements...";
            replacements.insert(zipPath, merged);
            qDebug() << "saveOpenXML: inserted into replacements OK";
        }
    }

    bool sheetsChanged = (workbookXml != wbXmlRaw); // workbookXml was modified by </sheets> replacement

    // Ensure formulas recalc on open (prevents zeroed formula results until manual edit)
    if (!workbookXml.isEmpty()) {
        QRegularExpression calcRe("<calcPr[^>]*/>");
        QString wbStr = QString::fromUtf8(workbookXml);
        if (calcRe.match(wbStr).hasMatch()) {
            wbStr.replace(calcRe, "<calcPr calcId=\"0\" fullCalcOnLoad=\"1\"/>");
        } else {
            int idx = wbStr.indexOf("</workbook>");
            if (idx != -1) {
                wbStr.insert(idx, "<calcPr calcId=\"0\" fullCalcOnLoad=\"1\"/>");
            }
        }
        workbookXml = wbStr.toUtf8();
    }

    const bool workbookChanged = (workbookXml != wbXmlRaw);
    if (workbookChanged) {
        replacements["xl/workbook.xml"] = workbookXml;
    }

    // Only replace rels/contentTypes if new sheets were added.
    if (sheetsChanged) {
        if (!relsXml.isEmpty()) replacements["xl/_rels/workbook.xml.rels"] = relsXml;
        if (!contentTypesXml.isEmpty()) replacements["[Content_Types].xml"] = contentTypesXml;
    }
    qDebug() << "saveOpenXML: sheetsChanged=" << sheetsChanged
             << "workbookChanged=" << workbookChanged
             << "replacements=" << replacements.size();
    qDebug() << "saveOpenXML [3] replacements built";

    // Strip calcChain when any sheet was modified to avoid stale references
    if (!sheetsToWrite.isEmpty()) {
        replacements.remove("xl/calcChain.xml");
        if (!contentTypesXml.isEmpty()) {
            QString ct = QString::fromUtf8(contentTypesXml);
            QRegularExpression ctRe("<Override[^>]*PartName=\"/xl/calcChain.xml\"[^>]*/?>");
            ct.remove(ctRe);
            contentTypesXml = ct.toUtf8();
            replacements["[Content_Types].xml"] = contentTypesXml;
        }
    }

    // Fix content type for macro-enabled workbooks (.xlsm):
    // Ensure vbaProject content type is preserved and workbook uses xlsm content type.
    // Only patch [Content_Types].xml if it was already modified (new sheet added).
    if ((wb.filePath.endsWith(".xlsm", Qt::CaseInsensitive) ||
         filePath.endsWith(".xlsm", Qt::CaseInsensitive)) &&
        replacements.contains("[Content_Types].xml")) {
        QByteArray& ct = replacements["[Content_Types].xml"];
        // Replace xlsx workbook content type with xlsm if present
        ct.replace(
            "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml",
            "application/vnd.ms-excel.sheet.macroEnabled.main+xml");
        // Restore vbaProject content type entry if it was stripped out
        if (!ct.contains("vbaProject") && contentTypesXml.contains("vbaProject")) {
            int vStart = contentTypesXml.indexOf("vbaProject");
            if (vStart != -1) {
                int ovStart = contentTypesXml.lastIndexOf('<', vStart);
                int ovEnd   = contentTypesXml.indexOf("/>", vStart);
                if (ovStart >= 0 && ovEnd > ovStart)
                    ct.replace("</Types>",
                        contentTypesXml.mid(ovStart, ovEnd - ovStart + 2) + "</Types>");
            }
        }
    }

    // Write all entries to a new ZIP file.
    // If we have cached entries, use them directly (no network I/O needed).
    // Otherwise fall back to copying from the network file.
    // Write patch to local temp file first, then copy to network share (more reliable than rename on L:)
    QString patchPath = QDir::tempPath() + "/exceltransfer_patch.xlsx";
    QFile::remove(patchPath);
    {
        QFile patchFile(patchPath);
        if (!patchFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            qWarning() << "ExcelHandler::saveOpenXML: cannot write patch file" << patchPath;
            return false;
        }
        qDebug() << "saveOpenXML [4] patch file opened" << patchPath;

        QZipWriter writer(&patchFile);
        writer.setCompressionPolicy(QZipWriter::AutoCompress);

        QSet<QString> written;

        if (!wb.cachedZipEntries.isEmpty()) {
            // Fast path: use cached ZIP entries — no network I/O at all
            qDebug() << "saveOpenXML: writing" << wb.cachedZipEntries.size() << "cached entries, replacements=" << replacements.size();
            int entryCount = 0;
            for (auto it = wb.cachedZipEntries.constBegin(); it != wb.cachedZipEntries.constEnd(); ++it) {
                const QString& entryPath = it.key();
                if (replacements.contains(entryPath)) {
                    qDebug() << "saveOpenXML: writing replacement" << entryPath << "size=" << replacements[entryPath].size();
                    writer.addFile(entryPath, replacements[entryPath]);
                    written.insert(entryPath);
                } else {
                    writer.addFile(entryPath, it.value());
                }
                entryCount++;
                if (entryCount % 50 == 0)
                    qDebug() << "saveOpenXML: wrote" << entryCount << "entries so far...";
            }
            qDebug() << "saveOpenXML: finished writing" << entryCount << "entries";
        } else {
            // Slow path: read from network ZIP (fallback for non-save-target workbooks)
            QString tmpPath = filePath + ".rovodev_tmp";
            QFile::remove(tmpPath);
            if (!QFile::copy(wb.filePath, tmpPath)) {
                qWarning() << "saveOpenXML: cannot copy source to temp" << wb.filePath;
                return false;
            }
            QZipReader tmpReader(tmpPath);
            if (!tmpReader.isReadable()) {
                QFile::remove(tmpPath);
                return false;
            }
            for (const QZipReader::FileInfo& fi : tmpReader.fileInfoList()) {
                if (fi.isDir) {
                    writer.addDirectory(fi.filePath);
                } else if (replacements.contains(fi.filePath)) {
                    writer.addFile(fi.filePath, replacements[fi.filePath]);
                    written.insert(fi.filePath);
                } else {
                    writer.addFile(fi.filePath, tmpReader.fileData(fi.filePath));
                }
            }
            tmpReader.close();
            QFile::remove(tmpPath);
        }

        // Add new entries not in original (new sheets from copyFullSheet)
        for (auto it = replacements.constBegin(); it != replacements.constEnd(); ++it) {
            if (!written.contains(it.key()))
                writer.addFile(it.key(), it.value());
        }
        writer.close();
        qDebug() << "saveOpenXML [5] QZipWriter closed";
        patchFile.close();
        qDebug() << "saveOpenXML [6] patch file closed";
    }

    // Atomically replace target
    // Check if destination is locked/open in Excel (fail fast with warning)
    {
        QFile lockProbe(filePath);
        if (lockProbe.exists()) {
            if (!lockProbe.open(QIODevice::ReadWrite)) {
                qWarning() << "ExcelHandler::saveOpenXML: destination locked/open" << filePath;
                qWarning() << "saveOpenXML: patch left at" << patchPath;
                return false;
            }
            lockProbe.close();
        }
    }

    // Remove existing target first to avoid partial overwrites on network shares
    if (!QFile::remove(filePath)) {
        qWarning() << "ExcelHandler::saveOpenXML: cannot remove target (locked?)" << filePath;
        qWarning() << "saveOpenXML: patch left at" << patchPath;
        return false;
    }
    qDebug() << "saveOpenXML [7] original removed";

    if (!QFile::copy(patchPath, filePath)) {
        qWarning() << "ExcelHandler::saveOpenXML: copy to target failed" << patchPath << "->" << filePath;
        qWarning() << "saveOpenXML: patch left at" << patchPath;
        return false;
    }
    QFile::remove(patchPath);
    qDebug() << "saveOpenXML [8] copy complete, temp removed";

    qDebug() << "saveOpenXML END" << filePath;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Public API — all OpenXML, no COM
// ─────────────────────────────────────────────────────────────────────────────

bool ExcelHandler::loadWorkbook(const QString& filePath, const QString& key,
                                const QSet<QString>& sheetsNeeded)
{
    if (!QFile::exists(filePath)) {
        qWarning() << "ExcelHandler::loadWorkbook: file not found:" << filePath;
        return false;
    }

    // Parse outside the lock — each thread has its own QZipReader, fully safe
    WorkbookData wb;
    // Mark as save target if key ends with _cost_control — caches all ZIP entries upfront
    // so saveOpenXML never needs to re-open the network file.
    wb.isSaveTarget = key.endsWith("_cost_control");
    if (!loadOpenXML(filePath, wb, sheetsNeeded)) {
        qWarning() << "ExcelHandler::loadWorkbook: OpenXML parse failed for" << filePath;
        return false;
    }

    qDebug() << "ExcelHandler: loaded" << key << "—" << wb.sheetNames.size()
             << "sheets (pre-loaded:" << wb.sheets.size() << ") from" << filePath;

    // Only lock for the map write
    QWriteLocker locker(&m_lock);
    m_workbooks[key] = std::move(wb);
    return true;
}

bool ExcelHandler::loadWorkbookDataOnly(const QString& filePath, const QString& key)
{
    return loadWorkbook(filePath, key);
}

bool ExcelHandler::saveWorkbook(const QString& key, const QString& outputPath)
{
    QElapsedTimer timer;
    timer.start();
    qDebug() << "saveWorkbook START" << key << outputPath;
    QWriteLocker locker(&m_lock);

    if (!m_workbooks.contains(key)) {
        qWarning() << "ExcelHandler::saveWorkbook: key not found:" << key;
        return false;
    }

    // Diagnostic: verify WorkbookData integrity before save
    qDebug() << "saveWorkbook: sheets=" << m_workbooks[key].sheets.size()
             << "sheetPathMap=" << m_workbooks[key].sheetPathMap.size()
             << "filePath=" << m_workbooks[key].filePath
             << "modified=" << m_workbooks[key].modifiedSheets.size();

    // Copy filePath out before taking reference — avoids any COW issues
    const QString srcFilePath = m_workbooks[key].filePath;
    const QString dest = outputPath.isEmpty() ? srcFilePath : outputPath;

    // If destination differs from source, copy first then patch
    if (dest != srcFilePath && !QFile::copy(srcFilePath, dest)) {
        qWarning() << "ExcelHandler::saveWorkbook: copy failed" << srcFilePath << "->" << dest;
        return false;
    }

    // Re-fetch reference after potential map modifications above
    const WorkbookData& wb = m_workbooks[key];
    qDebug() << "saveWorkbook: wb.filePath=" << wb.filePath << "valid=" << wb.isValid;

    bool ok = saveOpenXML(dest, wb);

    // After a successful save, update the cached workbook path to the new file
    // so future saves read the latest state (prevents overwriting new sheets)
    if (ok) {
        if (m_workbooks.contains(key))
            m_workbooks[key].filePath = dest;
    } else {
        qWarning() << "saveWorkbook failed for" << key << "->" << dest;
    }

    qDebug() << "saveWorkbook END" << key << "elapsed" << timer.elapsed() << "ms";
    return ok;
}

void ExcelHandler::unloadWorkbook(const QString& key)
{
    QWriteLocker locker(&m_lock);
    m_workbooks.remove(key);
}

void ExcelHandler::unloadAll()
{
    QWriteLocker locker(&m_lock);
    qDebug() << "[UNLOAD ALL]" << m_workbooks.size() << "workbooks cleared from cache";
    m_workbooks.clear();
}

bool ExcelHandler::recalcWithCOM(const QString& filePath, QString* errorOut)
{
    // Uses Qt ActiveX (QAxObject) to open the file in Excel, accept any repair
    // dialog automatically, force full recalculation, then save and close.
    // This ensures formula-based cells (like TRAFFIC mott Q column) are correct
    // on disk before we read them via OpenXML.

    qDebug() << "[COM-RECALC] Starting for:" << filePath;

    QAxObject excel("Excel.Application");
    if (excel.isNull()) {
        QString err = "COM: Excel.Application not available — is Excel installed?";
        qWarning() << "[COM-RECALC]" << err;
        if (errorOut) *errorOut = err;
        return false;
    }

    // Keep Excel hidden — no visible window
    excel.setProperty("Visible", false);

    // CRITICAL: suppress ALL alert/dialog popups including:
    //   - "We found a problem with some content..." (repair dialog)
    //   - "Do you want to save changes?"
    //   - "This file contains macros..."
    excel.setProperty("DisplayAlerts", false);

    // AutomationSecurity = msoAutomationSecurityForceDisable (3)
    // This disables macro prompts without blocking the file opening
    excel.setProperty("AutomationSecurity", 3);

    QAxObject* workbooks = excel.querySubObject("Workbooks");
    if (!workbooks) {
        QString err = "COM: Could not access Workbooks collection";
        qWarning() << "[COM-RECALC]" << err;
        if (errorOut) *errorOut = err;
        excel.dynamicCall("Quit()");
        return false;
    }

    // Open the workbook via Workbooks.Open with CorruptLoad=xlRepairFile(2).
    // dynamicCall only supports up to 8 QVariant args, so we use the
    // QList<QVariant> overload which accepts any number of parameters.
    QAxObject* wb = nullptr;
    {
        QList<QVariant> args;
        args << QVariant(filePath)  // Filename
             << QVariant(0)         // UpdateLinks = 0 (no external link prompts)
             << QVariant(false)     // ReadOnly = false
             << QVariant(5)         // Format
             << QVariant("")        // Password
             << QVariant("")        // WriteResPassword
             << QVariant(false)     // IgnoreReadOnlyRecommended
             << QVariant(2)         // Origin = xlWindows
             << QVariant("")        // Delimiter
             << QVariant(false)     // Editable
             << QVariant(false)     // Notify
             << QVariant(0)         // Converter
             << QVariant(false)     // AddToMRU
             << QVariant(false)     // Local
             << QVariant(2);        // CorruptLoad = xlRepairFile(2) — auto-repair, no dialog

        workbooks->dynamicCall("Open(QVariant&,QVariant&,QVariant&,QVariant&,QVariant&,"
                               "QVariant&,QVariant&,QVariant&,QVariant&,QVariant&,"
                               "QVariant&,QVariant&,QVariant&,QVariant&,QVariant&)", args);
        // The opened workbook is now the active workbook
        wb = excel.querySubObject("ActiveWorkbook");
    }

    if (!wb || wb->isNull()) {
        // Fallback: simple open — DisplayAlerts=false still suppresses dialogs
        qWarning() << "[COM-RECALC] Open with CorruptLoad failed, trying simple Open...";
        if (wb) delete wb;
        wb = workbooks->querySubObject("Open(const QString&)", filePath);
    }

    if (!wb) {
        QString err = QString("COM: Failed to open workbook: %1").arg(filePath);
        qWarning() << "[COM-RECALC]" << err;
        if (errorOut) *errorOut = err;
        excel.dynamicCall("Quit()");
        return false;
    }

    qDebug() << "[COM-RECALC] Workbook opened, forcing full recalculation...";

    // Force recalculation of all formulas in this workbook
    // CalculateFullRebuild = recalculates everything including non-volatile formulas
    wb->dynamicCall("Calculate()");
    // Belt-and-suspenders: also force full app recalc
    excel.dynamicCall("CalculateFull()");

    qDebug() << "[COM-RECALC] Recalculation complete, saving...";

    // Save — DisplayAlerts=false ensures no "save as xlsm?" dialog appears
    wb->dynamicCall("Save()");

    qDebug() << "[COM-RECALC] Saved. Closing workbook...";

    // Close without prompting (DisplayAlerts already false)
    wb->dynamicCall("Close(bool)", false);

    excel.dynamicCall("Quit()");

    qDebug() << "[COM-RECALC] Done for:" << filePath;
    return true;
}

bool ExcelHandler::isLoaded(const QString& key)
{
    QReadLocker locker(&m_lock);
    return m_workbooks.contains(key);
}

QStringList ExcelHandler::getSheetNames(const QString& key)
{
    QReadLocker locker(&m_lock);
    if (m_workbooks.contains(key))
        return m_workbooks[key].sheetNames;
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
//  Cell read / write — pure in-memory on the loaded WorkbookData
// ─────────────────────────────────────────────────────────────────────────────

QVariant ExcelHandler::getCellValue(const QString& key, const QString& sheetName, int row, int col)
{
    QWriteLocker locker(&m_lock); // write lock needed — getSheet may lazy-load
    if (!m_workbooks.contains(key))
        return QVariant();

    SheetData& sheet = getSheet(key, sheetName);
    QString cellRef  = buildCellRef(columnToLetter(col), row);
    auto cellIt      = sheet.cells.find(cellRef);
    if (cellIt == sheet.cells.end())
        return QVariant();

    const QString& val = cellIt->value;
    bool ok = false;
    double d = val.toDouble(&ok);
    if (ok) return d;
    return val;
}

bool ExcelHandler::setCellValue(const QString& key, const QString& sheetName, int row, int col, const QVariant& value)
{
    QWriteLocker locker(&m_lock);
    if (!m_workbooks.contains(key))
        return false;

    WorkbookData& wb = m_workbooks[key];
    SheetData& sheet = getSheet(key, sheetName);
    sheet.name = sheetName;

    QString cellRef = buildCellRef(columnToLetter(col), row);
    CellData& cell  = sheet.cells[cellRef];
    cell.row     = row;
    cell.col     = col;
    cell.formula.clear();

    if (value.typeId() == QMetaType::Double || value.typeId() == QMetaType::Float ||
        value.typeId() == QMetaType::Int    || value.typeId() == QMetaType::LongLong) {
        cell.dataType = "";
        cell.value    = QString::number(value.toDouble(), 'f', 10)
                            .remove(QRegularExpression("0+$"))
                            .remove(QRegularExpression("\\.$"));
    } else {
        cell.dataType = "str";
        cell.value    = value.toString();
    }

    wb.modifiedSheets.insert(sheetName);
    wb.dirtyCells[sheetName].insert(cellRef);
    return true;
}

bool ExcelHandler::setCellFormula(const QString& key, const QString& sheetName, int row, int col, const QString& formula)
{
    QWriteLocker locker(&m_lock);
    if (!m_workbooks.contains(key))
        return false;

    WorkbookData& wb = m_workbooks[key];
    SheetData& sheet = getSheet(key, sheetName);
    sheet.name       = sheetName;

    QString cellRef  = buildCellRef(columnToLetter(col), row);
    CellData& cell   = sheet.cells[cellRef];
    cell.row     = row;
    cell.col     = col;
    cell.formula = formula;
    cell.dataType = "";

    wb.modifiedSheets.insert(sheetName);
    wb.dirtyCells[sheetName].insert(cellRef);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  transferData — fully in-memory, no COM (ported from Excel_transfer_23_3)
// ─────────────────────────────────────────────────────────────────────────────

int ExcelHandler::transferData(const QString& srcKey, const QString& srcSheet, const QString& srcCol,
                               const QVector<int>& srcRows, const QString& destKey, const QString& destSheet,
                               const QString& destCol, const QVector<int>& destRows,
                               const QString& sourceFileType, bool divideBy1000)
{
    QWriteLocker locker(&m_lock); // write because we modify destSheet

    if (!m_workbooks.contains(srcKey) || !m_workbooks.contains(destKey)) {
        qWarning() << "ExcelHandler::transferData: workbook not found" << srcKey << destKey;
        return 0;
    }
    if (srcRows.size() != destRows.size()) {
        qWarning() << "ExcelHandler::transferData: row count mismatch";
        return 0;
    }

    WorkbookData& srcWb = m_workbooks[srcKey];
    WorkbookData& dstWb  = m_workbooks[destKey];

    auto normalizeSheet = [](const QString& name) {
        return name.trimmed().toLower();
    };

    auto resolveSheetName = [&](const WorkbookData& wb, const QString& requested,
                                bool allowFallback, const QString& label) {
        if (requested.isEmpty()) return requested;
        const QString requestedNorm = normalizeSheet(requested);
        for (const QString& name : wb.sheetNames) {
            if (normalizeSheet(name) == requestedNorm) {
                if (name != requested) {
                    qDebug() << "ExcelHandler::transferData: resolved" << label
                             << "sheet" << requested << "->" << name;
                }
                return name;
            }
        }
        for (auto it = wb.sheetPathMap.constBegin(); it != wb.sheetPathMap.constEnd(); ++it) {
            if (normalizeSheet(it.key()) == requestedNorm) {
                if (it.key() != requested) {
                    qDebug() << "ExcelHandler::transferData: resolved" << label
                             << "sheet" << requested << "->" << it.key();
                }
                return it.key();
            }
        }
        if (allowFallback && !wb.sheetNames.isEmpty()) {
            qWarning() << "ExcelHandler::transferData: sheet" << requested
                       << "not found; falling back to" << wb.sheetNames.first()
                       << "for" << label;
            return wb.sheetNames.first();
        }
        return requested;
    };

    const bool allowSrcFallback = (sourceFileType == "pax" || sourceFileType == "staff");
    const QString srcSheetName = resolveSheetName(srcWb, srcSheet, allowSrcFallback, "source");
    const QString destSheetName = resolveSheetName(dstWb, destSheet, false, "dest");

    // Lazy-load both sheets under the write lock
    SheetData& srcSheetRef = getSheet(srcKey, srcSheetName);
    SheetData& dstSheet    = getSheet(destKey, destSheetName);

    // Take a const copy of src sheet since getSheet may invalidate refs on insert
    const SheetData srcSheetCopy = srcSheetRef;
    dstSheet.name = destSheetName;

    int srcColIdx  = letterToColumn(srcCol);
    int destColIdx = letterToColumn(destCol);
    int transferred = 0;


    for (int i = 0; i < srcRows.size(); ++i) {
        QString srcRef  = buildCellRef(columnToLetter(srcColIdx), srcRows[i]);
        QString destRef = buildCellRef(columnToLetter(destColIdx), destRows[i]);

        auto cellIt = srcSheetCopy.cells.find(srcRef);
        double numVal = 0.0;
        bool isNum = false;
        if (cellIt != srcSheetCopy.cells.end()) {
            if (!cellIt->value.isEmpty()) {
                numVal = cellIt->value.toDouble(&isNum);
                if (!isNum) {
                    // Non-numeric string value — write 0 to dest (don't copy formula references)
                    isNum = true;
                    numVal = 0.0;
                }
            } else {
                // Cell exists but has no cached value (e.g. formula with no <v> tag) — use 0
                isNum = true;
                numVal = 0.0;
            }
        } else {
            // Missing source cell — use 0
            isNum = true;
            numVal = 0.0;
        }

        CellData& dCell = dstSheet.cells[destRef];
        dCell.row     = destRows[i];
        dCell.col     = destColIdx;
        dCell.formula.clear();  // never copy source formula — raw value only

        if (isNum) {
            if (divideBy1000) {
                if (sourceFileType == "pax") {
                    const bool shouldDivide = (destRows[i] >= 5 && destRows[i] <= 7);
                    if (shouldDivide) {
                        numVal = numVal / 1000.0;
                    }
                } else {
                    numVal /= -1000.0;
                }
            }
            dCell.dataType = "";
            dCell.value    = QString::number(numVal, 'f', 10)
                                 .remove(QRegularExpression("0+$"))
                                 .remove(QRegularExpression("\\.$"));
        } else {
            dCell.dataType = "str";
            dCell.value    = cellIt->value;
        }

        transferred++;
    }

    dstWb.modifiedSheets.insert(destSheetName);

    // Track which cells were written so saveOpenXML merges only dirty cells
    {
        int destColIdx2 = letterToColumn(destCol);
        for (int i = 0; i < destRows.size(); ++i) {
            QString destRef = buildCellRef(columnToLetter(destColIdx2), destRows[i]);
            dstWb.dirtyCells[destSheetName].insert(destRef);
        }
    }

    return transferred;
}

// ─────────────────────────────────────────────────────────────────────────────
//  copyData / copyFullSheet
// ─────────────────────────────────────────────────────────────────────────────

bool ExcelHandler::copyData(const QString& srcKey, const QString& srcSheet, const QString& srcCol,
                            const QVector<int>& srcRows, const QString& destKey, const QString& destSheet,
                            const QString& destCol, const QVector<int>& destRows)
{
    if (srcRows.size() != destRows.size()) {
        qWarning() << "ExcelHandler::copyData: row count mismatch";
        return false;
    }
    int copied = transferData(srcKey, srcSheet, srcCol, srcRows, destKey, destSheet, destCol, destRows, "generic", false);
    return copied == srcRows.size();
}

void ExcelHandler::resetOverrides(const QString& key)
{
    QWriteLocker locker(&m_lock);
    if (!m_workbooks.contains(key)) return;
    WorkbookData& wb = m_workbooks[key];
    wb.modifiedSheets.clear();
    wb.rawSheetOverrides.clear();
    wb.highlightSourceRows.clear();
    wb.dirtyCells.clear();
    wb.insertAfterSheet.clear();
}

void ExcelHandler::setInsertAfter(const QString& key, const QString& sheetName, const QString& insertAfter)
{
    QWriteLocker locker(&m_lock);
    if (!m_workbooks.contains(key)) return;
    m_workbooks[key].insertAfterSheet[sheetName] = insertAfter;
}

bool ExcelHandler::renameSheet(const QString& key, const QString& oldName, const QString& newName)
{
    QWriteLocker locker(&m_lock);
    if (!m_workbooks.contains(key)) return false;
    WorkbookData& wb = m_workbooks[key];

    int idx = wb.sheetNames.indexOf(oldName);
    if (idx == -1) return false;
    wb.sheetNames[idx] = newName;

    if (wb.sheetPathMap.contains(oldName)) {
        QString path = wb.sheetPathMap.take(oldName);
        wb.sheetPathMap[newName] = path;
    }

    if (wb.sheets.contains(oldName)) {
        SheetData data = wb.sheets.take(oldName);
        data.name = newName;
        wb.sheets[newName] = data;
    }

    wb.modifiedSheets.insert(newName);
    wb.modifiedSheets.remove(oldName);
    return true;
}

// Apply dark blue (sourceRows) / light grey (all other rows) fill to row elements in sheet XML.
// styleBlue and styleGrey are xf indices in the destination styles.xml.
static QByteArray applyRowHighlights(const QByteArray& sheetXml,
                                     const QSet<int>& sourceRows,
                                     int styleBlue, int styleGrey)
{
    QByteArray out;
    out.reserve(sheetXml.size() + sheetXml.size() / 4);

    int pos = 0;
    const int len = sheetXml.size();

    while (pos < len) {
        int rowStart = sheetXml.indexOf("<row ", pos);
        if (rowStart == -1) {
            out.append(sheetXml.constData() + pos, len - pos);
            break;
        }
        out.append(sheetXml.constData() + pos, rowStart - pos);

        // Find end of opening <row ...> tag
        int tagEnd = sheetXml.indexOf('>', rowStart);
        if (tagEnd == -1) {
            out.append(sheetXml.constData() + rowStart, len - rowStart);
            break;
        }

        QByteArray rowTag = sheetXml.mid(rowStart, tagEnd - rowStart + 1);

        // Extract r="N" row number
        int rIdx = rowTag.indexOf("r=\"");
        int rowNum = -1;
        if (rIdx != -1) {
            int rEnd = rowTag.indexOf('"', rIdx + 3);
            if (rEnd != -1)
                rowNum = rowTag.mid(rIdx + 3, rEnd - rIdx - 3).toInt();
        }

        if (rowNum > 0) {
            int styleIdx = sourceRows.contains(rowNum) ? styleBlue : styleGrey;

            // Strip existing s="..." and customFormat="..." attrs
            QByteArray clean = rowTag;
            // Remove customFormat="..."
            {
                int cf = clean.indexOf("customFormat=\"");
                if (cf != -1) {
                    int cfEnd = clean.indexOf('"', cf + 14);
                    if (cfEnd != -1) clean.remove(cf, cfEnd - cf + 1);
                }
            }
            // Remove s="..." (only standalone, not inside another attr)
            {
                int si = clean.indexOf(" s=\"");
                if (si != -1) {
                    int siEnd = clean.indexOf('"', si + 4);
                    if (siEnd != -1) clean.remove(si, siEnd - si + 1);
                }
            }

            // Insert s="X" customFormat="1" before closing >
            bool selfClose = clean.endsWith("/>");
            QByteArray insert = QString(" s=\"%1\" customFormat=\"1\"").arg(styleIdx).toUtf8();
            if (selfClose)
                clean.insert(clean.size() - 2, insert);
            else
                clean.insert(clean.size() - 1, insert);

            out.append(clean);
        } else {
            out.append(rowTag);
        }

        pos = tagEnd + 1;
    }

    return out;
}

// Append two fills+xfs to styles.xml and return {blueXfIdx, greyXfIdx}.
// Returns {-1,-1} on failure. Safe to call even if fills/xfs already exist.
static QPair<int,int> injectHighlightStyles(QByteArray& stylesXml)
{
    if (stylesXml.isEmpty()) return {-1,-1};

    // ── Helper: read count="N" from a tag, return N and the byte range [countStart, countEnd]
    // Returns -1 if not found.
    auto readCount = [&](const QByteArray& tag) -> int {
        int tagPos = stylesXml.indexOf(tag);
        if (tagPos == -1) return -1;
        int cc = stylesXml.indexOf("count=\"", tagPos);
        if (cc == -1) return -1;
        int ccEnd = stylesXml.indexOf('"', cc + 7);
        if (ccEnd == -1) return -1;
        bool ok = false;
        int n = stylesXml.mid(cc + 7, ccEnd - cc - 7).toInt(&ok);
        return ok ? n : -1;
    };

    auto updateCount = [&](const QByteArray& tag, int newCount) {
        int tagPos = stylesXml.indexOf(tag);
        if (tagPos == -1) return;
        int cc = stylesXml.indexOf("count=\"", tagPos);
        if (cc == -1) return;
        int ccEnd = stylesXml.indexOf('"', cc + 7);
        if (ccEnd == -1) return;
        // replace count="OLD" with count="NEW"
        stylesXml.replace(cc, ccEnd - cc + 1,
            QString("count=\"%1\"").arg(newCount).toUtf8());
    };

    // ── 1. Inject fills ──────────────────────────────────────────────────
    const QByteArray blueFill =
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FF1F3864\"/><bgColor indexed=\"64\"/></patternFill></fill>";
    const QByteArray greyFill =
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFD9D9D9\"/><bgColor indexed=\"64\"/></patternFill></fill>";

    int fillCount = readCount("<fills ");
    if (fillCount < 0) return {-1,-1};
    int blueFillIdx = fillCount;
    int greyFillIdx = fillCount + 1;

    // Insert fills then update count (count tag comes BEFORE </fills>, so update first)
    updateCount("<fills ", fillCount + 2);
    stylesXml.replace("</fills>", blueFill + greyFill + "</fills>");

    // ── 2. Inject xf entries into <cellXfs> ─────────────────────────────
    int xfCount = readCount("<cellXfs");
    if (xfCount < 0) return {-1,-1};
    int blueXfIdx = xfCount;
    int greyXfIdx = xfCount + 1;

    QByteArray blueXf = QString(
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"%1\" borderId=\"0\" applyFill=\"1\"/>")
        .arg(blueFillIdx).toUtf8();
    QByteArray greyXf = QString(
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"%1\" borderId=\"0\" applyFill=\"1\"/>")
        .arg(greyFillIdx).toUtf8();

    // Update count before inserting (count tag comes before </cellXfs>)
    updateCount("<cellXfs", xfCount + 2);
    stylesXml.replace("</cellXfs>", blueXf + greyXf + "</cellXfs>");

    return {blueXfIdx, greyXfIdx};
}

bool ExcelHandler::copyFullSheet(const QString& srcKey, const QString& srcSheet,
                                 const QString& destKey, const QString& newSheetName,
                                 const QVector<int>& highlightRows)
{
    QWriteLocker locker(&m_lock);

    if (!m_workbooks.contains(srcKey) || !m_workbooks.contains(destKey)) {
        qWarning() << "ExcelHandler::copyFullSheet: workbook not found" << srcKey << destKey;
        return false;
    }

    WorkbookData& src = m_workbooks[srcKey];

    // Lazy-load if not cached — getSheet() assumes lock is held by caller
    if (!src.sheets.contains(srcSheet)) {
        if (!src.sheetPathMap.contains(srcSheet)) {
            qWarning() << "ExcelHandler::copyFullSheet: sheet" << srcSheet
                       << "not found in workbook" << srcKey
                       << "— available:" << src.sheetPathMap.keys();
            return false;
        }
        // loadSheetLazy only reads the ZIP (no shared state) — safe under write lock
        src.sheets[srcSheet] = loadSheetLazy(src, srcSheet);
    }

    // Raw XML copy (Option A)
    QString targetName = newSheetName.isEmpty() ? srcSheet : newSheetName;

    WorkbookData& dst = m_workbooks[destKey];
    for (auto it = dst.sheetNames.begin(); it != dst.sheetNames.end(); ) {
        if (it->startsWith("Recovered_", Qt::CaseInsensitive))
            it = dst.sheetNames.erase(it);
        else
            ++it;
    }

    QString srcPath = src.sheetPathMap.value(srcSheet);
    if (srcPath.isEmpty()) {
        qWarning() << "ExcelHandler::copyFullSheet: source path not found for" << srcSheet;
        return false;
    }

    // Use cached ZIP entries if available — avoids re-opening the network file under lock
    QByteArray rawXml;
    if (!src.cachedZipEntries.isEmpty()) {
        rawXml = src.cachedZipEntries.value(srcPath);
    } else {
        QZipReader zip(src.filePath);
        rawXml = zip.fileData(srcPath);
    }
    if (rawXml.isEmpty()) {
        qWarning() << "ExcelHandler::copyFullSheet: empty XML for" << srcSheet;
        return false;
    }

    // Strip drawing references from sheet XML — drawings use relationship IDs that are
    // only valid in the source workbook. Leaving them causes Excel to remove drawing shapes.
    // Pattern: <drawing r:id="rId..."/> or <legacyDrawing r:id="rId..."/>
    {
        QByteArray stripped = rawXml;
        // Remove <drawing .../> elements
        QByteArray result;
        int pos2 = 0;
        while (pos2 < stripped.size()) {
            int ds = stripped.indexOf("<drawing ", pos2);
            if (ds == -1) { result.append(stripped.constData() + pos2, stripped.size() - pos2); break; }
            result.append(stripped.constData() + pos2, ds - pos2);
            int de = stripped.indexOf("/>", ds);
            if (de == -1) { result.append(stripped.constData() + ds, stripped.size() - ds); break; }
            pos2 = de + 2;
        }
        rawXml = result;

        // Remove <legacyDrawing .../> elements
        result.clear(); pos2 = 0;
        while (pos2 < rawXml.size()) {
            int ds = rawXml.indexOf("<legacyDrawing ", pos2);
            if (ds == -1) { result.append(rawXml.constData() + pos2, rawXml.size() - pos2); break; }
            result.append(rawXml.constData() + pos2, ds - pos2);
            int de = rawXml.indexOf("/>", ds);
            if (de == -1) { result.append(rawXml.constData() + ds, rawXml.size() - ds); break; }
            pos2 = de + 2;
        }
        rawXml = result;
    }

    bool bypass = qEnvironmentVariableIsSet("EXCEL_BYPASS_SHARED_STRINGS");
    QByteArray xmlToStore = bypass ? rawXml : convertSharedToInline(rawXml, src.sharedStrings);
    if (bypass) {
        qWarning() << "copyFullSheet: bypassing shared-strings conversion";
    }

    dst.rawSheetOverrides[targetName] = xmlToStore;

    if (!highlightRows.isEmpty())
        dst.highlightSourceRows[targetName] = highlightRows;
    else
        dst.highlightSourceRows.remove(targetName);

    if (!newSheetName.isEmpty()) {
        // carry insert-after preference from mapping when copyFullSheet is used
        // placeholder removed; insertAfter is set explicitly by transfer service
    }

    if (!dst.sheetNames.contains(targetName))
        dst.sheetNames.append(targetName);

    dst.modifiedSheets.insert(targetName);

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  File finders
// ─────────────────────────────────────────────────────────────────────────────

QString ExcelHandler::findCostControlFile(const QString& basePath, const QString& month, int year)
{
    QString monthNum = MONTH_TO_NUM.value(month, "01");

    // Files must be in {base}/{year}/{monthNum}/test/ — no fallback
    const QString testPath = QString("%1/%2/%3/test").arg(basePath).arg(year).arg(monthNum);
    QDir dir(testPath);

    if (dir.exists()) {
        // Strict match: correct month number + year (e.g. *03_2025*working*)
        const QString strictPattern = QString("*%1_%2*working*").arg(monthNum).arg(year);
        QStringList files = dir.entryList(QStringList() << strictPattern, QDir::Files);
        if (!files.isEmpty())
            return dir.absoluteFilePath(files.first());

        // Loose fallback only for non-standard naming (e.g. estimation files)
        files = dir.entryList(QStringList() << "*working*", QDir::Files);
        if (!files.isEmpty()) {
            qWarning() << "ExcelHandler::findCostControlFile: non-standard filename"
                       << files.first() << "for" << month << year;
            return dir.absoluteFilePath(files.first());
        }
    }

    qWarning() << "ExcelHandler::findCostControlFile: no *working* file found for" << month << year
               << "under" << testPath;
    return QString();
}

QString ExcelHandler::findSAPFile(const QString& basePath, const QString& month, int year)
{
    QString monthNum = MONTH_TO_NUM.value(month, "01");
    QString path = QString("%1/%2/%3/SAP export monthly").arg(basePath).arg(year).arg(monthNum);
    QDir dir(path);
    if (!dir.exists()) {
        qWarning() << "SAP monthly folder missing:" << path;
        return QString();
    }

    // Prefer exact match: 09_2026.xlsx / 09_2026.xlsm
    QString preferredXlsx = QString("%1_%2.xlsx").arg(monthNum).arg(year);
    QString preferredXlsm = QString("%1_%2.xlsm").arg(monthNum).arg(year);
    if (dir.exists(preferredXlsx))
        return dir.absoluteFilePath(preferredXlsx);
    if (dir.exists(preferredXlsm))
        return dir.absoluteFilePath(preferredXlsm);

    // Fallback: any file starting with monthNum_year
    QStringList files = dir.entryList(QStringList() << QString("%1_%2.*").arg(monthNum).arg(year), QDir::Files);
    if (!files.isEmpty())
        return dir.absoluteFilePath(files.first());

    qWarning() << "No SAP monthly report found in:" << path;
    return QString();
}

QString ExcelHandler::findSapYtdFile(const QString& basePath, const QString& month, int year)
{
    QString monthNum = MONTH_TO_NUM.value(month, "01");
    QStringList searchDirs = {
        QString("%1/%2/%3/SAP YTD").arg(basePath).arg(year).arg(monthNum),
        QString("%1/%2/%3/SAP export monthly").arg(basePath).arg(year).arg(monthNum)
    };

    for (const QString& path : searchDirs) {
        QDir dir(path);
        if (!dir.exists()) continue;
        QStringList files = dir.entryList(QStringList() << "YTD_*" << "*YTD*", QDir::Files);
        if (!files.isEmpty())
            return dir.absoluteFilePath(files.first());
    }
    return QString();
}

QString ExcelHandler::findPaxFile(const QString& basePath, const QString& month, int year)
{
    QString monthNum = MONTH_TO_NUM.value(month, "01");
    QString trafficDir = QString("%1/%2/%3/Traffic").arg(basePath).arg(year).arg(monthNum);
    QDir dir(trafficDir);
    if (!dir.exists()) {
        qWarning() << "ExcelHandler::findPaxFile: Traffic dir not found:" << trafficDir;
        return QString();
    }
    // Try common PAX file patterns
    const QStringList patterns = {"PAX*", "pax*", "*PAX*", "*pax*", "*traffic*", "*Traffic*", "*.xls*"};
    for (const QString& p : patterns) {
        QStringList files = dir.entryList(QStringList() << p, QDir::Files);
        if (!files.isEmpty())
            return dir.absoluteFilePath(files.first());
    }
    qWarning() << "ExcelHandler::findPaxFile: no PAX file found in" << trafficDir
               << "— files:" << dir.entryList(QDir::Files);
    return QString();
}

QString ExcelHandler::findStaffFile(const QString& basePath, int year)
{
    QString staffDir = QString("%1/Staff").arg(basePath);
    QDir dir(staffDir);
    if (!dir.exists()) return QString();
    // Try exact year match first
    QString preferred = QString("Staff_%1.xlsx").arg(year);
    if (dir.exists(preferred))
        return dir.absoluteFilePath(preferred);
    preferred = QString("Staff_%1.xlsm").arg(year);
    if (dir.exists(preferred))
        return dir.absoluteFilePath(preferred);
    // Fall back to newest Staff_*.xls* file
    QStringList files = dir.entryList(QStringList() << "Staff_*.xls*", QDir::Files, QDir::Name | QDir::Reversed);
    if (files.isEmpty()) return QString();
    return dir.absoluteFilePath(files.first());
}
