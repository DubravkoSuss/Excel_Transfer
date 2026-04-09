import re
file_path = r'c:\Users\dposavac\CLionProjects\Excel_transfer\services\excelhandler.cpp'
with open(file_path, 'r', encoding='utf-8') as f:
    text = f.read()

# Fix 1: sdEnd updates
text = re.sub(
    r'(\s+output\.replace\(rowTagEnd - 1, 2, replacement\);)',
    r'\1\n                    sdEnd += replacement.size() - 2;',
    text)

text = re.sub(
    r'(\s+output\.replace\(rowTagEnd \+ 1, rowClose - rowTagEnd - 1, rowContent\);)',
    r'\n                        int oldSize = rowClose - rowTagEnd - 1;\n                        output.replace(rowTagEnd + 1, oldSize, rowContent);\n                        sdEnd += rowContent.size() - oldSize;',
    text)

text = re.sub(
    r'(output\.insert\(insertPos, newRow\);)',
    r'\1\n                sdEnd += newRow.size();',
    text)

# Fix 2: Shared formula deduplication preservation
dedup_repl = """if (!matches.isEmpty()) {
                    QString out;
                    out.reserve(input.size());
                    int lastPos = 0, dupes = 0;
                    
                    QMap<QString, QString> preservedFormulas;
                    for (int i = 0; i < matches.size(); ++i) {
                        const auto& cm = matches[i];
                        if (lastIdx[cm.ref] != i) {
                            int fStart = cm.full.indexOf("<f ");
                            if (fStart == -1) fStart = cm.full.indexOf("<f>");
                            if (fStart != -1) {
                                int fEnd = cm.full.indexOf("</f>", fStart);
                                if (fEnd != -1) {
                                    preservedFormulas[cm.ref] = cm.full.mid(fStart, fEnd - fStart + 4);
                                } else {
                                    int fSelfClose = cm.full.indexOf("/>", fStart);
                                    if (fSelfClose != -1) {
                                        preservedFormulas[cm.ref] = cm.full.mid(fStart, fSelfClose - fStart + 2);
                                    }
                                }
                            }
                        }
                    }

                    for (int i = 0; i < matches.size(); ++i) {
                        const auto& cm = matches[i];
                        out.append(input.mid(lastPos, cm.start - lastPos));
                        if (lastIdx[cm.ref] == i) {
                            QString finalCell = cm.full;
                            if (preservedFormulas.contains(cm.ref)) {
                                int vStart = finalCell.indexOf("<v>");
                                if (vStart != -1) {
                                    finalCell.insert(vStart, preservedFormulas[cm.ref]);
                                } else {
                                    int cEnd = finalCell.lastIndexOf("</c>");
                                    if (cEnd != -1) {
                                        finalCell.insert(cEnd, preservedFormulas[cm.ref]);
                                    } else {
                                        int cSelfClose = finalCell.lastIndexOf("/>");
                                        if (cSelfClose != -1) {
                                            finalCell = finalCell.mid(0, cSelfClose) + ">" + preservedFormulas[cm.ref] + "</c>";
                                        }
                                    }
                                }
                            }
                            out.append(finalCell);
                        } else {
                            dupes++;
                            qWarning() << "[DUP CELL REMOVED]" << cm.ref;
                        }
                        lastPos = cm.end;
                    }
                    out.append(input.mid(lastPos));
                    if (dupes > 0) {
                        qInfo() << "[DUP CELL] Removed" << dupes << "duplicates, salvaged" << preservedFormulas.size() << "formulas.";
                        merged = out.toUtf8();
                    }
                }"""

# Use targeted replace instead of error-prone regex for the large string
start_str = "if (!matches.isEmpty()) {"
end_str = "merged = out.toUtf8();\n                    }\n                }"
start_idx = text.find(start_str)
end_idx = text.find(end_str, start_idx) + len(end_str)

if start_idx != -1 and end_idx != -1:
    # replace the block
    text = text[:start_idx] + dedup_repl + text[end_idx:]
else:
    print("WARNING: dedup block not found")

with open(file_path, 'w', encoding='utf-8') as f:
    f.write(text)

print('Success.')
