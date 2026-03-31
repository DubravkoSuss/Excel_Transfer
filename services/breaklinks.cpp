#include "breaklinks.h"
#include <QDebug>
#include <QFile>
#include <QTemporaryDir>
#include <QProcess>
#include <QDirIterator>

const QString BreakLinks::NS_SML = "http://schemas.openxmlformats.org/spreadsheetml/2006/main";
const QString BreakLinks::NS_REL = "http://schemas.openxmlformats.org/package/2006/relationships";
const QString BreakLinks::NS_CT = "http://schemas.openxmlformats.org/package/2006/content-types";
const QString BreakLinks::EXT_LINK_TYPE = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/externalLink";
const QString BreakLinks::EXT_LINK_CT = "http://schemas.openxmlformats.org/officeDocument/2006/relationshipsCalcChain";

BreakLinks::BreakLinks(QObject *parent)
    : QObject(parent)
{
}

BreakLinks::~BreakLinks()
{
}

bool BreakLinks::breakExternalLinks(const QString& filePath, const QString& valuesFilePath)
{
    m_cachedValues.clear();
    m_cachedValuesRoot.clear();
    Result result;
    result.success = false;
    result.error.clear();
    result.filesModified = 0;

    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        result.error = "File not found";
        emit complete(result);
        return false;
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        result.error = "Failed to create temp dir";
        emit complete(result);
        return false;
    }

    QString tempPath = tempDir.path();
    QString expandCmd = QString("Expand-Archive -Path '%1' -DestinationPath '%2' -Force")
                            .arg(fileInfo.absoluteFilePath())
                            .arg(tempPath);
    if (QProcess::execute("powershell", {"-Command", expandCmd}) != 0) {
        result.error = "Failed to expand archive";
        emit complete(result);
        return false;
    }

    const QString workbookXmlPath = tempPath + "/xl/workbook.xml";
    const QString workbookRelsPath = tempPath + "/xl/_rels/workbook.xml.rels";
    const QString contentTypesPath = tempPath + "/[Content_Types].xml";

    auto updateXml = [this, &result](const QString& path, auto processor) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }
        QByteArray data = file.readAll();
        file.close();

        bool changed = processor(data);
        if (changed) {
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                return false;
            }
            file.write(data);
            file.close();
            result.filesModified++;
        }
        return true;
    };

    if (!updateXml(workbookXmlPath, [this](QByteArray& data) { return processWorkbookXml(data); })) {
        result.error = "Failed to update workbook.xml";
        emit complete(result);
        return false;
    }
    if (!updateXml(workbookRelsPath, [this](QByteArray& data) { return processWorkbookRels(data); })) {
        result.error = "Failed to update workbook rels";
        emit complete(result);
        return false;
    }
    if (!updateXml(contentTypesPath, [this](QByteArray& data) { return processContentTypes(data); })) {
        result.error = "Failed to update content types";
        emit complete(result);
        return false;
    }

    QDir worksheetsDir(tempPath + "/xl/worksheets");
    if (worksheetsDir.exists()) {
        if (!valuesFilePath.isEmpty()) {
            QTemporaryDir valuesTempDir;
            if (valuesTempDir.isValid()) {
                QString valuesPath = valuesTempDir.path();
                QString expandCmdValues = QString("Expand-Archive -Path '%1' -DestinationPath '%2' -Force")
                                              .arg(valuesFilePath)
                                              .arg(valuesPath);
                if (QProcess::execute("powershell", {"-Command", expandCmdValues}) == 0) {
                    m_cachedValuesRoot = valuesPath;
                }
            }
        }

        QDirIterator it(worksheetsDir.absolutePath(), QStringList() << "sheet*.xml", QDir::Files);
        while (it.hasNext()) {
            QString sheetPath = it.next();
            QFile sheetFile(sheetPath);
            if (!sheetFile.open(QIODevice::ReadOnly)) {
                continue;
            }
            QByteArray data = sheetFile.readAll();
            sheetFile.close();

            int sheetIndex = getSheetIndexFromPath(sheetPath).toInt();
            bool changed = processWorksheetXml(data, sheetIndex, m_cachedValuesRoot);
            if (changed) {
                if (sheetFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    sheetFile.write(data);
                    sheetFile.close();
                    result.filesModified++;
                }
            }
        }
    }

    QDir externalLinksDir(tempPath + "/xl/externalLinks");
    if (externalLinksDir.exists()) {
        externalLinksDir.removeRecursively();
        result.filesModified++;
    }

    QString tempZip = tempPath + "/out.xlsx";
    QString compressCmd = QString("Compress-Archive -Path '%1/*' -DestinationPath '%2' -Force")
                              .arg(tempPath)
                              .arg(tempZip);
    if (QProcess::execute("powershell", {"-Command", compressCmd}) != 0) {
        result.error = "Failed to recompress archive";
        emit complete(result);
        return false;
    }

    QFile::remove(fileInfo.absoluteFilePath());
    if (!QFile::rename(tempZip, fileInfo.absoluteFilePath())) {
        result.error = "Failed to replace workbook";
        emit complete(result);
        return false;
    }

    result.success = true;
    emit complete(result);
    return true;
}

QVector<BreakLinks::ExtLinkInfo> BreakLinks::findExternalLinks(const QString& zipPath)
{
    Q_UNUSED(zipPath);
    return QVector<ExtLinkInfo>();
}

bool BreakLinks::processWorkbookXml(QByteArray& data)
{
    QXmlStreamReader reader(data);
    QByteArray output;
    QXmlStreamWriter writer(&output);
    bool changed = false;

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == "externalReferences") {
            changed = true;
            reader.skipCurrentElement();
            continue;
        }
        if (reader.isStartDocument()) {
            writer.writeStartDocument();
            continue;
        }
        if (reader.isEndDocument()) {
            writer.writeEndDocument();
            break;
        }
        if (reader.isStartElement()) {
            writer.writeStartElement(reader.qualifiedName().toString());
            for (const auto& attr : reader.attributes()) {
                writer.writeAttribute(attr);
            }
        } else if (reader.isEndElement()) {
            writer.writeEndElement();
        } else if (reader.isCharacters()) {
            writer.writeCharacters(reader.text().toString());
        }
    }

    if (changed && !reader.hasError()) {
        data = output;
    }
    return changed;
}

bool BreakLinks::processWorkbookRels(QByteArray& data)
{
    QXmlStreamReader reader(data);
    QByteArray output;
    QXmlStreamWriter writer(&output);
    bool changed = false;

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == "Relationship") {
            auto typeAttr = reader.attributes().value("Type");
            if (typeAttr == EXT_LINK_TYPE) {
                changed = true;
                reader.skipCurrentElement();
                continue;
            }
        }
        if (reader.isStartDocument()) {
            writer.writeStartDocument();
            continue;
        }
        if (reader.isEndDocument()) {
            writer.writeEndDocument();
            break;
        }
        if (reader.isStartElement()) {
            writer.writeStartElement(reader.qualifiedName().toString());
            for (const auto& attr : reader.attributes()) {
                writer.writeAttribute(attr);
            }
        } else if (reader.isEndElement()) {
            writer.writeEndElement();
        } else if (reader.isCharacters()) {
            writer.writeCharacters(reader.text().toString());
        }
    }

    if (changed && !reader.hasError()) {
        data = output;
    }
    return changed;
}

bool BreakLinks::processContentTypes(QByteArray& data)
{
    QXmlStreamReader reader(data);
    QByteArray output;
    QXmlStreamWriter writer(&output);
    bool changed = false;

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == "Override") {
            auto partAttr = reader.attributes().value("PartName");
            auto typeAttr = reader.attributes().value("ContentType");
            if (typeAttr == EXT_LINK_CT || partAttr.toString().startsWith("/xl/externalLinks")) {
                changed = true;
                reader.skipCurrentElement();
                continue;
            }
        }
        if (reader.isStartDocument()) {
            writer.writeStartDocument();
            continue;
        }
        if (reader.isEndDocument()) {
            writer.writeEndDocument();
            break;
        }
        if (reader.isStartElement()) {
            writer.writeStartElement(reader.qualifiedName().toString());
            for (const auto& attr : reader.attributes()) {
                writer.writeAttribute(attr);
            }
        } else if (reader.isEndElement()) {
            writer.writeEndElement();
        } else if (reader.isCharacters()) {
            writer.writeCharacters(reader.text().toString());
        }
    }

    if (changed && !reader.hasError()) {
        data = output;
    }
    return changed;
}

bool BreakLinks::processWorksheetXml(QByteArray& data, int sheetIndex, const QString& valuesZipPath)
{
    QXmlStreamReader reader(data);
    QByteArray output;
    QXmlStreamWriter writer(&output);
    bool changed = false;

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartDocument()) {
            writer.writeStartDocument();
            continue;
        }
        if (reader.isEndDocument()) {
            writer.writeEndDocument();
            break;
        }

        if (reader.isStartElement() && reader.name() == "c") {
            QString cellRef = reader.attributes().value("r").toString();
            QString cellType = reader.attributes().value("t").toString();
            QByteArray cellData;
            QXmlStreamWriter cellWriter(&cellData);
            cellWriter.writeStartElement("c");
            for (const auto& attr : reader.attributes()) {
                cellWriter.writeAttribute(attr);
            }

            QByteArray cachedValue = getCachedValue(valuesZipPath, sheetIndex, cellRef);
            bool injected = false;

            while (!(reader.isEndElement() && reader.name() == "c") && !reader.atEnd()) {
                reader.readNext();
                if (reader.isStartElement() && reader.name() == "f") {
                    reader.skipCurrentElement();
                    changed = true;
                    continue;
                }
                if (reader.isStartElement() && reader.name() == "v") {
                    reader.readNext();
                    QString valueText = reader.text().toString();
                    if (!cachedValue.isEmpty()) {
                        valueText = QString::fromUtf8(cachedValue);
                        injected = true;
                    }
                    cellWriter.writeTextElement("v", valueText);
                    continue;
                }
                if (reader.isStartElement()) {
                    cellWriter.writeStartElement(reader.qualifiedName().toString());
                    for (const auto& attr : reader.attributes()) {
                        cellWriter.writeAttribute(attr);
                    }
                } else if (reader.isEndElement()) {
                    cellWriter.writeEndElement();
                } else if (reader.isCharacters()) {
                    cellWriter.writeCharacters(reader.text().toString());
                }
            }

            if (cachedValue.isEmpty() && injected) {
                cachedValue.clear();
            }

            if (!cachedValue.isEmpty() && !injected) {
                cellWriter.writeTextElement("v", QString::fromUtf8(cachedValue));
                injected = true;
            }

            cellWriter.writeEndElement();
            writer.writeCharacters("\n");
            writer.device()->write(cellData);
            continue;
        }

        if (reader.isStartElement()) {
            writer.writeStartElement(reader.qualifiedName().toString());
            for (const auto& attr : reader.attributes()) {
                writer.writeAttribute(attr);
            }
        } else if (reader.isEndElement()) {
            writer.writeEndElement();
        } else if (reader.isCharacters()) {
            writer.writeCharacters(reader.text().toString());
        }
    }

    if (changed && !reader.hasError()) {
        data = output;
    }

    return changed;
}

QStringList BreakLinks::getZipContents(const QString& zipPath)
{
    Q_UNUSED(zipPath);
    return QStringList();
}

QByteArray BreakLinks::readZipEntry(const QString& zipPath, const QString& entryName)
{
    Q_UNUSED(zipPath);
    Q_UNUSED(entryName);
    return QByteArray();
}

bool BreakLinks::writeZipEntry(const QString& zipPath, const QString& entryName, const QByteArray& data)
{
    Q_UNUSED(zipPath);
    Q_UNUSED(entryName);
    Q_UNUSED(data);
    return true;
}

QString BreakLinks::getSheetIndexFromPath(const QString& path)
{
    Q_UNUSED(path);
    return QString();
}

QByteArray BreakLinks::getCachedValue(const QString& valuesRootPath, int sheetIndex, const QString& cellRef)
{
    if (valuesRootPath.isEmpty()) {
        return QByteArray();
    }

    if (!m_cachedValues.contains(sheetIndex)) {
        QMap<int, QMap<QString, QByteArray>> loaded = loadCachedSheetValues(valuesRootPath, sheetIndex);
        if (loaded.contains(sheetIndex)) {
            m_cachedValues.insert(sheetIndex, loaded.value(sheetIndex));
        }
    }

    const auto sheetMap = m_cachedValues.value(sheetIndex);
    return sheetMap.value(cellRef);
}

QMap<int, QMap<QString, QByteArray>> BreakLinks::loadCachedSheetValues(const QString& valuesRootPath, int sheetIndex)
{
    QMap<int, QMap<QString, QByteArray>> result;
    if (valuesRootPath.isEmpty()) {
        return result;
    }

    QString sheetPath = QString("%1/xl/worksheets/sheet%2.xml").arg(valuesRootPath).arg(sheetIndex);
    QFile sheetFile(sheetPath);
    if (!sheetFile.open(QIODevice::ReadOnly)) {
        return result;
    }

    QXmlStreamReader reader(sheetFile.readAll());
    sheetFile.close();

    QMap<QString, QByteArray> cellValues;
    QString currentRef;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == "c") {
            currentRef = reader.attributes().value("r").toString();
        }
        if (reader.isStartElement() && reader.name() == "v") {
            reader.readNext();
            cellValues.insert(currentRef, reader.text().toString().toUtf8());
        }
    }

    result.insert(sheetIndex, cellValues);
    return result;
}
