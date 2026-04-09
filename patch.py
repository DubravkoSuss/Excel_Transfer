import sys
import re

content = open('services/excelhandler.cpp', 'r', encoding='utf-8').read()

new_merge_styles = '''ExcelHandler::MergeStylesResult ExcelHandler::mergeStyles(WorkbookData& src, WorkbookData& dst)
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

    // Merge numFmts
    QMap<int, int> numFmtRemap;
    {
        int srcNfCount = 0, dstNfCount = 0;
        QByteArray srcNf = extractStyleSection(srcStyles, "numFmts", srcNfCount);
        QByteArray dstNf = extractStyleSection(dstStyles, "numFmts", dstNfCount);
        if (!srcNf.isEmpty()) {
            int maxId = 163;
            QRegularExpression idRe("numFmtId=\\"(\\d+)\\"");
            auto idIt = idRe.globalMatch(QString::fromUtf8(dstNf));
            while (idIt.hasNext()) {
                int id = idIt.next().captured(1).toInt();
                if (id > maxId) maxId = id;
            }
            QByteArray newEntries;
            int nextId = maxId + 1;
            QRegularExpression nfRe("<numFmt[^/]*/?>", QRegularExpression::DotMatchesEverythingOption);
            auto nfIt = nfRe.globalMatch(QString::fromUtf8(srcNf));
            while (nfIt.hasNext()) {
                QString entry = nfIt.next().captured(0);
                QRegularExpressionMatch idMatch = idRe.match(entry);
                if (!idMatch.hasMatch()) continue;
                int oldId = idMatch.captured(1).toInt();
                if (oldId < 164) {
                    numFmtRemap[oldId] = oldId;
                } else {
                    numFmtRemap[oldId] = nextId;
                    entry.replace(QString("numFmtId=\\"%1\\"").arg(oldId), QString("numFmtId=\\"%1\\"").arg(nextId));
                    newEntries.append(entry.toUtf8());
                    nextId++;
                }
            }
            if (!newEntries.isEmpty()) {
                dstNf.append(newEntries);
                replaceStyleSection(dstStyles, "numFmts", dstNf, dstNfCount + (nextId - maxId - 1));
            }
        }
    }

    dstFonts.append(srcFonts);
    dstBorders.append(srcBorders);
    replaceStyleSection(dstStyles, "fonts",   dstFonts,   dstFontCount + srcFontCount);
    replaceStyleSection(dstStyles, "borders", dstBorders, dstBorderCount + srcBorderCount);

    QString xfsStr = QString::fromUtf8(srcCellXfs);
    QRegularExpression xfRe("<xf\\\\b[^>]*/?>(?:.*?</xf>)?", QRegularExpression::DotMatchesEverythingOption);
    auto xfIt = xfRe.globalMatch(xfsStr);
    
    auto shiftAttr = [](QString& xf, const QString& attr, int offset) {
        QRegularExpression re(attr + "=\\"(\\d+)\\"");
        int adj = 0;
        auto it = re.globalMatch(xf);
        while (it.hasNext()) {
            auto m = it.next();
            int pos = m.capturedStart(1) + adj;
            int len = m.capturedLength(1);
            QString newVal = QString::number(m.captured(1).toInt() + offset);
            xf.replace(pos, len, newVal);
            adj += newVal.length() - len;
        }
    };
    
    QByteArray mergedXfs;
    while (xfIt.hasNext()) {
        QString xf = xfIt.next().captured(0);
        shiftAttr(xf, "fontId", fontOffset);
        shiftAttr(xf, "borderId", borderOffset);
        QRegularExpressionMatch m = QRegularExpression("numFmtId=\\"(\\d+)\\"").match(xf);
        if (m.hasMatch()) {
            int oldId = m.captured(1).toInt();
            if (numFmtRemap.contains(oldId))
                xf.replace(m.capturedStart(1), m.capturedLength(1), QString::number(numFmtRemap[oldId]));
        }
        mergedXfs.append(xf.toUtf8());
    }

    dstCellXfs.append(mergedXfs);
    replaceStyleSection(dstStyles, "cellXfs", dstCellXfs, dstXfCount + srcXfCount);

    dst.cachedZipEntries["xl/styles.xml"] = dstStyles;

    result.valid = true;
    result.xfOffsetLightBlue = dstXfCount;
    return result;
}'''

new_apply_colors = '''void ExcelHandler::applyRowColors(WorkbookData& dst, const QString& sheetPath,
                                   const QSet<int>& mappingRows,
                                   int xfOffsetLightBlue, int xfOffsetDarkBlue)
{
    if (!dst.cachedZipEntries.contains(sheetPath)) return;
    if (!dst.cachedZipEntries.contains("xl/styles.xml")) return;

    QByteArray stylesXml = dst.cachedZipEntries["xl/styles.xml"];

    int fillCount = 0;
    QByteArray fills = extractStyleSection(stylesXml, "fills", fillCount);
    int lightBlueFillId = fillCount;
    int darkBlueFillId  = fillCount + 1;
    fills.append("<fill><patternFill patternType=\\"solid\\"><fgColor rgb=\\"FFBDD7EE\\"/><bgColor indexed=\\"64\\"/></patternFill></fill>");
    fills.append("<fill><patternFill patternType=\\"solid\\"><fgColor rgb=\\"FF1F4E79\\"/><bgColor indexed=\\"64\\"/></patternFill></fill>");
    replaceStyleSection(stylesXml, "fills", fills, fillCount + 2);

    int fontCount = 0;
    QByteArray fonts = extractStyleSection(stylesXml, "fonts", fontCount);
    int whiteFontId = fontCount;
    fonts.append("<font><color rgb=\\"FFFFFFFF\\"/><sz val=\\"11\\"/><name val=\\"Calibri\\"/></font>");
    replaceStyleSection(stylesXml, "fonts", fonts, fontCount + 1);

    int xfCount = 0;
    QByteArray cellXfsRaw = extractStyleSection(stylesXml, "cellXfs", xfCount);
    QStringList originalXfs;
    QRegularExpression xfRe("<xf\\\\b[^>]*/?>(?:.*?</xf>)?", QRegularExpression::DotMatchesEverythingOption);
    auto xfIt = xfRe.globalMatch(QString::fromUtf8(cellXfsRaw));
    while (xfIt.hasNext()) originalXfs.append(xfIt.next().captured(0));

    QMap<QPair<int, bool>, int> styleMap;
    int nextXfIndex = originalXfs.size();

    auto getColoredXf = [&](int oldXf, bool isDark) -> int {
        if (oldXf < 0 || oldXf >= originalXfs.size()) return oldXf;

        QPair<int, bool> cacheKey(oldXf, isDark);
        if (styleMap.contains(cacheKey)) return styleMap[cacheKey];

        QString newXf = originalXfs.at(oldXf);
        int fillId = isDark ? darkBlueFillId : lightBlueFillId;

        QRegularExpressionMatch fm = QRegularExpression("fillId=\\"(\\d+)\\"").match(newXf);
        if (fm.hasMatch()) newXf.replace(fm.capturedStart(1), fm.capturedLength(1), QString::number(fillId));
        else if (newXf.endsWith("/>")) newXf.replace("/>", QString(" fillId=\\"%1\\"/>").arg(fillId));
        else newXf.replace(">", QString(" fillId=\\"%1\\">").arg(fillId));

        if (!newXf.contains("applyFill=")) {
            if (newXf.endsWith("/>")) newXf.replace("/>", " applyFill=\\"1\\"/>");
            else newXf.replace(">", " applyFill=\\"1\\">");
        }

        if (isDark) {
            QRegularExpressionMatch fontm = QRegularExpression("fontId=\\"(\\d+)\\"").match(newXf);
            if (fontm.hasMatch()) newXf.replace(fontm.capturedStart(1), fontm.capturedLength(1), QString::number(whiteFontId));
            else if (newXf.endsWith("/>")) newXf.replace("/>", QString(" fontId=\\"%1\\"/>").arg(whiteFontId));
            else newXf.replace(">", QString(" fontId=\\"%1\\">").arg(whiteFontId));
            
            if (!newXf.contains("applyFont=")) {
                if (newXf.endsWith("/>")) newXf.replace("/>", " applyFont=\\"1\\"/>");
                else newXf.replace(">", " applyFont=\\"1\\">");
            }
        }

        originalXfs.append(newXf);
        styleMap[cacheKey] = nextXfIndex;
        int resultIdx = nextXfIndex;
        nextXfIndex++;
        return resultIdx;
    };

    QString sheetStr = QString::fromUtf8(dst.cachedZipEntries[sheetPath]);
    QRegularExpression rowStartRe("<row\\\\b[^>]*\\\\br=\\"(\\d+)\\"");
    QRegularExpression sAttrRe("\\\\bs=\\"(\\d+)\\"");
    
    int currentRow = 0;
    QString result;
    result.reserve(sheetStr.size() + sheetStr.size() / 10);
    int pos = 0;

    while (pos < sheetStr.size()) {
        int nextRow = sheetStr.indexOf("<row ", pos);
        int nextCell = sheetStr.indexOf("<c ", pos);
        
        int nextTag = -1; bool isRow = false;
        if (nextRow != -1 && (nextCell == -1 || nextRow < nextCell)) { nextTag = nextRow; isRow = true; }
        else if (nextCell != -1) { nextTag = nextCell; isRow = false; }
        
        if (nextTag == -1) { result.append(sheetStr.mid(pos)); break; }
        
        result.append(sheetStr.mid(pos, nextTag - pos));
        int tagEnd = sheetStr.indexOf('>', nextTag);
        if (tagEnd == -1) { result.append(sheetStr.mid(nextTag)); break; }
        
        QString tagContent = sheetStr.mid(nextTag, tagEnd - nextTag + 1);

        if (isRow) {
            QRegularExpressionMatch rm = rowStartRe.match(tagContent);
            if (rm.hasMatch()) currentRow = rm.captured(1).toInt();
            
            // NOTE: DO NOT COLOR HEADERS (rows 1-7).
            // Actually, my code just colors whatever mappingRows implies mapping.
        }

        if (!isRow && currentRow > 7) { // Only style if NOT header row
            bool isDark = mappingRows.contains(currentRow);
            QRegularExpressionMatch sm = sAttrRe.match(tagContent);
            if (sm.hasMatch()) {
                int oldS = sm.captured(1).toInt();
                int mappedS = getColoredXf(oldS, isDark);
                tagContent.replace(sm.capturedStart(1), sm.capturedLength(1), QString::number(mappedS));
            } else {
                int mappedS = getColoredXf(0, isDark);
                tagContent.insert(3, QString("s=\\"%1\\" ").arg(mappedS));
            }
        }
        
        result.append(tagContent);
        pos = tagEnd + 1;
    }

    dst.cachedZipEntries[sheetPath] = result.toUtf8();
    
    if (nextXfIndex > xfCount) {
        replaceStyleSection(stylesXml, "cellXfs", originalXfs.join("").toUtf8(), nextXfIndex);
        dst.cachedZipEntries["xl/styles.xml"] = stylesXml;
    }
}'''

new_copy_styles = '''    MergeStylesResult styleResult = mergeStyles(src, dst);

    mergeSharedStrings(src, dst, sheetXml);

    if (styleResult.valid && src.filePath != dst.filePath) {
        QString sheetStr = QString::fromUtf8(sheetXml);
        QRegularExpression sRe("\\\\bs=\\"(\\d+)\\"");
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

    if (styleResult.valid && !highlightRows.isEmpty()) {
        QSet<int> mappingRowSet(highlightRows.begin(), highlightRows.end());
        applyRowColors(dst, dstSheetPath, mappingRowSet, 0, 0); // No offsets needed; everything is calculated dynamically based on sheet's s=""
    }
'''

content = re.sub(r'ExcelHandler::MergeStylesResult ExcelHandler::mergeStyles.*?return result;\n}', new_merge_styles, content, flags=re.DOTALL)
content = re.sub(r'void ExcelHandler::applyRowColors\(WorkbookData& dst, const QString& sheetPath,\s*const QSet<int>& mappingRows,\s*int xfOffsetLightBlue,\s*int xfOffsetDarkBlue\).*?dst\.cachedZipEntries\["xl/styles\.xml"\] = dstStyles;\n}', new_apply_colors, content, flags=re.DOTALL)
content = re.sub(r'    MergeStylesResult styleResult = mergeStyles.*?srcXfCount\);\s*}', new_copy_styles, content, flags=re.DOTALL)

open('services/excelhandler.cpp', 'w', encoding='utf-8').write(content)
print("done")
