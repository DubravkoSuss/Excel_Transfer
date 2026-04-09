# Complete Analysis: Ignore Rows Not Working

## What We Tried (First Attempt)

### Changes Made:
1. ✅ Added `MappingModel::updateEntryAt()` - syncs model when dialog closes
2. ✅ Added `MappingController::updateEntryAt()` - delegates to model
3. ✅ Added `MappingRow::ignoredRows()` getter - cleaner API
4. ✅ Updated `MainWindow::onIgnoreRows()` - calls `updateEntryAt()` after dialog

### Why It Didn't Work:
The model sync is correct, but there are TWO execution paths that read mappings:

## Execution Path Analysis

### Path 1: Execute All (Individual Transfer)
**File:** `ui/mainwindow.cpp` - `onExecuteAll()` method (line ~1284)

```cpp
QVector<MappingItem> items = m_mappingController->items();  // ← reads from MODEL
for (int i = 0; i < items.size(); ++i) {
    const MappingItem& item = items[i];
    MappingRow* row = m_mappingController->rowAt(i);
    
    TransferMapping tm;
    tm.entry = item.entry;  // ← MODEL entry (may be stale)
    
    // Sync UI state (workaround for stale model)
    tm.entry.copyFullSheet = row->isCopyFullSheet();
    tm.entry.customSheetName = row->getCustomSheetName();
    tm.entry.insertAfterSheet = row->getInsertAfterSheet();
    // ❌ MISSING: tm.entry.ignoredDestRows = row->ignoredRows();
}
```

**Problem:** Even though model is synced via `updateEntryAt()`, this code reads from model first, then patches with widget values. The `ignoredDestRows` patch was missing.

**Fix Applied:** Added line:
```cpp
tm.entry.ignoredDestRows = row->ignoredRows();  // ✅ FIX
```

### Path 2: Rolling Transfer
**File:** `ui/mainwindow.cpp` - `onRollingTransfer()` method (line ~1847)

```cpp
QVector<MappingItem> allItems = checkedItems.isEmpty()
    ? m_mappingController->items()
    : checkedItems;

for (const MappingItem& item : allItems) {
    allEntriesByMonth[key].append(item.entry);  // ← Uses MODEL entry directly
}
```

**Problem:** No widget sync at all - relies entirely on model being up-to-date.

**Fix:** The `updateEntryAt()` we added in `onIgnoreRows()` keeps the model in sync, so this path should work now.

**Caveat:** If user clicks "Load RT" without clicking "Load" first, it loads mappings from JSON files (line 1894+), which don't have `ignoredDestRows` at all. This is expected behavior - ignored rows are UI state, not persisted.

## Complete Fix Summary

### Files Modified:

#### 1. `core/mappingmodel.h`
```cpp
// Added method declaration:
void updateEntryAt(int index, const MappingEntry& entry);
```

#### 2. `core/mappingmodel.cpp`
```cpp
// Added implementation:
void MappingModel::updateEntryAt(int index, const MappingEntry& entry)
{
    if (index >= 0 && index < m_items.size()) {
        m_items[index].entry = entry;
        // Don't emit dataChanged() — avoids triggering rebuildUI()
    }
}
```

#### 3. `core/mappingcontroller.h`
```cpp
// Added method declaration:
void updateEntryAt(int index, const MappingEntry& entry);
```

#### 4. `core/mappingcontroller.cpp`
```cpp
// Added implementation:
void MappingController::updateEntryAt(int index, const MappingEntry& entry)
{
    if (m_model) {
        m_model->updateEntryAt(index, entry);
    }
}
```

#### 5. `features/mappings/mappingrow.h`
```cpp
// Added getter:
QSet<int> ignoredRows() const { return m_entry.ignoredDestRows; }
```

#### 6. `ui/mainwindow.cpp` - `onIgnoreRows()` method
```cpp
if (dlg.exec() == QDialog::Accepted) {
    entry.ignoredDestRows = dlg.ignoredDestRows();
    row->setIgnoredRows(dlg.ignoredDestRows());
    
    // ✅ FIX: Sync back to model
    m_mappingController->updateEntryAt(index, entry);
    
    // ... toast messages ...
}
```

#### 7. `ui/mainwindow.cpp` - `onExecuteAll()` method (line ~1305)
```cpp
// Ensure latest UI state for copy-full-sheet option and ignored rows
tm.entry.copyFullSheet = row->isCopyFullSheet();
tm.entry.customSheetName = row->getCustomSheetName();
tm.entry.insertAfterSheet = row->getInsertAfterSheet();
tm.entry.ignoredDestRows = row->ignoredRows();  // ✅ FIX: Added this line
```

## Why Two Fixes Were Needed

### Fix #1: Model Sync (`updateEntryAt`)
- Keeps model in sync when dialog closes
- Required for Rolling Transfer (which reads directly from model)
- Good MVC practice - model should be source of truth

### Fix #2: Widget Sync in `onExecuteAll`
- Execute All reads from model, then patches with widget values
- Already had patches for `copyFullSheet`, `customSheetName`, `insertAfterSheet`
- Missing patch for `ignoredDestRows`
- This is a workaround for the fact that model can be stale

## Testing Checklist

### Execute All Path:
1. ✅ Load mappings for a month
2. ✅ Click "Ignore" on a card
3. ✅ Select rows to ignore
4. ✅ Click "Apply"
5. ✅ Click "Execute All"
6. ✅ Verify ignored rows are NOT transferred
7. ✅ Check cell count excludes ignored rows

### Rolling Transfer Path:
1. ✅ Load mappings for multiple months
2. ✅ Click "Ignore" on cards
3. ✅ Select rows to ignore
4. ✅ Click "Apply"
5. ✅ Click "Load RT"
6. ✅ Click "Execute RT"
7. ✅ Verify ignored rows are NOT transferred

### Edge Cases:
- ❌ Load RT without Load first → ignored rows lost (expected - JSON doesn't store UI state)
- ✅ Ignore rows, uncheck card, re-check card → ignored rows preserved
- ✅ Ignore rows, close app, reopen → ignored rows lost (expected - not persisted)

## Root Cause

Classic MVC desynchronization with TWO symptoms:

1. **Model not updated** - Widget changes didn't sync back to model
2. **Incomplete widget sync** - Execute All patched some fields but not `ignoredDestRows`

Both needed fixing for complete solution.

## Future Improvements

### Implemented Improvements:

#### 1. Fine-Grained Signal (`entryUpdated`)
Added `entryUpdated(int index)` signal to `MappingModel` instead of suppressing `dataChanged()`:

```cpp
// core/mappingmodel.h
signals:
    void dataChanged();
    void entryUpdated(int index);  // Fine-grained signal for single entry updates

// core/mappingmodel.cpp
void MappingModel::updateEntryAt(int index, const MappingEntry& entry)
{
    if (index >= 0 && index < m_items.size()) {
        m_items[index].entry = entry;
        emit entryUpdated(index);  // Allows downstream reactions without full rebuild
    }
}
```

**Benefits:**
- Downstream consumers can react to single-entry changes
- Avoids triggering full UI rebuild (which would destroy widgets)
- More maintainable - explicit about what changed

#### 2. Centralized Sync Helper (`syncRowToModel`)
Added helper method to sync all widget state to model in one place:

```cpp
// core/mappingcontroller.h
void syncRowToModel(int index);  // Sync widget state back to model

// core/mappingcontroller.cpp
void MappingController::syncRowToModel(int index)
{
    MappingRow* row = rowAt(index);
    if (!row || !m_model) return;
    
    MappingEntry entry = row->getMapping();
    entry.copyFullSheet = row->isCopyFullSheet();
    entry.customSheetName = row->getCustomSheetName();
    entry.insertAfterSheet = row->getInsertAfterSheet();
    entry.ignoredDestRows = row->ignoredRows();
    
    m_model->updateEntryAt(index, entry);
}
```

**Benefits:**
- Single source of truth for widget→model sync logic
- New UI-settable fields only need to be added here
- Can be called from any widget change handler

### Remaining Technical Debt:

The widget sync workaround in `onExecuteAll` is still needed:

```cpp
// TODO: TECHNICAL DEBT - These widget syncs are a workaround for model staleness.
tm.entry.copyFullSheet = row->isCopyFullSheet();
tm.entry.customSheetName = row->getCustomSheetName();
tm.entry.insertAfterSheet = row->getInsertAfterSheet();
tm.entry.ignoredDestRows = row->ignoredRows();
```

**To eliminate this:**
1. Connect widget change signals (checkbox toggled, text edited, etc.) to `syncRowToModel()`
2. Ensure model is always current when widgets change
3. Remove all widget sync patches from `onExecuteAll`
4. Trust model as single source of truth

**Challenges:**
- Need to identify all widget change points
- May impact performance if syncing too frequently
- Need to avoid sync loops (widget→model→signal→widget)

**Recommended approach:**
- Sync on focus loss / editing finished, not on every keystroke
- Use `QSignalBlocker` during programmatic widget updates
- Add debouncing for text fields
