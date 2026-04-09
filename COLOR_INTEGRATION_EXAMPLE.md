# Color Integration Example

## How to Integrate Color Application in FillAllWorker

This guide shows exactly where and how to call the color functions in your existing transfer code.

## Location: features/transfer/fillallworker.cpp

### Current Code Structure

Your current `FillAllWorker::run()` likely has code like this:

```cpp
// Inside the loop where you copy sheets
if (fileEntry.copyFullSheet) {
    const QString srcSheet = fileEntry.sourceSheetName.isEmpty()
                             ? "Sheet1" : fileEntry.sourceSheetName;
    const QString newName  = fileEntry.customSheetName.isEmpty()
                             ? srcSheet : fileEntry.customSheetName;
    
    bool ok = m_handler->copyFullSheet(srcKey, srcSheet, destKey, newName);
    
    if (!ok) {
        result.errors.append(
            QString("[%1] %2: copyFullSheet failed (%3 → %4)")
                .arg(monthName, fileEntry.transferType, srcSheet, newName));
        result.failCount++;
        continue;
    }
    
    result.successCount++;
    continue; // Skip normal transfer logic
}
```

### Modified Code with Color Application

Replace the above with this enhanced version:

```cpp
// Inside the loop where you copy sheets
if (fileEntry.copyFullSheet) {
    const QString srcSheet = fileEntry.sourceSheetName.isEmpty()
                             ? "Sheet1" : fileEntry.sourceSheetName;
    const QString newName  = fileEntry.customSheetName.isEmpty()
                             ? srcSheet : fileEntry.customSheetName;
    
    // ── STEP 1: Copy the full sheet ──
    bool ok = m_handler->copyFullSheet(srcKey, srcSheet, destKey, newName);
    
    if (!ok) {
        result.errors.append(
            QString("[%1] %2: copyFullSheet failed (%3 → %4)")
                .arg(monthName, fileEntry.transferType, srcSheet, newName));
        result.failCount++;
        continue;
    }
    
    // ── STEP 2: Build mapping row set ──
    // Adjust these paths to match your project structure
    QString projectDir = QDir::currentPath(); // or get from config
    QString mappingsPath    = projectDir + "/JSON/mappings.json";
    QString mappingsOldPath = projectDir + "/JSON/mappings_old.json";
    
    // Build the set of rows that should be dark blue
    QSet<int> mappingRows = m_handler->buildMappingRowSet(
        mappingsPath, 
        mappingsOldPath, 
        newName  // Use the destination sheet name
    );
    
    qInfo() << "[FillAllWorker] Sheet:" << newName 
            << "has" << mappingRows.size() << "mapping rows to highlight";
    
    // ── STEP 3: Apply colors ──
    // Get the workbook data to access sheet path
    // Note: You may need to add a public getter in ExcelHandler if needed
    // For now, construct the path based on convention
    QString sheetPath = QString("xl/worksheets/sheet%1.xml").arg(sheetNum);
    
    // Alternative: If you have access to the WorkbookData:
    // QString sheetPath = dst.sheetPathMap.value(newName);
    
    if (!sheetPath.isEmpty()) {
        // This is a private method, so you need to call it through ExcelHandler
        // You may need to make this public or add a wrapper method
        m_handler->applyRowColors(destKey, sheetPath, mappingRows);
        
        qInfo() << "[FillAllWorker] Applied colors to sheet:" << newName;
    } else {
        qWarning() << "[FillAllWorker] Cannot find sheet path for" << newName;
    }
    
    result.successCount++;
    continue; // Skip normal transfer logic
}
```

---

## Alternative: Add Public Wrapper Method

If `applyRowColors()` needs access to internal WorkbookData, add this public wrapper to ExcelHandler:

### In services/excelhandler.h

```cpp
public:
    // ... existing public methods ...
    
    // Apply row colors to a copied sheet
    bool applyRowColorsToSheet(const QString& key, 
                               const QString& sheetName,
                               const QSet<int>& mappingRows);
```

### In services/excelhandler.cpp

```cpp
bool ExcelHandler::applyRowColorsToSheet(const QString& key, 
                                         const QString& sheetName,
                                         const QSet<int>& mappingRows)
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
    
    applyRowColors(wb, sheetPath, mappingRows);
    return true;
}
```

### Then in FillAllWorker:

```cpp
// ── STEP 3: Apply colors (using public wrapper) ──
bool colorOk = m_handler->applyRowColorsToSheet(destKey, newName, mappingRows);

if (colorOk) {
    qInfo() << "[FillAllWorker] Applied colors to sheet:" << newName;
} else {
    qWarning() << "[FillAllWorker] Failed to apply colors to sheet:" << newName;
}
```

---

## Complete Integration Example

Here's a complete example showing the full flow:

```cpp
void FillAllWorker::run()
{
    // ... your existing setup code ...
    
    for (const FillAllEntry& fileEntry : m_entries) {
        
        // ... your existing file loading code ...
        
        if (fileEntry.copyFullSheet) {
            const QString srcSheet = fileEntry.sourceSheetName.isEmpty()
                                     ? "Sheet1" : fileEntry.sourceSheetName;
            const QString newName  = fileEntry.customSheetName.isEmpty()
                                     ? srcSheet : fileEntry.customSheetName;
            
            qInfo() << "[FillAllWorker] Copying full sheet:" << srcSheet 
                    << "→" << newName << "for month:" << monthName;
            
            // ═══════════════════════════════════════════════════════════
            // STEP 1: COPY SHEET
            // ═══════════════════════════════════════════════════════════
            bool ok = m_handler->copyFullSheet(srcKey, srcSheet, destKey, newName);
            
            if (!ok) {
                result.errors.append(
                    QString("[%1] %2: copyFullSheet failed (%3 → %4)")
                        .arg(monthName, fileEntry.transferType, srcSheet, newName));
                result.failCount++;
                continue;
            }
            
            qInfo() << "[FillAllWorker] Sheet copied successfully";
            
            // ═══════════════════════════════════════════════════════════
            // STEP 2: BUILD MAPPING ROW SET
            // ═══════════════════════════════════════════════════════════
            QString projectDir = QDir::currentPath();
            QString mappingsPath    = projectDir + "/JSON/mappings.json";
            QString mappingsOldPath = projectDir + "/JSON/mappings_old.json";
            
            QSet<int> mappingRows = m_handler->buildMappingRowSet(
                mappingsPath, 
                mappingsOldPath, 
                newName
            );
            
            qInfo() << "[FillAllWorker] Found" << mappingRows.size() 
                    << "mapping rows for sheet:" << newName;
            
            // ═══════════════════════════════════════════════════════════
            // STEP 3: APPLY COLORS
            // ═══════════════════════════════════════════════════════════
            bool colorOk = m_handler->applyRowColorsToSheet(
                destKey, 
                newName, 
                mappingRows
            );
            
            if (colorOk) {
                qInfo() << "[FillAllWorker] Colors applied successfully to:" << newName;
            } else {
                qWarning() << "[FillAllWorker] Failed to apply colors to:" << newName;
                // Not a critical error, continue anyway
            }
            
            result.successCount++;
            
            // Emit progress
            emit progressUpdate(
                ++processedCount, 
                totalEntries,
                QString("Copied and colored sheet: %1").arg(newName)
            );
            
            continue; // Skip normal transfer logic
        }
        
        // ... rest of your normal transfer logic ...
    }
    
    // ... your existing save and cleanup code ...
}
```

---

## Configuration: JSON File Paths

Make sure your JSON file paths are correct. You have several options:

### Option 1: Relative to Current Directory
```cpp
QString projectDir = QDir::currentPath();
QString mappingsPath = projectDir + "/JSON/mappings.json";
```

### Option 2: Relative to Executable
```cpp
QString exeDir = QCoreApplication::applicationDirPath();
QString mappingsPath = exeDir + "/JSON/mappings.json";
```

### Option 3: From Configuration
```cpp
// If you have a config class
QString mappingsPath = m_config->getMappingsPath();
```

### Option 4: Hardcoded (for testing)
```cpp
QString mappingsPath = "C:/Projects/ExcelTransfer/JSON/mappings.json";
```

---

## Error Handling

Add robust error handling:

```cpp
// Check if JSON files exist before building mapping set
QFileInfo mappingsFile(mappingsPath);
QFileInfo mappingsOldFile(mappingsOldPath);

if (!mappingsFile.exists()) {
    qWarning() << "[FillAllWorker] Mappings file not found:" << mappingsPath;
    qWarning() << "[FillAllWorker] Skipping color application for:" << newName;
    // Continue without colors
    result.successCount++;
    continue;
}

QSet<int> mappingRows;
if (mappingsFile.exists()) {
    mappingRows = m_handler->buildMappingRowSet(
        mappingsPath, 
        mappingsOldFile.exists() ? mappingsOldPath : QString(), 
        newName
    );
}

// Only apply colors if we have mapping rows
if (!mappingRows.isEmpty()) {
    bool colorOk = m_handler->applyRowColorsToSheet(destKey, newName, mappingRows);
    if (!colorOk) {
        qWarning() << "[FillAllWorker] Color application failed, but sheet was copied";
    }
} else {
    qInfo() << "[FillAllWorker] No mapping rows found, applying light blue to all rows";
    // Still apply colors (all rows will be light blue)
    m_handler->applyRowColorsToSheet(destKey, newName, QSet<int>());
}
```

---

## Testing the Integration

### Test 1: Single Sheet Copy with Colors
```cpp
// In your test code or main window
FillAllScanResult testResult;
FillAllEntry entry;
entry.month = "January";
entry.year = 2024;
entry.transferType = "sap";
entry.copyFullSheet = true;
entry.sourceSheetName = "SAP_Data";
entry.customSheetName = "January";

testResult.entries.append(entry);

FillAllWorker* worker = new FillAllWorker(m_handler, testResult);
connect(worker, &FillAllWorker::finished, this, [](const FillAllResult& result) {
    qDebug() << "Success count:" << result.successCount;
    qDebug() << "Errors:" << result.errors;
});

worker->start();
```

### Test 2: Verify Colors in Excel
1. Run the transfer
2. Open the destination Excel file
3. Check that:
   - Rows from mappings.json are dark blue with white text
   - Other rows are light blue with black text
   - Fonts and borders are preserved

---

## Troubleshooting Integration

### Issue: "applyRowColors is private"
**Solution**: Add the public wrapper method `applyRowColorsToSheet()` as shown above.

### Issue: "Cannot find JSON files"
**Solution**: 
1. Print the full path: `qDebug() << "Looking for:" << mappingsPath;`
2. Verify the file exists at that location
3. Use absolute paths for testing

### Issue: "No mapping rows found"
**Solution**:
1. Check JSON structure matches expected format
2. Verify `destination_sheet` field matches sheet name exactly
3. Add debug output in `buildMappingRowSet()`

### Issue: "Colors not visible in Excel"
**Solution**:
1. Check log for: `[applyRowColors] Colorized ...`
2. Verify `saveWorkbook()` is called after color application
3. Try opening with different Excel version (2016+)

---

## Summary

1. Add public wrapper `applyRowColorsToSheet()` to ExcelHandler
2. In FillAllWorker, after `copyFullSheet()`:
   - Call `buildMappingRowSet()` to get mapping rows
   - Call `applyRowColorsToSheet()` to apply colors
3. Handle errors gracefully (missing JSON files, etc.)
4. Test with sample data before production use

The integration is straightforward and non-invasive to your existing code!
