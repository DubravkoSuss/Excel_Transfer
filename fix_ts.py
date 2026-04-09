import sys
file_path = r'c:\Users\dposavac\CLionProjects\Excel_transfer\services\transferservice.cpp'
with open(file_path, 'r', encoding='utf-8') as f:
    text = f.read()

target = '''        }
        const QStringList names = m_handler->getSheetNames(key);'''

replacement = '''        }

        // Smart fallback: If the requested sheet is "Report" but the SAP file uses the "MM_YYYY" sheet name format
        if (requestedNorm == "report") {
            QString monthNum = ExcelHandler::MONTH_TO_NUM.value(month, "");
            if (!monthNum.isEmpty()) {
                QString altName = QString("%1_%2").arg(monthNum).arg(year);
                for (const QString& name : m_handler->getSheetNames(key)) {
                    if (name.trimmed().toLower() == altName.toLower()) {
                        qInfo() << "TransferService: Found alternate sheet name" << altName << "instead of" << requested;
                        return name;
                    }
                }
            }
        }

        const QStringList names = m_handler->getSheetNames(key);'''

text = text.replace(target.replace('\n', '\r\n'), replacement.replace('\n', '\r\n'))
text = text.replace(target, replacement)

with open(file_path, 'w', encoding='utf-8') as f:
    f.write(text)

print('Success.')
