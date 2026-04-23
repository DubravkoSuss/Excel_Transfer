#include "excelhandler.h"
#include <QAxObject>
#include <QDirIterator>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QDebug>
#include <QRegularExpression>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QBuffer>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QProcess>
#include <QtCore/qglobal.h>
#include <QtCore/private/qzipreader_p.h>
#include <QtCore/private/qzipwriter_p.h>

//  Helpers

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

double ExcelHandler::parseNumericString(const QString& val, bool* ok)
{
    if (val.isEmpty()) {
        if (ok) *ok = false;
        return 0.0;
    }

    // 1. Strip known currency symbols and signs (actual unicode chars, not escape sequences)
    static const QRegularExpression currencyRx(
        QString::fromUtf8("[$\xe2\x82\xac\xc2\xa3\xc2\xa5\xe2\x82\xb9]"), // $, €, £, ¥, ₹
        QRegularExpression::CaseInsensitiveOption);
    
    QString cleaned = val.trimmed();
    cleaned.remove(currencyRx);
    
    // 2. Handle Croatian 'kn' and other word-based symbols
    cleaned.remove(QRegularExpression(QStringLiteral("\\bkn\\b"), QRegularExpression::CaseInsensitiveOption));
    
    // 3. Remove all characters except digits, minus sign, dot, and comma
    cleaned.remove(QRegularExpression(QStringLiteral("[^0-9.,\\-]")));
    
    if (cleaned.isEmpty()) {
        if (ok) *ok = false;
        return 0.0;
    }

    // 4. Handle European vs Standard formatting (dot/comma)
    // If both exist, the LAST one is almost certainly the decimal separator.
    int lastDot = cleaned.lastIndexOf('.');
    int lastComma = cleaned.lastIndexOf(',');
    
    if (lastDot != -1 && lastComma != -1) {
        if (lastComma > lastDot) {
            // "1.234,56" -> remove dots, replace comma with dot
            cleaned.remove('.');
            cleaned.replace(',', '.');
        } else {
            // "1,234.56" -> remove commas
            cleaned.remove(',');
        }
    }
    // If only dot exists ("1234.56"), it's already standard.

    return cleaned.toDouble(ok);
}

static constexpr int kMaxExcelRows = 1048576;
static constexpr int kMaxExcelCols = 16384;

static bool isValidCellAddress(int row, int col)
{
    return row > 0 && row <= kMaxExcelRows && col > 0 && col <= kMaxExcelCols;
}

 //  Static data

const QMap<QString, QString> ExcelHandler::MONTH_TO_NUM = {
    {"January","01"},{"February","02"},{"March","03"},{"April","04"},
    {"May","05"},{"June","06"},{"July","07"},{"August","08"},
    {"September","09"},{"October","10"},{"November","11"},{"December","12"}
};

 //  Constructor / Destructor

ExcelHandler::ExcelHandler(QObject *parent) : QObject(parent) {}
ExcelHandler::~ExcelHandler() {}

bool ExcelHandler::validateWorkbookFile(const QString& filePath, QString* errorOut)
{
    auto setError = [&](const QString& msg) {
        if (errorOut) *errorOut = msg;
    };

    QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile()) {
        setError(QString("File not found: %1").arg(filePath));
        return false;
    }

    QFile probe(filePath);
    if (!probe.open(QIODevice::ReadOnly)) {
        setError(QString("Cannot open file. It may be locked/open in Excel: %1")
                     .arg(probe.errorString()));
        return false;
    }

    const QByteArray sig = probe.read(4);
    probe.close();
    if (sig.size() < 4 || sig[0] != 'P' || sig[1] != 'K') {
        setError("File is not a valid OpenXML ZIP archive (.xlsx/.xlsm).");
        return false;
    }

    QZipReader zip(filePath);
    if (!zip.isReadable()) {
        setError("ZIP container is unreadable. File is likely corrupt or incomplete.");
        return false;
    }

    const QByteArray contentTypes = zip.fileData("[Content_Types].xml");
    if (contentTypes.isEmpty()) {
        setError("OpenXML structure invalid: missing [Content_Types].xml.");
        return false;
    }

    const QByteArray workbookXml = zip.fileData("xl/workbook.xml");
    if (workbookXml.isEmpty()) {
        setError("OpenXML structure invalid: missing xl/workbook.xml.");
        return false;
    }

    if (errorOut) errorOut->clear();
    return true;
}

 //  Column helpers

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
    const QString normalized = letter.trimmed().toUpper();
    if (normalized.isEmpty())
        return 0;

    int col = 0;
    for (int i = 0; i < normalized.length(); ++i) {
        const QChar ch = normalized[i];
        if (ch < QChar('A') || ch > QChar('Z'))
            return 0;
        col = col * 26 + (ch.toLatin1() - 'A' + 1);
        if (col > kMaxExcelCols)
            return 0;
    }
    return col;
}

QString ExcelHandler::columnToLetter(int col) { return staticColumnToLetter(col); }
int ExcelHandler::letterToColumn(const QString& letter) { return staticLetterToColumn(letter); }

// No PowerShell extract/compress needed  ” we use QZipReader/QZipWriter directly in-memory.

 //  OpenXML: load shared strings from raw XML bytes

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

 //  OpenXML: parse one sheet XML into SheetData

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

 //  OpenXML: load workbook metadata ONLY  ” no sheet parsing (fast)

bool ExcelHandler::loadOpenXML(const QString& filePath, WorkbookData& wb,
                               const QSet<QString>& sheetsNeeded,
                               QString* errorOut)
{
    qDebug() << "loadOpenXML START" << filePath;
    // Fast file magic signature check to fail fast without instantiating QZipReader on invalid files
    QFile probe(filePath);
    if (probe.open(QIODevice::ReadOnly)) {
        const QByteArray sig = probe.read(4);
        probe.close();
        if (sig.size() < 4 || sig[0] != 'P' || sig[1] != 'K') {
            if (errorOut) *errorOut = "File is not a valid OpenXML ZIP archive.";
            return false;
        }
    }

    QZipReader zip(filePath);
    if (!zip.isReadable()) {
        qWarning() << "ExcelHandler: cannot open ZIP:" << filePath;
        if (errorOut) {
            *errorOut = "ZIP container is unreadable. File is likely corrupt or incomplete.";
        }
        return false;
    }
    qDebug() << "loadOpenXML: ZIP opened, reading workbook.xml...";

    // --- workbook.xml â†’ sheet names + sheetId map ---
    QByteArray wbData = zip.fileData("xl/workbook.xml");
    if (wbData.isEmpty()) {
        if (errorOut) {
            *errorOut = "OpenXML structure invalid: missing xl/workbook.xml.";
        }
        return false;
    }
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
    if (sheetNames.isEmpty()) {
        if (errorOut) {
            *errorOut = "Workbook metadata invalid: no sheets found in xl/workbook.xml.";
        }
        return false;
    }

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

    // --- build sheetName â†’ ZIP path map (no sheet parsing yet!) ---
    wb.filePath   = filePath;
    wb.sheetNames = sheetNames; // will be replaced with rels-resolved names below if available
    wb.sheets.clear();
    wb.sheetPathMap.clear();
    wb.rawSheetOverrides.clear();
    wb.modifiedSheets.clear();

    // Also build rId â†’ sheetName from workbook.xml rels for robust mapping
    QMap<QString, QString> rIdToZipPath; // rId â†’ xl/worksheets/sheetN.xml
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

    // Re-parse workbook.xml with r:id to get correct sheetName â†’ ZIP path
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

        // Pre-parse only sheets in sheetsNeeded (if specified)  ” skip the rest (lazy on demand)
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
    if (errorOut) errorOut->clear();
    return true;
}

 //  Lazy sheet loader  ” called on first access to a sheet

SheetData ExcelHandler::loadSheetLazy(const WorkbookData& wb, const QString& sheetName)
{
    QString zipPath = wb.sheetPathMap.value(sheetName);
    if (zipPath.isEmpty()) {
        qWarning() << "ExcelHandler::loadSheetLazy: sheet not found:" << sheetName
                   << " ” available sheets:" << wb.sheetPathMap.keys();
        return SheetData{sheetName, {}, 0, 0};
    }

    QByteArray data;
    if (!wb.cachedZipEntries.isEmpty()) {
        // Fast path: use in-memory cache  ” no network I/O
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

 //  getSheet  ” returns cached sheet or lazy-loads it (caller must hold write lock)

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
        // Release write lock temporarily? No  ” we hold it, just load inline.
        // loadSheetLazy only reads the ZIP file (no shared state), so it's safe.
        wb.sheets[resolvedName] = loadSheetLazy(wb, resolvedName);
    }
    return wb.sheets[resolvedName];
}

 //  OpenXML: save workbook  ” fully in-memory via QZipReader + QZipWriter
//  Strategy: read all entries from original ZIP, replace only modified sheets

 //  mergeSheetXml  ” patches specific cells into original sheet XML bytes
//  Works directly on raw bytes  ” no re-serialization, preserves everything.
//  Only the exact cells in SheetData.cells are replaced/inserted.

// Forward declarations
static QByteArray applyRowHighlights(const QByteArray& sheetXml, const QSet<int>& sourceRows, int styleBlue, int styleLightBlue);
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

static void widenSheetColumns(QByteArray& sheetXml, double minWidth)
{
    if (sheetXml.isEmpty())
        return;

    int sheetDataPos = sheetXml.indexOf("<sheetData");
    if (sheetDataPos < 0)
        return;

    int colsStart = sheetXml.indexOf("<cols>");
    if (colsStart < 0) {
        QByteArray colsBlock = QString("<cols>\n  <col min=\"1\" max=\"16384\" width=\"%1\" customWidth=\"1\"/>\n</cols>\n")
                                 .arg(minWidth, 0, 'f', 2)
                                 .toUtf8();
        sheetXml.insert(sheetDataPos, colsBlock);
        return;
    }

    QString xml = QString::fromUtf8(sheetXml);
    QRegularExpression colRx(
        QStringLiteral("<col\\s+([^>]*?)min=\"(\\d+)\"([^>]*?)max=\"(\\d+)\"([^>]*?)width=\"([\\d.]+)\"([^>]*?)/>"),
        QRegularExpression::CaseInsensitiveOption);

    struct Hit { qsizetype start; qsizetype len; QString replacement; };
    QVector<Hit> hits;

    auto it = colRx.globalMatch(xml);
    while (it.hasNext()) {
        auto m = it.next();
        double currentWidth = m.captured(6).toDouble();
        if (currentWidth < minWidth) {
            QString patched = m.captured(0);
            patched.replace(QStringLiteral("width=\"") + m.captured(6) + QStringLiteral("\""),
                            QString("width=\"%1\"").arg(minWidth, 0, 'f', 2));
            if (!patched.contains("customWidth"))
                patched.replace("/>", QStringLiteral(" customWidth=\"1\"/>"));
            hits.append({ m.capturedStart(0), m.capturedLength(0), patched });
        }
    }

    for (int i = hits.size() - 1; i >= 0; --i)
        xml.replace(hits[i].start, hits[i].len, hits[i].replacement);

    if (!hits.isEmpty())
        sheetXml = xml.toUtf8();
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
        qWarning() << "[XML ERROR] removeDuplicateCells:" << reader.errorString() << " ” falling back";
        return xml;
    }

    qDebug() << "[DUP CLEANUP] Output size:" << output.size() << "vs original:" << xml.size();
    return output;
}

static QByteArray sortCellsInRow(const QByteArray& rowContent)
{
    struct Cell {
        int colIndex;
        QByteArray xml;
    };
    QList<Cell> parsedCells;
    QByteArray extLstXml;
    
    int pos = 0;
    while (pos < rowContent.size()) {
        int cStart = -1;
        for (int i = pos; i < rowContent.size() - 2; ++i) {
            if (rowContent[i] == '<' && rowContent[i+1] == 'c') {
                char next = rowContent[i+2];
                if (next == ' ' || next == '\t' || next == '\r' || next == '\n' || next == '/' || next == '>') {
                    cStart = i;
                    break;
                }
            }
        }
        
        int eStart = rowContent.indexOf("<extLst", pos);
        
        if (cStart == -1 && eStart == -1) break;
        
        if (eStart != -1 && (cStart == -1 || eStart < cStart)) {
            extLstXml = rowContent.mid(eStart);
            break;
        }
        
        int tagEnd = rowContent.indexOf('>', cStart);
        if (tagEnd == -1) break;
        
        int cellEnd;
        if (rowContent[tagEnd - 1] == '/') {
            cellEnd = tagEnd + 1;
        } else {
            cellEnd = rowContent.indexOf("</c>", tagEnd);
            if (cellEnd == -1) break;
            cellEnd += 4;
        }
        
        QByteArray cellXml = rowContent.mid(cStart, cellEnd - cStart);
        int rIdx = cellXml.indexOf("r=\"");
        int rEnd = -1;
        if (rIdx != -1) {
            rEnd = cellXml.indexOf('"', rIdx + 3);
        } else {
            rIdx = cellXml.indexOf("r='");
            if (rIdx != -1) rEnd = cellXml.indexOf('\'', rIdx + 3);
        }
        
        int colIndex = 0;
        if (rIdx != -1 && rEnd != -1) {
            QString ref = QString::fromLatin1(cellXml.data() + rIdx + 3, rEnd - rIdx - 3);
            ref.remove(QRegularExpression("[0-9]"));
            colIndex = ExcelHandler::staticLetterToColumn(ref);
        }
        parsedCells.append({colIndex, cellXml});
        pos = cellEnd;
    }
    
    std::stable_sort(parsedCells.begin(), parsedCells.end(), [](const Cell& a, const Cell& b) {
        return a.colIndex < b.colIndex;
    });
    
    QByteArray out;
    for (const Cell& c : parsedCells) {
        out.append(c.xml);
    }
    out.append(extLstXml);
    return out;
}

static QByteArray mergeSheetXml(const QByteArray& originalXml, const SheetData& changes)
{
    if (originalXml.isEmpty() || changes.cells.isEmpty())
        return originalXml;

    const QByteArray workingXml = originalXml;

    // Linear byte scan — O(n) single pass through the XML.
    const QMap<QString, CellData>& changedCells = changes.cells;
    QByteArray output;
    output.reserve(workingXml.size() + changedCells.size() * 64);
    QSet<QString> writtenCells; // track which changed cells were found in original XML
    QSet<QString> seenCells;    // track existing cells to drop duplicates

    int pos = 0;
    const int len = workingXml.size();

    while (pos < len) {
        int cStart = -1;
        for (int i = pos; i < len - 1; ++i) {
            if (workingXml[i] == '<' && workingXml[i+1] == 'c') {
                char next = (i + 2 < len) ? workingXml[i+2] : 0;
                if (next == ' ' || next == '\t' || next == '\r' || next == '\n' || next == '/' || next == '>') {
                    cStart = i;
                    break;
                }
            }
        }
        if (cStart == -1) {
            output.append(workingXml.constData() + pos, len - pos);
            break;
        }

        output.append(workingXml.constData() + pos, cStart - pos);

        int tagEnd = workingXml.indexOf('>', cStart);
        if (tagEnd == -1) {
            output.append(workingXml.constData() + cStart, len - cStart);
            break;
        }

        bool selfClosing = (tagEnd > 0 && workingXml[tagEnd - 1] == '/');
        int cEnd = selfClosing ? tagEnd + 1 : workingXml.indexOf("</c>", tagEnd + 1) + 4;
        if (cEnd < 4) {
            output.append(workingXml.constData() + cStart, len - cStart);
            break;
        }

        QByteArray openTag = workingXml.mid(cStart, tagEnd - cStart + 1);
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

            // Extract style attribute
            QString styleAttr;
            int sIdx = openTag.indexOf("s=\"");
            if (sIdx != -1) {
                int sEnd = openTag.indexOf('"', sIdx + 3);
                if (sEnd != -1)
                    styleAttr = QString(" s=\"%1\"").arg(
                        QString::fromLatin1(openTag.constData() + sIdx + 3, sEnd - sIdx - 3));
            }

            // Check if this is a shared formula cell  ” master (has ref=) or slave (no ref=).
            // Master: <c ...><f t="shared" si="0" ref="U5:AY5">...</f><v>...</v></c>
            // Slave:  <c ...><f t="shared" si="0"/><v>...</v></c>
            // We must preserve the <f> element to avoid corrupting the shared formula group.
            // Excel reports "Removed Records: Shared formula" if any cell in the group
            // loses its t="shared"/si= attributes.
            QByteArray fullCellXml;
            if (!selfClosing) {
                fullCellXml = workingXml.mid(cStart, cEnd - cStart);
            }
            // Preserve shared formulas for ALL cells (dirty or not) if the original XML
            // has t="shared". Dirty cells have cell.formula="" (cleared by setCellValue),
            // but the original XML still has the shared formula group info. We must keep
            // the <f> element intact and only update <v> — otherwise Excel reports
            // "Removed Records: Shared formula" on open.
            const bool isSharedCell = !selfClosing &&
                                      (fullCellXml.contains("t=\"shared\"") ||
                                       fullCellXml.contains("t='shared'"));

            QString safeVal = escapeXmlValue(cell.value);
            QByteArray newCell;
            if (isSharedCell) {
                // Preserve the shared formula <f> element byte-for-byte (master and slave).
                // Only replace (or append) the <v> cached value.
                QByteArray origCell = fullCellXml;
                // Find existing <v>...</v> and replace it, or append before </c>
                int vStart = origCell.indexOf("<v>");
                int vEnd   = origCell.indexOf("</v>");
                if (vStart != -1 && vEnd != -1) {
                    newCell  = origCell.left(vStart);
                    newCell += QString("<v>%1</v>").arg(safeVal).toUtf8();
                    newCell += origCell.mid(vEnd + 4); // skip old </v>
                } else {
                    // No existing <v>  ” insert before </c>
                    int closeC = origCell.lastIndexOf("</c>");
                    if (closeC != -1) {
                        newCell  = origCell.left(closeC);
                        newCell += QString("<v>%1</v></c>").arg(safeVal).toUtf8();
                    } else {
                        newCell = origCell; // fallback  ” don't touch
                    }
                }
            } else if (cell.value.isEmpty() && cell.formula.isEmpty()) {
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
            // UNCHANGED CELL  ” copy original XML byte-for-byte. Touch NOTHING.
            // Shared formulas, array formulas, styles  ” all preserved exactly as-is.
            // Do NOT strip shared formula attributes here  ” doing so mangles slave cells
            // (which have no formula text) turning them into empty <f></f> garbage,
            // causing the 14KB size loss and Excel's "Removed Records" repair dialog.
            output.append(workingXml.constData() + cStart, cEnd - cStart);
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
        int sdStart = output.indexOf("<sheetData>");
        if (sdStart == -1) sdStart = output.indexOf("<sheetData");

        for (auto rowIt = newCellsByRow.constBegin(); rowIt != newCellsByRow.constEnd(); ++rowIt) {
            int rowNum = rowIt.key();

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

            if (rowCells.isEmpty())
                continue;

            int sdEnd = output.lastIndexOf("</sheetData>");
            if (sdEnd == -1) sdEnd = output.size();

            int rowTagPos = -1;
            int insertPos = sdEnd;
            int scanPos = output.indexOf("<row ", sdStart);
            while (scanPos != -1 && scanPos < sdEnd) {
                int endTag = output.indexOf('>', scanPos);
                if (endTag == -1) break;
                
                int rIdx = output.indexOf(" r=\"", scanPos);
                if (rIdx == -1 || rIdx >= endTag) rIdx = output.indexOf("\tr=\"", scanPos);
                if (rIdx == -1 || rIdx >= endTag) rIdx = output.indexOf("\nr=\"", scanPos);
                
                if (rIdx != -1 && rIdx < endTag) {
                    int cEnd = output.indexOf('"', rIdx + 4);
                    if (cEnd != -1) {
                        int curRow = output.mid(rIdx + 4, cEnd - rIdx - 4).toInt();
                        if (curRow == rowNum) {
                            rowTagPos = scanPos;
                            break;
                        } else if (curRow > rowNum) {
                            insertPos = scanPos;
                            break;
                        }
                    }
                }
                scanPos = output.indexOf("<row ", endTag);
            }

            if (rowTagPos != -1) {
                int rowTagEnd = output.indexOf('>', rowTagPos);
                bool isSelfClosing = (rowTagEnd > 0 && output[rowTagEnd - 1] == '/');
                
                if (isSelfClosing) {
                    QByteArray rowContent = sortCellsInRow(rowCells);
                    QByteArray replacement = ">" + rowContent + "</row>";
                    output.replace(rowTagEnd - 1, 2, replacement);
                    sdEnd += replacement.size() - 2;
                } else {
                    int rowClose = output.indexOf("</row>", rowTagEnd);
                    if (rowClose != -1) {
                        QByteArray rowContent = output.mid(rowTagEnd + 1, rowClose - rowTagEnd - 1);
                        rowContent.append(rowCells);
                        rowContent = sortCellsInRow(rowContent);
                        int oldSize = rowClose - rowTagEnd - 1;
                        output.replace(rowTagEnd + 1, oldSize, rowContent);
                        sdEnd += rowContent.size() - oldSize;
                    }
                }
            } else {
                QByteArray newRow = QString("<row r=\"%1\">").arg(rowNum).toUtf8() + rowCells + "</row>";
                output.insert(insertPos, newRow);
                sdEnd += newRow.size();
            }
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
                       << "has" << ws.size() << "worksheet entries  ” skipping repair";
            continue;
        }

        const QByteArray needle = QByteArray("r:id=\"rId") + QByteArray::number(oldNum) + "\"";
        const int occurrences = sheetsBlock.count(needle);
        if (occurrences != 1) {
            qWarning() << "saveOpenXML: expected 1 sheet reference to" << needle
                       << "but found" << occurrences << " ” skipping repair";
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

    // Use cached ZIP entries if available  ” avoids re-opening the network file
    // which can crash with 0xC0000005 on large xlsm files.
    QByteArray wbXmlRaw;
    if (!wb.cachedZipEntries.isEmpty()) {
        wbXmlRaw = wb.cachedZipEntries.value("xl/workbook.xml");
    } else {
        // Fallback: open network file (may crash on large files)
        qWarning() << "saveOpenXML: no cached ZIP entries  ” falling back to network read";
        QZipReader reader(wb.filePath);
        if (!reader.isReadable()) {
            qWarning() << "ExcelHandler::saveOpenXML: cannot open" << wb.filePath;
            return false;
        }
        wbXmlRaw = reader.fileData("xl/workbook.xml");
    }
    qDebug() << "saveOpenXML: workbook.xml size=" << wbXmlRaw.size();
    qDebug() << "saveOpenXML [2] workbook.xml read OK";
    // Strip corrupted #REF! definedNames entries individually, preserving valid ones.
    // These accumulate in workbooks over time and can cause repair dialogs if
    // the entire block is removed (which also removes valid named ranges like print areas).
    {
        int dnStart = wbXmlRaw.indexOf("<definedNames>");
        int dnEnd   = wbXmlRaw.indexOf("</definedNames>");
        if (dnStart >= 0 && dnEnd > dnStart) {
            QByteArray block = wbXmlRaw.mid(dnStart + 14, dnEnd - dnStart - 14);
            int refCount  = block.count("#REF!");
            int nameCount = block.count("<definedName ");

            if (nameCount > 0 && refCount > 0) {
                // Rebuild the block keeping only entries that do NOT contain #REF!
                QByteArray cleaned;
                int pos = 0;
                int kept = 0, removed = 0;
                while (pos < block.size()) {
                    int entryStart = block.indexOf("<definedName ", pos);
                    if (entryStart == -1) break;
                    int entryEnd = block.indexOf("</definedName>", entryStart);
                    if (entryEnd == -1) {
                        // self-closing: <definedName ... />
                        int sc = block.indexOf("/>", entryStart);
                        if (sc == -1) break;
                        entryEnd = sc + 2;
                        QByteArray entry = block.mid(entryStart, entryEnd - entryStart);
                        if (!entry.contains("#REF!")) { cleaned.append(entry); kept++; }
                        else removed++;
                        pos = entryEnd;
                    } else {
                        entryEnd += 14; // include </definedName>
                        QByteArray entry = block.mid(entryStart, entryEnd - entryStart);
                        if (!entry.contains("#REF!")) { cleaned.append(entry); kept++; }
                        else removed++;
                        pos = entryEnd;
                    }
                }
                // Replace the block content with the cleaned version
                wbXmlRaw.replace(dnStart + 14, dnEnd - dnStart - 14, cleaned);
                qDebug() << "saveOpenXML: cleaned definedNames  ” kept:" << kept
                         << "removed:" << removed << "#REF! entries";
            }
        }
    }

    // Note: workbook.xml may be 3MB+ but we never rebuild it from scratch.
    // We only modify it when new sheets are added (rare), by doing a targeted
    // QByteArray::replace("</sheets>", ...) which is safe on large buffers.

    // Build sheetName â†’ sheetIndex from workbook.xml.
    // IMPORTANT: workbook.xml may be 3MB+ single-line XML  ” QXmlStreamReader crashes on it
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

    // Only rewrite sheets that were actually modified  ” merge changes into original XML
    QMap<QString, QByteArray> replacements; // zip path â†’ new XML bytes
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
    // No deduplication needed  ” they are already clean from the source file.

    QByteArray stylesXml = getEntry("xl/styles.xml");

    // If any sheet has highlight rows, inject styles once
    int blueXfIdx = -1, lightBlueXfIdx = -1;
    bool needsHighlight = !wb.highlightSourceRows.isEmpty();
    if (needsHighlight && !stylesXml.isEmpty()) {
        auto [bi, lbi] = injectHighlightStyles(stylesXml);
        blueXfIdx = bi;
        lightBlueXfIdx = lbi;
        if (blueXfIdx >= 0)
            replacements["xl/styles.xml"] = stylesXml;
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

        // New sheet not in workbook yet  ” create a new sheet part
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
            // Never take begin()/end() from that temporary  ” it will crash/UB.
            if (blueXfIdx >= 0 && lightBlueXfIdx >= 0) {
                auto rowsIt = wb.highlightSourceRows.constFind(sheetName);
                if (rowsIt != wb.highlightSourceRows.constEnd()) {
                    const QVector<int>& rows = rowsIt.value();
                    const QSet<int> srcSet(rows.cbegin(), rows.cend());
                    xml = applyRowHighlights(xml, srcSet, blueXfIdx, lightBlueXfIdx);
                }
            }

            widenSheetColumns(xml, 20.0);
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
        // The full sheet may have 100k+ cells from lazy loading  ” we must not merge all of them.
        SheetData changesOnly;
        changesOnly.name = sheetName;
        if (wb.dirtyCells.contains(sheetName)) {
            const QSet<QString>& dirty = wb.dirtyCells[sheetName];
            for (const QString& ref : dirty) {
                if (fullSheet.cells.contains(ref)) {
                    CellData cell = fullSheet.cells[ref];
                    cell.formula = ""; // value was explicitly set  ” clear any loaded formula
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

            // [DIAGNOSTIC] Check if mergeSheetXml produced duplicates for the cells we just wrote
            for (const QString& ref : changesOnly.cells.keys().mid(0, 10)) {
                QByteArray needle = QString("r=\"%1\"").arg(ref).toUtf8();
                int count = 0, searchPos = 0;
                while ((searchPos = merged.indexOf(needle, searchPos)) != -1) { count++; searchPos += needle.size(); }
                if (count > 1) qWarning() << "[MERGE DIAG]" << ref << "appears" << count << "times in merged XML";
            }

            // Deduplicate using regex (same pattern as diagnostic)
            // This pass picks the LAST occurrence of any cell ref (the one mergeSheetXml wrote)
            // and drops earlier ones (the original file's cells).
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
                            // Keep the last occurrence (our merged result).
                            // We NO LONGER salvage formulas from earlier duplicates here.
                            // If a formula was intentionally stripped by mergeSheetXml,
                            // we must not re-inject it.
                            out.append(cm.full);
                        } else {
                            dupes++;
                            qWarning() << "[DUP CELL REMOVED]" << cm.ref;
                        }
                        lastPos = cm.end;
                    }
                    out.append(input.mid(lastPos));
                    if (dupes > 0) {
                        qInfo() << "[DUP CELL] Removed" << dupes << "duplicates.";
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
            // Fast path: use cached ZIP entries  ” no network I/O at all
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

 //  Public API  ” all OpenXML, no COM

bool ExcelHandler::loadWorkbook(const QString& filePath, const QString& key,
                                const QSet<QString>& sheetsNeeded,
                                QString* errorOut)
{
    if (!QFile::exists(filePath)) {
        if (errorOut) *errorOut = QString("File not found: %1").arg(filePath);
        qWarning() << "ExcelHandler::loadWorkbook: file not found:" << filePath;
        return false;
    }

    // Parse outside the lock  ” each thread has its own QZipReader, fully safe
    WorkbookData wb;
    // Mark as save target if key ends with _cost_control  ” caches all ZIP entries upfront
    // so saveOpenXML never needs to re-open the network file.
    wb.isSaveTarget = key.endsWith("_cost_control");
    QString loadError;
    if (!loadOpenXML(filePath, wb, sheetsNeeded, &loadError)) {
        if (errorOut) *errorOut = loadError;
        qWarning() << "ExcelHandler::loadWorkbook: OpenXML parse failed for" << filePath
                   << ":" << loadError;
        return false;
    }

    if (errorOut) errorOut->clear();

    qDebug() << "ExcelHandler: loaded" << key << " ”" << wb.sheetNames.size()
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

    // Copy filePath out before taking reference  ” avoids any COW issues
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

    // Apply NumberFormat = General to copied sheets via COM
    // This is the Excel equivalent of Ctrl+A → Format Cells → General
    if (ok && !wb.rawSheetOverrides.isEmpty()) {
        QString comError;
        const QStringList sheetsToFormat = wb.rawSheetOverrides.keys();
        if (!ExcelHandler::applyNumberFormatGeneral(dest, sheetsToFormat, &comError)) {
            qWarning() << "[COM-FORMAT] Failed for" << dest << ":" << comError;
        }
    }

    // After a successful save, update the cached workbook path to the new file
    // so future saves read the latest state (prevents overwriting new sheets)
    if (ok) {
        if (m_workbooks.contains(key))
            m_workbooks[key].filePath = dest;

        // PowerShell repair removed  ” no longer needed after shared formula
        // stripping was fixed in the OpenXML merge pass.
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
        QString err = "COM: Excel.Application not available  ” is Excel installed?";
        qWarning() << "[COM-RECALC]" << err;
        if (errorOut) *errorOut = err;
        return false;
    }

    // Keep Excel hidden  ” no visible window
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
             << QVariant(2);        // CorruptLoad = xlRepairFile(2)  ” auto-repair, no dialog

        workbooks->dynamicCall("Open(QVariant&,QVariant&,QVariant&,QVariant&,QVariant&,"
                               "QVariant&,QVariant&,QVariant&,QVariant&,QVariant&,"
                               "QVariant&,QVariant&,QVariant&,QVariant&,QVariant&)", args);
        // The opened workbook is now the active workbook
        wb = excel.querySubObject("ActiveWorkbook");
    }

    if (!wb || wb->isNull()) {
        // Fallback: simple open  ” DisplayAlerts=false still suppresses dialogs
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

    // Save  ” DisplayAlerts=false ensures no "save as xlsm?" dialog appears
    wb->dynamicCall("Save()");

    qDebug() << "[COM-RECALC] Saved. Closing workbook...";

    // Close without prompting (DisplayAlerts already false)
    wb->dynamicCall("Close(bool)", false);

    excel.dynamicCall("Quit()");

    qDebug() << "[COM-RECALC] Done for:" << filePath;
    return true;
}

bool ExcelHandler::applyNumberFormatGeneral(const QString& filePath, const QStringList& sheetNames,
                                             QString* errorOut)
{
    if (sheetNames.isEmpty())
        return true;

    qDebug() << "[COM-FORMAT] Starting for:" << filePath << "sheets=" << sheetNames;

    // Remove Mark of the Web (Zone.Identifier ADS) to bypass Protected View
    // Equivalent to right-clicking the file → Properties → Unblock
    QFile::remove(filePath + ":Zone.Identifier");

    QAxObject excel("Excel.Application");
    if (excel.isNull()) {
        QString err = "COM: Excel.Application not available  – is Excel installed?";
        qWarning() << "[COM-FORMAT]" << err;
        if (errorOut) *errorOut = err;
        return false;
    }

    excel.setProperty("Visible", false);
    excel.setProperty("DisplayAlerts", false);
    excel.setProperty("AutomationSecurity", 1);  // msoAutomationSecurityLow — allow macros

    QAxObject* workbooks = excel.querySubObject("Workbooks");
    if (!workbooks) {
        QString err = "COM: Could not access Workbooks collection";
        qWarning() << "[COM-FORMAT]" << err;
        if (errorOut) *errorOut = err;
        excel.dynamicCall("Quit()");
        return false;
    }

    QAxObject* wb = nullptr;
    {
        QList<QVariant> args;
        args << QVariant(filePath)
             << QVariant(0)
             << QVariant(false)
             << QVariant(5)
             << QVariant("")
             << QVariant("")
             << QVariant(false)
             << QVariant(2)
             << QVariant("")
             << QVariant(false)
             << QVariant(false)
             << QVariant(0)
             << QVariant(false)
             << QVariant(false)
             << QVariant(2);
        workbooks->dynamicCall("Open(QVariant&,QVariant&,QVariant&,QVariant&,QVariant&,"
                               "QVariant&,QVariant&,QVariant&,QVariant&,QVariant&,"
                               "QVariant&,QVariant&,QVariant&,QVariant&,QVariant&)", args);
        wb = excel.querySubObject("ActiveWorkbook");
    }

    if (!wb || wb->isNull()) {
        qWarning() << "[COM-FORMAT] Open failed, trying simple Open...";
        if (wb) delete wb;
        wb = workbooks->querySubObject("Open(const QString&)", filePath);
    }

    if (!wb || wb->isNull()) {
        QString err = QString("COM: Failed to open workbook: %1").arg(filePath);
        qWarning() << "[COM-FORMAT]" << err;
        if (errorOut) *errorOut = err;
        excel.dynamicCall("Quit()");
        return false;
    }

    QAxObject* sheets = wb->querySubObject("Worksheets");
    if (!sheets) {
        QString err = "COM: Could not access Worksheets collection";
        qWarning() << "[COM-FORMAT]" << err;
        if (errorOut) *errorOut = err;
        wb->dynamicCall("Close(bool)", false);
        excel.dynamicCall("Quit()");
        return false;
    }

    // Build a VBA macro that formats all target sheets at once
    QString vbaCode = "Sub FixCurrencyFormat()\n";
    for (const QString& sheetName : sheetNames) {
        QString escaped = sheetName;
        escaped.replace("\"", "\"\"");
        vbaCode += QString("    ThisWorkbook.Sheets(\"%1\").Cells.NumberFormat = \"General\"\n").arg(escaped);
    }
    vbaCode += "End Sub\n";

    qDebug() << "[COM-FORMAT] Injecting VBA macro:\n" << vbaCode;

    QAxObject* vbProject = wb->querySubObject("VBProject");
    if (!vbProject || vbProject->isNull()) {
        qWarning() << "[COM-FORMAT] Cannot access VBProject — falling back to direct approach";
        for (const QString& sheetName : sheetNames) {
            QAxObject* sheet = sheets->querySubObject("Item(const QVariant&)", sheetName);
            if (sheet && !sheet->isNull()) {
                QAxObject* cells = sheet->querySubObject("Cells");
                if (cells && !cells->isNull()) {
                    cells->setProperty("NumberFormat", "General");
                    delete cells;
                }
                delete sheet;
            }
        }
    } else {
        QAxObject* vbComponents = vbProject->querySubObject("VBComponents");
        QAxObject* module = vbComponents->querySubObject("Add(int)", 1);
        QAxObject* codeModule = module ? module->querySubObject("CodeModule") : nullptr;
        if (codeModule && !codeModule->isNull()) {
            codeModule->dynamicCall("AddFromString(const QString&)", vbaCode);
            excel.dynamicCall("Run(const QString&)", "FixCurrencyFormat");
            qDebug() << "[COM-FORMAT] VBA macro executed successfully";
            vbComponents->dynamicCall("Remove(IDispatch*)", module->asVariant());
            qDebug() << "[COM-FORMAT] Temporary VBA module removed";
        } else {
            qWarning() << "[COM-FORMAT] Failed to get CodeModule — skipping VBA macro";
        }

        if (codeModule) delete codeModule;
        if (module) delete module;
        delete vbComponents;
        delete vbProject;
    }

    wb->dynamicCall("Save()");
    wb->dynamicCall("Close(bool)", false);
    excel.dynamicCall("Quit()");

    qDebug() << "[COM-FORMAT] Done for:" << filePath;
    return true;
}

bool ExcelHandler::repairAndSaveWithCOM(const QString& filePath, QString* errorOut)
{
    // Opens the file in hidden Excel with CorruptLoad=xlRepairFile(2) which silently
    // accepts any repair dialog, then immediately saves and closes.
    // This fixes shared formula XML inconsistencies written by our OpenXML patcher.
    qDebug() << "[COM-REPAIR] Starting for:" << filePath;

    QAxObject excel("Excel.Application");
    if (excel.isNull()) {
        QString err = "COM: Excel.Application not available  ” is Excel installed?";
        qWarning() << "[COM-REPAIR]" << err;
        if (errorOut) *errorOut = err;
        return false;
    }

    excel.setProperty("Visible", false);
    excel.setProperty("DisplayAlerts", false);
    excel.setProperty("AutomationSecurity", 3);

    QAxObject* workbooks = excel.querySubObject("Workbooks");
    if (!workbooks) {
        QString err = "COM: Could not access Workbooks collection";
        if (errorOut) *errorOut = err;
        excel.dynamicCall("Quit()");
        return false;
    }

    QAxObject* wb = nullptr;
    {
        QList<QVariant> args;
        args << QVariant(filePath) << QVariant(0) << QVariant(false)
             << QVariant(5) << QVariant("") << QVariant("")
             << QVariant(false) << QVariant(2) << QVariant("")
             << QVariant(false) << QVariant(false) << QVariant(0)
             << QVariant(false) << QVariant(false) << QVariant(2); // CorruptLoad=xlRepairFile

        workbooks->dynamicCall("Open(QVariant&,QVariant&,QVariant&,QVariant&,QVariant&,"
                               "QVariant&,QVariant&,QVariant&,QVariant&,QVariant&,"
                               "QVariant&,QVariant&,QVariant&,QVariant&,QVariant&)", args);
        wb = excel.querySubObject("ActiveWorkbook");
    }

    if (!wb || wb->isNull()) {
        if (wb) delete wb;
        wb = workbooks->querySubObject("Open(const QString&)", filePath);
    }

    if (!wb) {
        QString err = QString("COM: Failed to open workbook: %1").arg(filePath);
        if (errorOut) *errorOut = err;
        excel.dynamicCall("Quit()");
        return false;
    }

    wb->dynamicCall("Save()");
    wb->dynamicCall("Close(bool)", false);
    excel.dynamicCall("Quit()");

    qDebug() << "[COM-REPAIR] Done for:" << filePath;
    return true;
}

bool ExcelHandler::silentRepairWithPowerShell(const QString& filePath, QString* errorOut)
{
    // Find repair.ps1 next to the executable
    QString scriptPath = QCoreApplication::applicationDirPath() + "/repair.ps1";
    if (!QFile::exists(scriptPath)) {
        scriptPath = QCoreApplication::applicationDirPath() + "/../repair.ps1";
    }
    if (!QFile::exists(scriptPath)) {
        if (errorOut) *errorOut = "repair.ps1 not found next to executable";
        qWarning() << "[REPAIR] repair.ps1 not found";
        return false;
    }

    QStringList args;
    args << "-NoProfile"
         << "-NonInteractive"
         << "-ExecutionPolicy" << "Bypass"
         << "-File" << scriptPath
         << "-FilePath" << QDir::toNativeSeparators(filePath);

    QProcess process;
    process.setProgram("powershell.exe");
    process.setArguments(args);
    process.setProcessChannelMode(QProcess::MergedChannels);

    qInfo() << "[REPAIR] Launching PowerShell repair for:" << filePath;
    
    QThread::msleep(2000); // 2-second FS flush safety barrier

    int maxAttempts = 3;
    bool success = false;
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        process.start();

        if (!process.waitForFinished(120000)) {
            process.kill();
            if (errorOut) *errorOut = "PowerShell repair timed out (120s)";
            qWarning() << "[REPAIR] Timed out";
            return false;
        }

        QString output = QString::fromUtf8(process.readAll()).trimmed();
        int exitCode = process.exitCode();

        if (exitCode == 0 && output.contains("OK")) {
            qInfo() << "[REPAIR] Success  ” file repaired and saved cleanly";
            success = true;
            break;
        } else {
            if (!output.contains("Unable to get the Open")) {
                QString err = QString("PowerShell exit=%1: %2").arg(exitCode).arg(output);
                if (errorOut) *errorOut = err;
                qWarning() << "[REPAIR]" << err;
                return false;
            }
            // If it IS "Unable to get the Open", sleep and loop
            qWarning() << "[REPAIR] Attempt" << attempt + 1 << "locked. Retrying...";
            QThread::msleep(3000);
        }
    }
    return success;
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

 //  Cell read / write  ” pure in-memory on the loaded WorkbookData

QVariant ExcelHandler::getCellValue(const QString& key, const QString& sheetName, int row, int col)
{
    if (sheetName.trimmed().isEmpty() || !isValidCellAddress(row, col))
        return QVariant();
    QWriteLocker locker(&m_lock); // write lock needed  ” getSheet may lazy-load
    if (!m_workbooks.contains(key))
        return QVariant();

    SheetData& sheet = getSheet(key, sheetName);
    QString cellRef  = buildCellRef(columnToLetter(col), row);
    auto cellIt      = sheet.cells.find(cellRef);
    if (cellIt == sheet.cells.end())
        return QVariant();

    const QString& val = cellIt->value;
    bool ok = false;
    double d = parseNumericString(val, &ok);
    if (ok) return d;
    return val;
}

bool ExcelHandler::setCellValue(const QString& key, const QString& sheetName, int row, int col, const QVariant& value)
{
    if (sheetName.trimmed().isEmpty() || !isValidCellAddress(row, col)) {
        qWarning() << "ExcelHandler::setCellValue: invalid cell target" << sheetName << row << col;
        return false;
    }
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
    cell.formula = QString(); // Clear any existing formula — this cell now holds a literal value

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
    if (sheetName.trimmed().isEmpty() || !isValidCellAddress(row, col)) {
        qWarning() << "ExcelHandler::setCellFormula: invalid cell target" << sheetName << row << col;
        return false;
    }
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

bool ExcelHandler::deleteCellValue(const QString& key, const QString& sheetName, int row, int col)
{
    if (sheetName.trimmed().isEmpty() || !isValidCellAddress(row, col))
        return false;
    QWriteLocker locker(&m_lock);
    if (!m_workbooks.contains(key))
        return false;

    WorkbookData& wb = m_workbooks[key];
    SheetData& sheet = getSheet(key, sheetName);
    QString cellRef  = buildCellRef(columnToLetter(col), row);

    // Write a blank CellData (empty value, empty formula, empty dataType).
    // saveOpenXML's changesOnly loop checks fullSheet.cells.contains(ref) —
    // if we remove the cell it won't find it and will leave the original <c>
    // element untouched.  By keeping a blank entry, mergeSheetXml emits a
    // self-closing <c r="X"/> which Excel treats as an empty/deleted cell,
    // equivalent to the user pressing Delete.
    CellData& cell  = sheet.cells[cellRef];
    cell.row        = row;
    cell.col        = col;
    cell.value.clear();
    cell.formula.clear();
    cell.dataType.clear();

    wb.dirtyCells[sheetName].insert(cellRef);
    wb.modifiedSheets.insert(sheetName);
    return true;
}

QString ExcelHandler::getCellFormula(const QString& key, const QString& sheetName, int row, int col)
{
    if (sheetName.trimmed().isEmpty() || !isValidCellAddress(row, col))
        return QString();
    QWriteLocker locker(&m_lock);
    if (!m_workbooks.contains(key))
        return QString();

    SheetData& sheet = getSheet(key, sheetName);
    QString cellRef  = buildCellRef(columnToLetter(col), row);
    auto cellIt      = sheet.cells.find(cellRef);
    if (cellIt == sheet.cells.end())
        return QString();

    return cellIt->formula;
}

 //  transferData  ” fully in-memory, no COM (ported from Excel_transfer_23_3)

int ExcelHandler::transferData(const QString& srcKey, const QString& srcSheet, const QString& srcCol,
                               const QVector<int>& srcRows, const QString& destKey, const QString& destSheet,
                               const QString& destCol, const QVector<int>& destRows,
                               const QString& sourceFileType, bool divideBy1000)
{
    if (srcSheet.trimmed().isEmpty() || destSheet.trimmed().isEmpty()) {
        qWarning() << "ExcelHandler::transferData: source/destination sheet name is empty";
        return 0;
    }
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
    if (srcColIdx <= 0 || destColIdx <= 0) {
        qWarning() << "ExcelHandler::transferData: invalid column mapping" << srcCol << destCol;
        return 0;
    }
    int transferred = 0;


        for (int i = 0; i < srcRows.size(); ++i) {
        if (!isValidCellAddress(srcRows[i], srcColIdx) || !isValidCellAddress(destRows[i], destColIdx)) {
            qWarning() << "ExcelHandler::transferData: invalid row mapping index" << i
                       << "srcRow" << srcRows[i] << "destRow" << destRows[i];
            continue;
        }
        QString srcRef  = buildCellRef(columnToLetter(srcColIdx), srcRows[i]);
        QString destRef = buildCellRef(columnToLetter(destColIdx), destRows[i]);

        auto cellIt = srcSheetCopy.cells.find(srcRef);
        double numVal = 0.0;
        bool isNum = false;
        if (cellIt != srcSheetCopy.cells.end()) {
            if (!cellIt->value.isEmpty()) {
                numVal = parseNumericString(cellIt->value, &isNum);
                if (!isNum) {
                    // Still not a number after cleaning -> write 0 to dest
                    isNum = true;
                    numVal = 0.0;
                }
            } else {
                // Cell exists but has no cached value (e.g. formula with no <v> tag)  ” use 0
                isNum = true;
                numVal = 0.0;
            }
        } else {
            // Missing source cell  ” use 0
            isNum = true;
            numVal = 0.0;
        }

        CellData& dCell = dstSheet.cells[destRef];
        dCell.row     = destRows[i];
        dCell.col     = destColIdx;
        dCell.formula.clear();  // never copy source formula  ” raw value only

        if (isNum) {
            // Rows 12, 13, 16 are headcount rows — never divide or round by 1000
            const bool isHeadcountRow = (destRows[i] == 12 || destRows[i] == 13 || destRows[i] == 16);
            if (divideBy1000 && !isHeadcountRow) {
                if (sourceFileType == "pax") {
                    const bool shouldDivide = (destRows[i] >= 5 && destRows[i] <= 7);
                    if (shouldDivide) {
                        numVal = numVal / 1000.0;
                        numVal = std::round(numVal * 100000.0) / 100000.0;
                    }
                } else if (destRows[i] == 212 || destRows[i] == 216 || destRows[i] == 218 || destRows[i] == 214
                           || destRows[i] == 222 || destRows[i] == 226) {
                    // Rows 212-226 PAX/EUR section must not be sign-flipped
                    numVal /= 1000.0;
                    numVal = std::round(numVal * 100000.0) / 100000.0;
                } else {
                    numVal /= -1000.0;
                    numVal = std::round(numVal * 100000.0) / 100000.0;
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

 //  copyData / copyFullSheet

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

// Apply dark blue (sourceRows) / light blue (all other rows) fill to row elements in sheet XML.
// styleBlue and styleLightBlue are xf indices in the destination styles.xml.
static QByteArray applyRowHighlights(const QByteArray& sheetXml,
                                     const QSet<int>& sourceRows,
                                     int styleBlue, int styleLightBlue)
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
            int styleIdx = sourceRows.contains(rowNum) ? styleBlue : styleLightBlue;

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

    //  Helper: read count="N" from a tag, return N and the byte range [countStart, countEnd]
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

    // Dark blue for mapped rows (matching JSON mappings)
    const QByteArray blueFill =
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FF1F4E79\"/><bgColor indexed=\"64\"/></patternFill></fill>";
    // Light blue for other rows
    const QByteArray lightBlueFill =
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFBDD7EE\"/><bgColor indexed=\"64\"/></patternFill></fill>";

    int fillCount = readCount("<fills ");
    if (fillCount < 0) return {-1,-1};
    int blueFillIdx = fillCount;
    int lightBlueFillIdx = fillCount + 1;

    // Insert fills then update count (count tag comes BEFORE </fills>, so update first)
    updateCount("<fills ", fillCount + 2);
    stylesXml.replace("</fills>", blueFill + lightBlueFill + "</fills>");

    int xfCount = readCount("<cellXfs");
    if (xfCount < 0) return {-1,-1};
    int blueXfIdx = xfCount;
    int lightBlueXfIdx = xfCount + 1;

    // Dark blue with white text and underline for mapped rows
    QByteArray blueXf = QString(
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"%1\" borderId=\"0\" applyFill=\"1\" applyFont=\"1\">"
        "<font><color rgb=\"FFFFFFFF\"/><u/></font></xf>")
        .arg(blueFillIdx).toUtf8();
    // Light blue with default text for other rows
    QByteArray lightBlueXf = QString(
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"%1\" borderId=\"0\" applyFill=\"1\"/>")
        .arg(lightBlueFillIdx).toUtf8();

    // Update count before inserting (count tag comes before </cellXfs>)
    updateCount("<cellXfs", xfCount + 2);
    stylesXml.replace("</cellXfs>", blueXf + lightBlueXf + "</cellXfs>");

    return {blueXfIdx, lightBlueXfIdx};
}

bool ExcelHandler::copyFullSheet(const QString& srcKey, const QString& srcSheet,
                                 const QString& destKey, const QString& newSheetName,
                                 const QVector<int>& highlightRows)
{
    QWriteLocker locker(&m_lock);

    if (!m_workbooks.contains(srcKey) || !m_workbooks.contains(destKey)) {
        qWarning() << "[copyFullSheet] Missing workbook:" << srcKey << "or" << destKey;
        return false;
    }

    WorkbookData& src = m_workbooks[srcKey];
    WorkbookData& dst = m_workbooks[destKey];
    
    QString targetName = newSheetName.isEmpty() ? srcSheet : newSheetName;
    auto ensureZipCache = [](WorkbookData& wb, const QString& wbKey) -> bool {
        if (!wb.cachedZipEntries.isEmpty())
            return true;
        if (wb.filePath.trimmed().isEmpty()) {
            qWarning() << "[copyFullSheet] Empty workbook file path for key:" << wbKey;
            return false;
        }

        QZipReader zip(wb.filePath);
        if (!zip.isReadable()) {
            qWarning() << "[copyFullSheet] Unable to open ZIP for key:" << wbKey
                       << "path:" << wb.filePath;
            return false;
        }
        for (const QZipReader::FileInfo& fi : zip.fileInfoList()) {
            if (!fi.isDir)
                wb.cachedZipEntries[fi.filePath] = zip.fileData(fi.filePath);
        }
        qInfo() << "[copyFullSheet] Cached" << wb.cachedZipEntries.size()
                << "entries for key:" << wbKey;
        return !wb.cachedZipEntries.isEmpty();
    };

    if (!ensureZipCache(src, srcKey) || !ensureZipCache(dst, destKey)) {
        qWarning() << "[copyFullSheet] Required ZIP cache unavailable";
        return false;
    }

    //    Step 1: Find source sheet path in ZIP
    QString srcSheetPath = src.sheetPathMap.value(srcSheet);
    if (srcSheetPath.isEmpty()) {
        for (auto it = src.sheetPathMap.constBegin(); it != src.sheetPathMap.constEnd(); ++it) {
            if (it.key().trimmed().compare(srcSheet.trimmed(), Qt::CaseInsensitive) == 0) {
                srcSheetPath = it.value();
                break;
            }
        }
        if (srcSheetPath.isEmpty()) {
            qWarning() << "[copyFullSheet] Source sheet not found:" << srcSheet;
            return false;
        }
    }

    // Normalize to ZIP-internal path (remove leading slash if present)
    if (srcSheetPath.startsWith("/"))
        srcSheetPath = srcSheetPath.mid(1);

    if (!src.cachedZipEntries.contains(srcSheetPath)) {
        qWarning() << "[copyFullSheet] Source sheet XML not in cache:" << srcSheetPath;
        return false;
    }

    //    Step 2: Determine destination sheet path
    QString dstSheetPath;
    bool isNewSheet = false;
    int dstSheetNum = 0;
    
    if (dst.sheetNames.contains(targetName) && dst.sheetPathMap.contains(targetName)) {
        dstSheetPath = dst.sheetPathMap[targetName];
        qInfo() << "[copyFullSheet] Overwriting existing sheet:" << targetName << "at" << dstSheetPath;
        
        // Extract the sheet number from the existing path (e.g., xl/worksheets/sheet3.xml)
        QRegularExpression sheetNumRe("sheet(\\d+)\\.xml$", QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch m = sheetNumRe.match(dstSheetPath);
        if (m.hasMatch()) {
            dstSheetNum = m.captured(1).toInt();
        } else {
            dstSheetNum = 999; // Fallback
        }
    } else {
        isNewSheet = true;
        
        // Find next available sheetN.xml in destination
        int maxSheetNum = 0;
        QRegularExpression sheetNumRe("sheet(\\d+)\\.xml$", QRegularExpression::CaseInsensitiveOption);
        for (auto it = dst.cachedZipEntries.constBegin(); it != dst.cachedZipEntries.constEnd(); ++it) {
            QRegularExpressionMatch m = sheetNumRe.match(it.key());
            if (m.hasMatch()) {
                maxSheetNum = qMax(maxSheetNum, m.captured(1).toInt());
            }
        }
        dstSheetNum = maxSheetNum + 1;
        dstSheetPath = QString("xl/worksheets/sheet%1.xml").arg(dstSheetNum);
    }

    qInfo() << "[copyFullSheet]" << srcSheet << "->" << targetName
            << "| src:" << srcSheetPath << "| dst:" << dstSheetPath;

    //    Step 3: Copy sheet XML with shared string and style remapping
    QByteArray sheetXml = src.cachedZipEntries[srcSheetPath];

    // Merge styles (no-op if same workbook) and shared strings
    MergeStylesResult styleResult = mergeStyles(src, dst);
    mergeSharedStrings(src, dst, sheetXml);

    // Only shift s= indices if copying from a different workbook
    if (styleResult.valid && src.filePath != dst.filePath && styleResult.xfOffsetLightBlue > 0) {
        QString sheetStr = QString::fromUtf8(sheetXml);
        QRegularExpression sRe("\\bs=\"(\\d+)\"");
        int adj = 0;
        auto it = sRe.globalMatch(sheetStr);
        while (it.hasNext()) {
            auto m = it.next();
            int pos = m.capturedStart(1) + adj;
            int len = m.capturedLength(1);
            int oldS = m.captured(1).toInt();
            QString newS = QString::number(oldS + styleResult.xfOffsetLightBlue);
            sheetStr.replace(pos, len, newS);
            adj += newS.length() - len;
        }
        sheetXml = sheetStr.toUtf8();
    }

    // Do not copy sheet-level protection flags from source sheet.
    {
        QString sheetXmlStr = QString::fromUtf8(sheetXml);
        QRegularExpression sheetProtectionRe(
            "<(?:\\w+:)?sheetProtection\\b[^>]*/>|"
            "<(?:\\w+:)?sheetProtection\\b[^>]*>.*?</(?:\\w+:)?sheetProtection>",
            QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption
        );
        sheetXmlStr.remove(sheetProtectionRe);
        sheetXml = sheetXmlStr.toUtf8();
    }

    //    Step 4: Copy sheet relationships (drawings, charts, comments, etc.)
    // Source rels: xl/worksheets/_rels/sheet3.xml.rels
    int srcLastSlash = srcSheetPath.lastIndexOf('/');
    QString srcDir = srcSheetPath.left(srcLastSlash + 1);
    QString srcFile = srcSheetPath.mid(srcLastSlash + 1);
    QString srcRelsPath = srcDir + "_rels/" + srcFile + ".rels";

    int dstLastSlash = dstSheetPath.lastIndexOf('/');
    QString dstDir = dstSheetPath.left(dstLastSlash + 1);
    QString dstFile = dstSheetPath.mid(dstLastSlash + 1);
    QString dstRelsPath = dstDir + "_rels/" + dstFile + ".rels";

    if (src.cachedZipEntries.contains(srcRelsPath)) {
        QByteArray relsXml = src.cachedZipEntries[srcRelsPath];
        QString relsStr = QString::fromUtf8(relsXml);

        // Parse each relationship target and copy the referenced files
        QRegularExpression relRe(
            "<Relationship\\s[^>]*Target=\"([^\"]+)\"[^>]*Type=\"([^\"]+)\"[^/]*/>",
            QRegularExpression::DotMatchesEverythingOption
        );
        QRegularExpressionMatchIterator relMatches = relRe.globalMatch(relsStr);

        while (relMatches.hasNext()) {
            QRegularExpressionMatch rm = relMatches.next();
            QString relTarget = rm.captured(1);
            QString relType = rm.captured(2);

            // Skip external relationships (URLs, etc.)
            if (relTarget.startsWith("http://") || relTarget.startsWith("https://")
                || relTarget.startsWith("mailto:")) {
                continue;
            }

            // Resolve absolute paths within ZIP
            QString absSrc = resolveRelPath(srcDir, relTarget);
            if (absSrc.isEmpty() || !src.cachedZipEntries.contains(absSrc)) {
                qDebug() << "[copyFullSheet] Skipping missing rel target:" << relTarget
                         << "resolved:" << absSrc;
                continue;
            }

            // Generate unique destination path
            QString newTarget = generateUniqueTarget(dst, srcDir, relTarget);  // Pass srcDir as baseDir
            QString absDst = resolveRelPath(dstDir, newTarget);

            // Copy the file into destination ZIP cache
            dst.cachedZipEntries[absDst] = src.cachedZipEntries[absSrc];

            // Register content type for known file types
            registerContentType(dst, absDst, relType);

            // Update relationship target in rels XML
            relsStr.replace(
                QString("Target=\"%1\"").arg(relTarget),
                QString("Target=\"%1\"").arg(newTarget)
            );

            // Recursively copy sub-relationships (e.g. chart -> style, colors)
            QSet<QString> visited;  // Track visited paths to prevent infinite recursion
            copySubRels(src, dst, absSrc, absDst, &visited);
        }

        // Store the updated rels file in destination
        dst.cachedZipEntries[dstRelsPath] = relsStr.toUtf8();
    }

    //    Step 5: Convert shared strings to inline, then convert any inline string cells
    //    that contain numeric/currency values into proper <v> numeric cells.
    //    This mirrors what transferEntry does (getCellValue → toDouble → setCellValue),
    //    ensuring no currency symbols survive in the copied sheet.
    {
        // First resolve shared string indices to inline text
        sheetXml = convertSharedToInline(sheetXml, src.sharedStrings);

        // Now convert inline string cells that contain numeric/currency text to <v> cells
        QString xmlStr = QString::fromUtf8(sheetXml);

        // Match full <c ...> ... </c> cells that are inlineStr
        static const QRegularExpression inlineCellRx(
            QStringLiteral("<c\\b([^>]*)t=\"inlineStr\"([^>]*)>(.*?)</c>"),
            QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression tTextRx(
            QStringLiteral("<t[^>]*>([^<]*)</t>"),
            QRegularExpression::DotMatchesEverythingOption);
        static const QRegularExpression currencySymRx(
            QString::fromUtf8("[$\xe2\x82\xac\xc2\xa3\xc2\xa5\xe2\x82\xb9]")); // $, €, £, ¥, ₹

        struct Hit { qsizetype start; qsizetype len; QString replacement; };
        QVector<Hit> hits;

        auto it = inlineCellRx.globalMatch(xmlStr);
        while (it.hasNext()) {
            auto m = it.next();
            auto tMatch = tTextRx.match(m.captured(3));
            if (!tMatch.hasMatch()) continue;

            QString text = tMatch.captured(1).trimmed();

            bool ok = false;
            double val = parseNumericString(text, &ok);
            if (!ok) continue;

            // Build replacement: drop t="inlineStr", replace <is><t>...</t></is> with <v>number</v>
            QString attrs1 = m.captured(1);
            QString attrs2 = m.captured(2);
            QString numStr = QString::number(val, 'f', 2);
            QString replacement = QStringLiteral("<c") + attrs1 + attrs2 + QStringLiteral("><v>")
                                + numStr + QStringLiteral("</v></c>");
            hits.append({ m.capturedStart(0), m.capturedLength(0), replacement });
        }

        int converted = 0;
        for (int i = hits.size() - 1; i >= 0; --i) {
            xmlStr.replace(hits[i].start, hits[i].len, hits[i].replacement);
            converted++;
        }

        if (converted > 0) {
            sheetXml = xmlStr.toUtf8();
            qDebug() << "[copyFullSheet] Converted" << converted << "currency text cells to numeric";
        }
    }

    // Step 5b: Strip style= from <col> elements to prevent inherited currency formats
    // Cells without explicit s= attribute inherit from <col style="X">, which may point to currency formats
    {
        QString xmlStr = QString::fromUtf8(sheetXml);
        static const QRegularExpression colStyleRe(
            QStringLiteral("(<col\\b[^>]*?)\\s*style=\"\\d+\"([^>]*?)"),
            QRegularExpression::CaseInsensitiveOption);
        xmlStr.replace(colStyleRe, "\\1\\2");
        sheetXml = xmlStr.toUtf8();
    }

    // Use rawSheetOverrides so the save logic picks it up
    dst.rawSheetOverrides[targetName] = sheetXml;
    dst.cachedZipEntries[dstSheetPath] = sheetXml;

    if (isNewSheet) {
        //    Step 6: Register sheet in workbook.xml
        int newSheetId = dstSheetNum + 100; // fallback
        {
            const QString wbXml = QString::fromUtf8(dst.cachedZipEntries.value("xl/workbook.xml"));
            QRegularExpression sheetIdRe("sheetId=\"(\\d+)\"");
            QRegularExpressionMatchIterator idIt = sheetIdRe.globalMatch(wbXml);
            int maxSheetId = 0;
            while (idIt.hasNext()) {
                maxSheetId = qMax(maxSheetId, idIt.next().captured(1).toInt());
            }
            if (maxSheetId > 0)
                newSheetId = maxSheetId + 1;
        }
        QString rId = addWorkbookSheet(dst, targetName, newSheetId);

        //    Step 7: Add workbook relationship
        // xl/_rels/workbook.xml.rels
        QString wbRelsPath = "xl/_rels/workbook.xml.rels";
        if (dst.cachedZipEntries.contains(wbRelsPath)) {
            QString wbRels = QString::fromUtf8(dst.cachedZipEntries[wbRelsPath]);
            QString relEntry = QString(
                "<Relationship Id=\"%1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet%2.xml\"/>")
                .arg(rId).arg(dstSheetNum);

            wbRels.replace("</Relationships>", relEntry + "</Relationships>");
            dst.cachedZipEntries[wbRelsPath] = wbRels.toUtf8();
        }

        //    Step 8: Register in [Content_Types].xml
        addContentType(dst, dstSheetPath,
                       "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml");

        //    Step 9: Update sheetMap and sheetNames
        dst.sheetPathMap[targetName] = dstSheetPath;
        dst.sheetNames.append(targetName);
    }

    // ── Step 10: Apply row colors using two-batch XF offsets ──
    if (styleResult.valid && !highlightRows.isEmpty()) {
        QSet<int> mappingRowSet(highlightRows.begin(), highlightRows.end());
        applyRowColors(dst, dstSheetPath, mappingRowSet, 0, 0);
    }
    if (!highlightRows.isEmpty()) {
        dst.highlightSourceRows[targetName] = highlightRows;
    } else {
        dst.highlightSourceRows.remove(targetName);
    }

    //    Step 11: Mark as modified
    dst.modifiedSheets.insert(targetName);

    qInfo() << "[copyFullSheet] SUCCESS: copied" << srcSheet << "as" << targetName
            << "(" << sheetXml.size() << "bytes,"
            << (src.cachedZipEntries.contains(srcRelsPath) ? "with" : "no") << "relationships)";

    return true;
}

 //  Public wrapper for applying row colors

bool ExcelHandler::applyRowColorsToSheet(const QString& key, const QString& sheetName, const QSet<int>& mappingRows)
{
    QWriteLocker locker(&m_lock);
    
    if (!m_workbooks.contains(key)) {
        qWarning() << "[applyRowColorsToSheet] Workbook not loaded:" << key;
        return false;
    }
    
    WorkbookData& wb = m_workbooks[key];
    QString sheetPath = wb.sheetPathMap.value(sheetName);
    
    if (sheetPath.isEmpty()) {
        qWarning() << "[applyRowColorsToSheet] Sheet not found:" << sheetName;
        return false;
    }
    
    // Standalone call — create new style entries for coloring
    // and overwrite all cell s= attributes directly (not offset-based)
    QByteArray& stylesXml = wb.cachedZipEntries["xl/styles.xml"];
    int darkBlueFillId    = appendFill(stylesXml, "FF1F4E79");
    int lightBlueFillId   = appendFill(stylesXml, "FFBDD7EE");
    int fontCount = 0;
    QByteArray fonts = extractStyleSection(stylesXml, "fonts", fontCount);
    fonts.append("<font><color rgb=\"FFFFFFFF\"/><sz val=\"11\"/><name val=\"Calibri\"/></font>");
    replaceStyleSection(stylesXml, "fonts", fonts, fontCount + 1);
    int darkBlueStyleIdx  = appendCellXf(stylesXml, fontCount,   darkBlueFillId,  0);
    int lightBlueStyleIdx = appendCellXf(stylesXml, 0,           lightBlueFillId, 0);

    // Overwrite all s= values directly (not offset-based) using a simple pass
    QString sheetStr = QString::fromUtf8(wb.cachedZipEntries[sheetPath]);
    QRegularExpression rowNumRe2("\\br=\"(\\d+)\"");
    QRegularExpression sAttrRe2("\\bs=\"\\d+\"");
    int curRow2 = 0, pos2 = 0;
    QString out2; out2.reserve(sheetStr.size());
    while (pos2 < sheetStr.size()) {
        int nr = sheetStr.indexOf("<row ", pos2), nc = sheetStr.indexOf("<c ", pos2);
        int nt = -1; bool ir = false;
        if (nr != -1 && (nc == -1 || nr < nc)) { nt = nr; ir = true; }
        else if (nc != -1) { nt = nc; }
        if (nt == -1) { out2.append(sheetStr.mid(pos2)); break; }
        out2.append(sheetStr.mid(pos2, nt - pos2));
        int te = sheetStr.indexOf('>', nt);
        if (te == -1) { out2.append(sheetStr.mid(nt)); break; }
        QString tag = sheetStr.mid(nt, te - nt + 1);
        if (ir) { auto rm = rowNumRe2.match(tag); if (rm.hasMatch()) curRow2 = rm.captured(1).toInt(); }
        else {
            int si = mappingRows.contains(curRow2) ? darkBlueStyleIdx : lightBlueStyleIdx;
            if (sAttrRe2.match(tag).hasMatch()) tag.replace(sAttrRe2, QString("s=\"%1\"").arg(si));
            else tag.insert(3, QString(" s=\"%1\"").arg(si));
        }
        out2.append(tag); pos2 = te + 1;
    }
    wb.cachedZipEntries[sheetPath] = out2.toUtf8();
    return true;
}

 //  File finders

QString ExcelHandler::findCostControlFile(const QString& basePath, const QString& month, int year)
{
    QString monthNum = MONTH_TO_NUM.value(month, "01");

    // Files must be in {base}/{year}/{monthNum}/test/  ” no fallback
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
               << " ” files:" << dir.entryList(QDir::Files);
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


// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
//  Enhanced copyFullSheet Helper Functions
// â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

QString ExcelHandler::resolveRelPath(const QString& basePath, const QString& relTarget)
{
    if (relTarget.startsWith("/")) {
        return relTarget.mid(1); // absolute within ZIP
    }
    
    // Handle "../" relative paths
    QStringList baseParts = basePath.split("/", Qt::SkipEmptyParts);
    QStringList relParts = relTarget.split("/", Qt::SkipEmptyParts);
    
    for (const QString& part : relParts) {
        if (part == "..") {
            if (!baseParts.isEmpty()) baseParts.removeLast();
        } else {
            baseParts.append(part);
        }
    }
    
    return baseParts.join("/");
}

QString ExcelHandler::generateUniqueTarget(WorkbookData& dst,
                                          const QString& baseDir,
                                          const QString& originalRelTarget)
{
    // Extract directory and extension
    int lastSlash = originalRelTarget.lastIndexOf('/');
    QString dir = (lastSlash >= 0) ? originalRelTarget.left(lastSlash + 1) : "";
    QString filename = (lastSlash >= 0) ? originalRelTarget.mid(lastSlash + 1) : originalRelTarget;
    
    int dotPos = filename.lastIndexOf('.');
    QString base = (dotPos >= 0) ? filename.left(dotPos) : filename;
    QString ext = (dotPos >= 0) ? filename.mid(dotPos) : "";
    
    // Check if the target already exists in destination
    QString candidate = originalRelTarget;
    QString absCand = resolveRelPath(baseDir, candidate);  // Use actual base directory
    int counter = 1;
    
    while (dst.cachedZipEntries.contains(absCand)) {
        candidate = QString("%1%2_copy%3%4").arg(dir, base).arg(counter).arg(ext);
        absCand = resolveRelPath(baseDir, candidate);
        counter++;
    }
    
    return candidate;
}

void ExcelHandler::copySubRels(WorkbookData& src, WorkbookData& dst,
                               const QString& srcAbsPath, const QString& dstAbsPath,
                               QSet<QString>* visited)
{
    // Prevent infinite recursion from circular references
    QSet<QString> localVisited;
    if (!visited) visited = &localVisited;
    
    if (visited->contains(srcAbsPath)) {
        qDebug() << "[copySubRels] Circular reference detected, skipping:" << srcAbsPath;
        return;
    }
    visited->insert(srcAbsPath);
    
    // Find the rels file for this path
    // e.g. xl/charts/chart1.xml â†’ xl/charts/_rels/chart1.xml.rels
    int lastSlash = srcAbsPath.lastIndexOf('/');
    QString dir = srcAbsPath.left(lastSlash + 1);
    QString file = srcAbsPath.mid(lastSlash + 1);
    QString srcRels = dir + "_rels/" + file + ".rels";
    
    if (!src.cachedZipEntries.contains(srcRels)) {
        return; // no sub-relationships
    }
    
    // Build destination rels path
    int dstLastSlash = dstAbsPath.lastIndexOf('/');
    QString dstDir = dstAbsPath.left(dstLastSlash + 1);
    QString dstFile = dstAbsPath.mid(dstLastSlash + 1);
    QString dstRels = dstDir + "_rels/" + dstFile + ".rels";
    
    QByteArray relsXml = src.cachedZipEntries[srcRels];
    QRegularExpression targetRe("Target=\"([^\"]+)\"");
    QRegularExpressionMatchIterator matches = targetRe.globalMatch(QString::fromUtf8(relsXml));
    
    QString relsStr = QString::fromUtf8(relsXml);
    
    while (matches.hasNext()) {
        QRegularExpressionMatch match = matches.next();
        QString relTarget = match.captured(1);
        QString absSrc = resolveRelPath(dir, relTarget);
        
        if (absSrc.isEmpty() || !src.cachedZipEntries.contains(absSrc)) {
            continue;
        }
        
        QString newTarget = generateUniqueTarget(dst, dir, relTarget);  // Pass dir as baseDir
        QString absDst = resolveRelPath(dstDir, newTarget);
        
        dst.cachedZipEntries[absDst] = src.cachedZipEntries[absSrc];
        relsStr.replace(
            QString("Target=\"%1\"").arg(relTarget),
            QString("Target=\"%1\"").arg(newTarget)
        );
        
        // Recurse with visited set
        copySubRels(src, dst, absSrc, absDst, visited);
    }
    
    dst.cachedZipEntries[dstRels] = relsStr.toUtf8();
}

QVector<QString> ExcelHandler::parseSharedStringTable(const QByteArray& sstXml)
{
    QVector<QString> strings;
    QString xml = QString::fromUtf8(sstXml);
    
    // Simple regex to extract <t> content from <si> elements
    QRegularExpression siRe("<si>(.*?)</si>", QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatchIterator it = siRe.globalMatch(xml);
    
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString siContent = match.captured(1);
        
        // Extract <t> text
        QRegularExpression tRe("<t[^>]*>(.*?)</t>", QRegularExpression::DotMatchesEverythingOption);
        QRegularExpressionMatch tMatch = tRe.match(siContent);
        
        if (tMatch.hasMatch()) {
            strings.append(tMatch.captured(1));
        } else {
            strings.append(""); // empty string entry
        }
    }
    
    return strings;
}

QByteArray ExcelHandler::buildSharedStringTable(const QVector<QString>& strings)
{
    QString xml = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>";
    xml += QString("<sst xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
                  "count=\"%1\" uniqueCount=\"%1\">").arg(strings.size());
    
    for (const QString& str : strings) {
        xml += "<si><t>" + str.toHtmlEscaped() + "</t></si>";
    }
    
    xml += "</sst>";
    return xml.toUtf8();
}

void ExcelHandler::mergeSharedStrings(WorkbookData& src, WorkbookData& dst, QByteArray& sheetXml)
{
    // Parse source shared strings
    if (!src.cachedZipEntries.contains("xl/sharedStrings.xml")) {
        return; // source has no shared strings
    }
    
    // Ensure destination has shared strings table
    if (!dst.cachedZipEntries.contains("xl/sharedStrings.xml")) {
        dst.cachedZipEntries["xl/sharedStrings.xml"] =
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<sst xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
            "count=\"0\" uniqueCount=\"0\"></sst>";
    }
    
    // Build index map: source SST index â†’ destination SST index
    QVector<QString> srcStrings = parseSharedStringTable(src.cachedZipEntries["xl/sharedStrings.xml"]);
    QVector<QString> dstStrings = parseSharedStringTable(dst.cachedZipEntries["xl/sharedStrings.xml"]);
    
    // Build lookup for existing destination strings
    QHash<QString, int> dstLookup;
    for (int i = 0; i < dstStrings.size(); i++) {
        dstLookup[dstStrings[i]] = i;
    }
    
    QMap<int, int> indexRemap; // src index â†’ dst index
    
    for (int srcIdx = 0; srcIdx < srcStrings.size(); srcIdx++) {
        const QString& str = srcStrings[srcIdx];
        if (dstLookup.contains(str)) {
            indexRemap[srcIdx] = dstLookup[str];
        } else {
            int newIdx = dstStrings.size();
            dstStrings.append(str);
            dstLookup[str] = newIdx;
            indexRemap[srcIdx] = newIdx;
        }
    }
    
    // Rewrite shared string indices in the sheet XML
    // More permissive regex to handle formulas and other elements between <c> and <v>
    QString xmlStr = QString::fromUtf8(sheetXml);
    QRegularExpression cellRe(
        "<c\\b[^>]*\\bt=\"s\"[^>]*>(?:(?!</c>).)*<v>(\\d+)</v>",
        QRegularExpression::DotMatchesEverythingOption
    );
    
    QString result;
    int lastEnd = 0;
    QRegularExpressionMatchIterator it = cellRe.globalMatch(xmlStr);
    
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        int vStart = m.capturedStart(1);
        int vEnd = m.capturedEnd(1);
        int srcIdx = m.captured(1).toInt();
        
        result += xmlStr.mid(lastEnd, vStart - lastEnd);
        
        if (indexRemap.contains(srcIdx)) {
            result += QString::number(indexRemap[srcIdx]);
        } else {
            result += m.captured(1); // keep as-is
        }
        
        lastEnd = vEnd;
    }
    result += xmlStr.mid(lastEnd);
    
    sheetXml = result.toUtf8();
    
    // Update destination shared strings XML
    dst.cachedZipEntries["xl/sharedStrings.xml"] = buildSharedStringTable(dstStrings);
}

// Helper function to extract inner XML of a tag and its count
QByteArray ExcelHandler::extractStyleSection(const QByteArray& stylesXml, const QString& tagName, int& count) {
    count = 0;
    QString startTagBase = QString("<%1").arg(tagName);
    int tagStart = stylesXml.indexOf(startTagBase.toUtf8());
    if (tagStart == -1) return QByteArray();
    
    // Extract count if present
    int countAttr = stylesXml.indexOf("count=\"", tagStart);
    int tagClose = stylesXml.indexOf(">", tagStart);
    if (countAttr != -1 && countAttr < tagClose) {
        int countEnd = stylesXml.indexOf('"', countAttr + 7);
        if (countEnd != -1) {
            count = stylesXml.mid(countAttr + 7, countEnd - countAttr - 7).toInt();
        }
    }
    
    QString endTag = QString("</%1>").arg(tagName);
    int tagEnd = stylesXml.indexOf(endTag.toUtf8(), tagClose);
    if (tagEnd == -1) return QByteArray();
    
    // Return just the inner XML
    return stylesXml.mid(tagClose + 1, tagEnd - tagClose - 1);
}

// Replaces the <tagName ... count="X"> content with newInnerXml and updates count attribute
void ExcelHandler::replaceStyleSection(QByteArray& stylesXml, const QString& tagName, const QByteArray& newInnerXml, int newCount) {
    QString startTagBase = QString("<%1").arg(tagName);
    int tagStart = stylesXml.indexOf(startTagBase.toUtf8());
    if (tagStart == -1) return;
    
    int tagClose = stylesXml.indexOf(">", tagStart);
    int countAttr = stylesXml.indexOf("count=\"", tagStart);
    if (countAttr != -1 && countAttr < tagClose) {
        int countEnd = stylesXml.indexOf('"', countAttr + 7);
        if (countEnd != -1) {
            stylesXml.replace(countAttr + 7, countEnd - countAttr - 7, QString::number(newCount).toUtf8());
        }
    }
    
    tagClose = stylesXml.indexOf(">", tagStart);
    QString endTag = QString("</%1>").arg(tagName);
    int tagEnd = stylesXml.indexOf(endTag.toUtf8(), tagClose);
    if (tagEnd != -1) {
        stylesXml.replace(tagClose + 1, tagEnd - tagClose - 1, newInnerXml);
    }
}

ExcelHandler::MergeStylesResult ExcelHandler::mergeStyles(WorkbookData& src, WorkbookData& dst)
{
    MergeStylesResult result;
    if (!src.cachedZipEntries.contains("xl/styles.xml") || !dst.cachedZipEntries.contains("xl/styles.xml")) return result;

    if (src.filePath == dst.filePath) {
        result.valid = true;
        result.xfOffsetLightBlue = 0;
        return result;
    }

    QByteArray srcStyles = src.cachedZipEntries["xl/styles.xml"];
    QByteArray dstStyles = dst.cachedZipEntries["xl/styles.xml"];

    int srcFontCount = 0, srcBorderCount = 0, srcXfCount = 0;
    QByteArray srcFonts   = extractStyleSection(srcStyles, "fonts",   srcFontCount);
    QByteArray srcBorders = extractStyleSection(srcStyles, "borders", srcBorderCount);
    QByteArray srcCellXfs = extractStyleSection(srcStyles, "cellXfs", srcXfCount);
    if (srcXfCount == 0) return result;

    int dstFontCount = 0, dstBorderCount = 0, dstXfCount = 0;
    QByteArray dstFonts   = extractStyleSection(dstStyles, "fonts",   dstFontCount);
    QByteArray dstBorders = extractStyleSection(dstStyles, "borders", dstBorderCount);
    QByteArray dstCellXfs = extractStyleSection(dstStyles, "cellXfs", dstXfCount);
    const int fontOffset   = dstFontCount;
    const int borderOffset = dstBorderCount;

    QMap<int, int> numFmtRemap;
    {
        int srcNfCount = 0, dstNfCount = 0;
        QByteArray srcNf = extractStyleSection(srcStyles, "numFmts", srcNfCount);
        QByteArray dstNf = extractStyleSection(dstStyles, "numFmts", dstNfCount);
        if (!srcNf.isEmpty()) {
            int maxId = 163;
            QRegularExpression idRe("numFmtId=\"(\\d+)\"");
            auto idIt = idRe.globalMatch(QString::fromUtf8(dstNf));
            while (idIt.hasNext()) { int id = idIt.next().captured(1).toInt(); if (id > maxId) maxId = id; }
            QByteArray newEntries;
            int nextId = maxId + 1;
            QRegularExpression nfRe("<numFmt[^/]*/?>", QRegularExpression::DotMatchesEverythingOption);
            auto nfIt = nfRe.globalMatch(QString::fromUtf8(srcNf));
            while (nfIt.hasNext()) {
                QString entry = nfIt.next().captured(0);
                QRegularExpressionMatch idMatch = idRe.match(entry);
                if (!idMatch.hasMatch()) continue;
                int oldId = idMatch.captured(1).toInt();
                // Check if this is a currency format — strip to General (numFmtId=0)
                QRegularExpression fcRe("formatCode=\"([^\"]*)\"");
                QRegularExpressionMatch fcMatch = fcRe.match(entry);
                bool isCurrency = false;
                if (fcMatch.hasMatch()) {
                    QString fmt = fcMatch.captured(1);
                    isCurrency = fmt.contains(QChar(0x20AC)) || fmt.contains(QChar(0x00A3)) || fmt.contains("$")
                              || fmt.contains(QChar(0x00A5)) || fmt.contains(QChar(0x20B9))
                              || fmt.contains("&quot;");  // XML-encoded currency wrappers
                }
                if (isCurrency) {
                    numFmtRemap[oldId] = 0; // Map to General — no currency symbol
                } else if (oldId < 164) { numFmtRemap[oldId] = oldId; }
                else { numFmtRemap[oldId] = nextId; entry.replace(QString("numFmtId=\"%1\"").arg(oldId), QString("numFmtId=\"%1\"").arg(nextId)); newEntries.append(entry.toUtf8()); nextId++; }
            }
            if (!newEntries.isEmpty()) { dstNf.append(newEntries); replaceStyleSection(dstStyles, "numFmts", dstNf, dstNfCount + (nextId - maxId - 1)); }
        }
    }

    dstFonts.append(srcFonts);
    dstBorders.append(srcBorders);
    replaceStyleSection(dstStyles, "fonts",   dstFonts,   dstFontCount + srcFontCount);
    replaceStyleSection(dstStyles, "borders", dstBorders, dstBorderCount + srcBorderCount);

    auto shiftAttr = [](QString& xf, const QString& attr, int offset) {
        QRegularExpression re(attr + "=\"(\\d+)\"");
        int adj = 0; auto it = re.globalMatch(xf);
        while (it.hasNext()) { auto m = it.next(); int pos = m.capturedStart(1)+adj; int len = m.capturedLength(1); QString nv = QString::number(m.captured(1).toInt()+offset); xf.replace(pos,len,nv); adj+=nv.length()-len; }
    };

    QString xfsStr = QString::fromUtf8(srcCellXfs);
    QRegularExpression xfRe("<xf\\b[^>]*/?>(?:.*?</xf>)?", QRegularExpression::DotMatchesEverythingOption);
    QByteArray mergedXfs;
    auto xfIt = xfRe.globalMatch(xfsStr);
    while (xfIt.hasNext()) {
        QString xf = xfIt.next().captured(0);
        shiftAttr(xf, "fontId",   fontOffset);
        shiftAttr(xf, "borderId", borderOffset);
        // Force ALL source cellXf entries to numFmtId=0 (General) — eliminates currency symbols
        QRegularExpressionMatch m = QRegularExpression("numFmtId=\"(\\d+)\"").match(xf);
        if (m.hasMatch()) { xf.replace(m.capturedStart(1), m.capturedLength(1), "0"); }
        mergedXfs.append(xf.toUtf8());
    }

    dstCellXfs.append(mergedXfs);
    replaceStyleSection(dstStyles, "cellXfs", dstCellXfs, dstXfCount + srcXfCount);
    dst.cachedZipEntries["xl/styles.xml"] = dstStyles;

    result.valid = true;
    result.xfOffsetLightBlue = dstXfCount;
    result.srcXfCount = srcXfCount;
    return result;
}

// Appends a solid fill with the given ARGB color to styles.xml
// Returns the new fillId (0-based index)
int ExcelHandler::appendFill(QByteArray& stylesXml, const QString& fgColorRgb)
{
    int fillCount = 0;
    QByteArray existingFills = extractStyleSection(stylesXml, "fills", fillCount);
    
    QByteArray newFill = QString(
        "<fill>"
          "<patternFill patternType=\"solid\">"
            "<fgColor rgb=\"%1\"/>"
            "<bgColor indexed=\"64\"/>"
          "</patternFill>"
        "</fill>"
    ).arg(fgColorRgb).toUtf8();
    
    existingFills.append(newFill);
    replaceStyleSection(stylesXml, "fills", existingFills, fillCount + 1);
    
    return fillCount; // 0-based index of the new fill
}

// Appends a cellXf entry with given fontId, fillId, borderId
// Returns the new style index (0-based)
int ExcelHandler::appendCellXf(QByteArray& stylesXml, int fontId, int fillId, int borderId)
{
    int xfCount = 0;
    QByteArray existingXfs = extractStyleSection(stylesXml, "cellXfs", xfCount);
    
    QByteArray newXf = QString(
        "<xf numFmtId=\"0\" fontId=\"%1\" fillId=\"%2\" borderId=\"%3\" "
        "xfId=\"0\" applyFill=\"1\" applyFont=\"1\" applyBorder=\"1\"/>"
    ).arg(fontId).arg(fillId).arg(borderId).toUtf8();
    
    existingXfs.append(newXf);
    replaceStyleSection(stylesXml, "cellXfs", existingXfs, xfCount + 1);
    
    return xfCount; // 0-based index of the new XF
}

// Build the mapping row set from JSON files
QSet<int> ExcelHandler::buildMappingRowSet(const QString& mappingsPath, const QString& mappingsOldPath, const QString& targetSheetName)
{
    QSet<int> mappingRows;
    
    auto loadFromFile = [&](const QString& filePath) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning() << "[buildMappingRowSet] Cannot open:" << filePath;
            return;
        }
        
        QByteArray data = file.readAll();
        file.close();
        
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(data, &err);
        if (err.error != QJsonParseError::NoError) {
            qWarning() << "[buildMappingRowSet] JSON parse error in" << filePath << ":" << err.errorString();
            return;
        }
        
        // Assuming structure is an array of mapping objects
        QJsonArray arr = doc.array();
        for (const QJsonValue& val : arr) {
            QJsonObject obj = val.toObject();
            
            // Only include rows targeting our specific sheet
            QString destSheet = obj.value("destination_sheet").toString();
            if (!destSheet.isEmpty() && destSheet != targetSheetName) {
                continue;
            }
            
            int destRow = obj.value("destination_row").toInt(0);
            if (destRow > 0) {
                mappingRows.insert(destRow);
            }
            
            // Also try "row" field as fallback
            if (destRow == 0) {
                destRow = obj.value("row").toInt(0);
                if (destRow > 0) {
                    mappingRows.insert(destRow);
                }
            }
        }
        
        qInfo() << "[buildMappingRowSet] Loaded" << arr.size() << "entries from" << filePath
                << ", matched" << mappingRows.size() << "rows for sheet" << targetSheetName;
    };
    
    loadFromFile(mappingsPath);
    loadFromFile(mappingsOldPath);
    
    return mappingRows;
}

// Apply row colors to a copied sheet
void ExcelHandler::applyRowColors(WorkbookData& dst, const QString& sheetPath,
                                   const QSet<int>& mappingRows,
                                   int /*xfOffsetLightBlue*/, int /*xfOffsetDarkBlue*/)
{
    if (!dst.cachedZipEntries.contains(sheetPath)) return;
    if (!dst.cachedZipEntries.contains("xl/styles.xml")) return;

    QByteArray stylesXml = dst.cachedZipEntries["xl/styles.xml"];

    // 1. Add light blue + dark blue fills (with smart reuse)
    int fillCount = 0;
    QByteArray fills = extractStyleSection(stylesXml, "fills", fillCount);
    int lightBlueFillId = -1;
    int darkBlueFillId = -1;

    // Use regex to find existing fill IDs for our colors
    QRegularExpression fillRe("<fill\\b[^>]*>.*?rgb=\"([^\"]+)\".*?</fill>", QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    auto fillIt = fillRe.globalMatch(QString::fromUtf8(fills));
    int currentIdx = 0;
    while (fillIt.hasNext()) {
        auto m = fillIt.next();
        QString rgb = m.captured(1).toUpper();
        if (rgb == "FFBDD7EE") lightBlueFillId = currentIdx;
        else if (rgb == "FF1F4E79") darkBlueFillId = currentIdx;
        currentIdx++;
    }

    if (lightBlueFillId == -1) {
        lightBlueFillId = fillCount++;
        fills.append("<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFBDD7EE\"/><bgColor indexed=\"64\"/></patternFill></fill>");
    }
    if (darkBlueFillId == -1) {
        darkBlueFillId = fillCount++;
        fills.append("<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FF1F4E79\"/><bgColor indexed=\"64\"/></patternFill></fill>");
    }
    replaceStyleSection(stylesXml, "fills", fills, fillCount);

    // 2. Parse original fonts for cloning
    int fontCount = 0;
    QByteArray fontsXml = extractStyleSection(stylesXml, "fonts", fontCount);
    QStringList originalFonts;
    QRegularExpression fontItemRe("<font\\b[^>]*/?>(?:.*?</font>)?", QRegularExpression::DotMatchesEverythingOption);
    auto fontMatchIt = fontItemRe.globalMatch(QString::fromUtf8(fontsXml));
    while (fontMatchIt.hasNext()) originalFonts.append(fontMatchIt.next().captured(0));

    // Cache for white versions of original fonts
    QMap<int, int> fontCache; // originalFontId -> whiteFontId

    // 3. Parse existing cellXfs into list
    int xfCount = 0;
    QByteArray cellXfsRaw = extractStyleSection(stylesXml, "cellXfs", xfCount);
    QStringList originalXfs;
    QRegularExpression xfRe("<xf\\b[^>]*/?>(?:.*?</xf>)?", QRegularExpression::DotMatchesEverythingOption);
    auto xfIt = xfRe.globalMatch(QString::fromUtf8(cellXfsRaw));
    while (xfIt.hasNext()) originalXfs.append(xfIt.next().captured(0));

    // Cache: <oldXfIndex, isDarkBlue> -> newXfIndex
    QMap<QPair<int, bool>, int> styleCache;
    int nextXfIndex = originalXfs.size();

    // 3. Define adaptive style mapping logic (clones fonts/fills as they are used)
    auto getColoredXf = [&](int oldXf, bool isDark) -> int {
        if (oldXf < 0 || oldXf >= originalXfs.size()) return oldXf;
        QPair<int, bool> key(oldXf, isDark);
        if (styleCache.contains(key)) return styleCache[key];
        
        QString newXf = originalXfs.at(oldXf);
        int fillId = isDark ? darkBlueFillId : lightBlueFillId;

        auto safeSet = [](QString& xf, const QString& attr, int val) {
            QRegularExpression re("\\b" + attr + "=\"(\\d+)\"");
            QRegularExpressionMatch m = re.match(xf);
            if (m.hasMatch()) {
                xf.replace(m.capturedStart(1), m.capturedLength(1), QString::number(val));
            } else {
                int firstGt = xf.indexOf('>');
                if (firstGt != -1) {
                    int insertPos = firstGt;
                    if (insertPos > 0 && xf[insertPos - 1] == '/') insertPos--;
                    xf.insert(insertPos, QString(" %1=\"%2\"").arg(attr).arg(val));
                }
            }
        };

        auto getWhiteFont = [&](int originalFontId) -> int {
            if (originalFontId < 0 || originalFontId >= originalFonts.size()) return originalFontId;
            if (fontCache.contains(originalFontId)) return fontCache[originalFontId];
            
            QString newFont = originalFonts.at(originalFontId);
            QRegularExpression colorRe("<color\\b[^>]*/>");
            newFont.replace(colorRe, "<color rgb=\"FFFFFFFF\"/>");
            
            int newFontId = originalFonts.size();
            originalFonts.append(newFont);
            fontCache[originalFontId] = newFontId;
            return newFontId;
        };

        safeSet(newXf, "fillId", fillId);
        safeSet(newXf, "applyFill", 1);

        // Remove currency symbols by swapping to a standard numeric format ID (4 = #,##0.00)
        // This is safe because it only affects the newly cloned style for highlighted rows.
        QRegularExpression nfIdRe("\\bnumFmtId=\"(\\d+)\"");
        QRegularExpressionMatch nfm = nfIdRe.match(newXf);
        if (nfm.hasMatch()) {
            int oldNfId = nfm.captured(1).toInt();
            // Force all non-General formats to General (numFmtId=0).
            // This is equivalent to "Select All → Format Cells → General" in Excel.
            if (oldNfId != 0) {
                safeSet(newXf, "numFmtId", 0); 
                safeSet(newXf, "applyNumberFormat", 1);
            }
        }

        if (isDark) {
            QRegularExpression fIdRe("\\bfontId=\"(\\d+)\"");
            QRegularExpressionMatch fm = fIdRe.match(newXf);
            int oldFontId = fm.hasMatch() ? fm.captured(1).toInt() : 0;
            int whiteFontId = getWhiteFont(oldFontId);
            
            safeSet(newXf, "fontId", whiteFontId);
            safeSet(newXf, "applyFont", 1);
        }
        
        originalXfs.append(newXf);
        styleCache[key] = nextXfIndex;
        return nextXfIndex++;
    };

    // 4. Walk sheet XML and remap cell s= attributes
    QString sheetStr = QString::fromUtf8(dst.cachedZipEntries[sheetPath]);
    QRegularExpression rowStartRe("<row\\b[^>]*\\br=\"(\\d+)\"");
    QRegularExpression sAttrRe("\\bs=\"(\\d+)\"");
    int currentRow = 0;
    QString result;
    result.reserve(sheetStr.size() + sheetStr.size() / 8);
    int pos = 0;

    while (pos < sheetStr.size()) {
        int nextRow  = sheetStr.indexOf("<row ", pos);
        int nextCell = sheetStr.indexOf("<c ",   pos);
        int nextTag = -1; bool isRow = false;
        if (nextRow != -1 && (nextCell == -1 || nextRow < nextCell)) { nextTag = nextRow;  isRow = true;  }
        else if (nextCell != -1)                                       { nextTag = nextCell; isRow = false; }
        if (nextTag == -1) { result.append(sheetStr.mid(pos)); break; }
        result.append(sheetStr.mid(pos, nextTag - pos));
        int tagEnd = sheetStr.indexOf('>', nextTag);
        if (tagEnd == -1) { result.append(sheetStr.mid(nextTag)); break; }
        QString tag = sheetStr.mid(nextTag, tagEnd - nextTag + 1);
        if (isRow) {
            QRegularExpressionMatch rm = rowStartRe.match(tag);
            if (rm.hasMatch()) currentRow = rm.captured(1).toInt();
        } else if (currentRow >= 8) {
            // Only colorize body rows (>=8), protecting header rows 1-7
            bool isDark = mappingRows.contains(currentRow);
            QRegularExpressionMatch sm = sAttrRe.match(tag);
            if (sm.hasMatch()) {
                int oldS = sm.captured(1).toInt();
                int newS = getColoredXf(oldS, isDark);
                tag.replace(sm.capturedStart(1), sm.capturedLength(1), QString::number(newS));
            } else {
                int newS = getColoredXf(0, isDark);
                tag.insert(3, QString(" s=\"%1\"").arg(newS));
            }
        }
        result.append(tag);
        pos = tagEnd + 1;
    }

    dst.cachedZipEntries[sheetPath] = result.toUtf8();

    // 5. Write updated styles with new cloned Fonts and XFs
    bool stylesChanged = false;
    if (originalFonts.size() > fontCount) {
        replaceStyleSection(stylesXml, "fonts", originalFonts.join("").toUtf8(), originalFonts.size());
        stylesChanged = true;
    }
    if (nextXfIndex > xfCount) {
        replaceStyleSection(stylesXml, "cellXfs", originalXfs.join("").toUtf8(), nextXfIndex);
        stylesChanged = true;
    }
    
    if (stylesChanged) {
        dst.cachedZipEntries["xl/styles.xml"] = stylesXml;
    }

    qInfo() << "[applyRowColors] Done. New Fonts:" << (originalFonts.size() - fontCount)
            << "New XFs:" << (nextXfIndex - xfCount);
}

QVector<QString> ExcelHandler::parseCellXfs(const QByteArray& stylesXml)
{
    // TODO: Implement cellXfs parsing
    return QVector<QString>();
}

int ExcelHandler::appendStyleSection(WorkbookData& src, WorkbookData& dst,
                                     const QString& section, const QString& element)
{
    // TODO: Implement style section appending
    return 0;
}

QString ExcelHandler::remapXfIndices(const QString& xfEntry, int fontsOffset,
                                     int fillsOffset, int bordersOffset, int numFmtsOffset)
{
    // TODO: Implement XF index remapping
    return xfEntry;
}

void ExcelHandler::rebuildStylesXml(WorkbookData& dst, const QVector<QString>& cellXfs)
{
    // TODO: Implement styles.xml rebuilding
}

void ExcelHandler::addContentType(WorkbookData& dst, const QString& partName, const QString& contentType)
{
    QString ctXml = QString::fromUtf8(dst.cachedZipEntries["[Content_Types].xml"]);
    
    // Check if already registered
    if (ctXml.contains(QString("PartName=\"/%1\"").arg(partName))) {
        return;
    }
    
    // Insert before closing </Types>
    QString entry = QString("<Override PartName=\"/%1\" ContentType=\"%2\"/>")
                        .arg(partName, contentType);
    
    ctXml.replace("</Types>", entry + "</Types>");
    dst.cachedZipEntries["[Content_Types].xml"] = ctXml.toUtf8();
}

QString ExcelHandler::addWorkbookSheet(WorkbookData& dst, const QString& sheetName, int sheetId)
{
    QString wbXml = QString::fromUtf8(dst.cachedZipEntries["xl/workbook.xml"]);
    
    // Find max existing rId to avoid collisions
    QString wbRels = QString::fromUtf8(dst.cachedZipEntries["xl/_rels/workbook.xml.rels"]);
    QRegularExpression ridRe("Id=\"rId(\\d+)\"");
    QRegularExpressionMatchIterator matches = ridRe.globalMatch(wbRels);
    int maxRid = 0;
    while (matches.hasNext()) {
        maxRid = qMax(maxRid, matches.next().captured(1).toInt());
    }
    
    // Generate unique rId
    QString rId = QString("rId%1").arg(maxRid + 1);
    
    // Insert before </sheets>
    QString entry = QString("<sheet name=\"%1\" sheetId=\"%2\" r:id=\"%3\"/>")
                        .arg(sheetName.toHtmlEscaped())
                        .arg(sheetId)
                        .arg(rId);
    
    wbXml.replace("</sheets>", entry + "</sheets>");
    dst.cachedZipEntries["xl/workbook.xml"] = wbXml.toUtf8();
    
    return rId;
}

void ExcelHandler::addWorkbookRel(WorkbookData& dst, const QString& rId, const QString& target)
{
    QString relsXml = QString::fromUtf8(dst.cachedZipEntries["xl/_rels/workbook.xml.rels"]);
    
    QString entry = QString(
        "<Relationship Id=\"%1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"%2\"/>")
        .arg(rId, target);
    
    relsXml.replace("</Relationships>", entry + "</Relationships>");
    dst.cachedZipEntries["xl/_rels/workbook.xml.rels"] = relsXml.toUtf8();
}

void ExcelHandler::registerContentType(WorkbookData& dst,
                                       const QString& partPath,
                                       const QString& relType)
{
    // Map relationship type URIs to content types
    static const QMap<QString, QString> typeMap = {
        {"http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
         "application/vnd.openxmlformats-officedocument.drawing+xml"},
        {"http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart",
         "application/vnd.openxmlformats-officedocument.drawingml.chart+xml"},
        {"http://schemas.openxmlformats.org/officeDocument/2006/relationships/chartsheet",
         "application/vnd.openxmlformats-officedocument.spreadsheetml.chartsheet+xml"},
        {"http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments",
         "application/vnd.openxmlformats-officedocument.spreadsheetml.comments+xml"},
        {"http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing",
         "application/vnd.openxmlformats-officedocument.vmlDrawing"},
        {"http://schemas.openxmlformats.org/officeDocument/2006/relationships/table",
         "application/vnd.openxmlformats-officedocument.spreadsheetml.table+xml"},
        {"http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotTable",
         "application/vnd.openxmlformats-officedocument.spreadsheetml.pivotTable+xml"},
        {"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image",
         ""},  // images use extension-based content types, handled by default entries
    };
    
    for (auto it = typeMap.constBegin(); it != typeMap.constEnd(); ++it) {
        if (relType.contains(it.key()) && !it.value().isEmpty()) {
            addContentType(dst, partPath, it.value());
            return;
        }
    }
    
    // Fallback: register based on file extension
    if (partPath.endsWith(".xml")) {
        addContentType(dst, partPath, "application/xml");
    }
}

