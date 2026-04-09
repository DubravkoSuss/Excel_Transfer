# Ignore Rows Function Fix

## Problem
The "Ignore" button on mapping cards wasn't working during transfer execution. Users could select rows to ignore in the dialog, but those rows were still being transferred.

## Root Cause
Classic MVC desynchronization:

1. ✅ **View (MappingRow widget)** - Gets updated when user clicks "Ignore"
   - `MappingRow::setIgnoredRows()` updates `m_entry.ignoredDestRows`

2. ❌ **Model (MappingModel)** - Never gets updated
   - Model still has empty `ignoredDestRows` set

3. ❌ **Controller reads from Model** - During execution
   - `m_mappingController->items()` returns stale data from model
   - Transfer uses entries with empty `ignoredDestRows`

4. ❌ **Transfer skips nothing** - Because `ignoredDestRows` is empty
   - `TransferService` checks `entry.ignoredDestRows.contains(destRow)` → always false

## The Fix

### 1. Added `updateEntryAt()` to MappingModel
**File: `core/mappingmodel.h`**
```cpp
void updateEntryAt(int index, const MappingEntry& entry);
```

**File: `core/mappingmodel.cpp`**
```cpp
void MappingModel::updateEntryAt(int index, const MappingEntry& entry)
{
    if (index >= 0 && index < m_items.size()) {
        m_items[index].entry = entry;
        // Don't emit dataChanged() — avoids triggering rebuildUI() which would
        // recreate all widgets. The model is now in sync; UI already reflects the change.
    }
}
```

### 2. Added `updateEntryAt()` to MappingController
**File: `core/mappingcontroller.h`**
```cpp
void updateEntryAt(int index, const MappingEntry& entry);
```

**File: `core/mappingcontroller.cpp`**
```cpp
void MappingController::updateEntryAt(int index, const MappingEntry& entry)
{
    if (m_model) {
        m_model->updateEntryAt(index, entry);
    }
}
```

### 3. Added getter for ignored rows
**File: `features/mappings/mappingrow.h`**
```cpp
QSet<int> ignoredRows() const { return m_entry.ignoredDestRows; }
```

### 4. Sync model when dialog closes
**File: `ui/mainwindow.cpp` - `onIgnoreRows()` method**
```cpp
if (dlg.exec() == QDialog::Accepted) {
    entry.ignoredDestRows = dlg.ignoredDestRows();
    row->setIgnoredRows(dlg.ignoredDestRows());
    
    // ✅ FIX: Sync back to model so items() returns updated data
    m_mappingController->updateEntryAt(index, entry);
    
    // ... toast messages ...
}
```

## Design Decisions

### Why not emit `dataChanged()` in `updateEntryAt()`?
Emitting `dataChanged()` would trigger `rebuildUI()` which:
- Destroys all existing MappingRow widgets
- Recreates them from scratch
- Loses UI state (scroll position, focus, etc.)
- Causes visual flicker

Since the UI already reflects the change (user just closed the dialog), we only need to sync the model silently.

### Why not read from widgets during execution?
The current approach of reading from the model is correct:
- Model is the single source of truth
- Widgets are just views of the model
- The bug was that widgets weren't syncing back to model

The fix properly closes the data flow loop: **UI → Widget → Model → Execution**

## Testing
1. Load mappings for a month/year
2. Click "Ignore" on a mapping card
3. Select some rows to ignore
4. Click "Apply"
5. Execute transfer (Execute All or Rolling Transfer)
6. ✅ Verify ignored rows are NOT transferred
7. ✅ Check status log shows correct cell count (excluding ignored rows)

## Related Issues
This same pattern (widget updates not syncing to model) also affects:
- `copyFullSheet` 
- `customSheetName`
- `insertAfterSheet`

These currently have workarounds in `onExecuteAll()` that read directly from widgets. Consider refactoring those to also use `updateEntryAt()` when those properties change in the UI.

## Files Modified
- `core/mappingmodel.h` - Added `updateEntryAt()` declaration
- `core/mappingmodel.cpp` - Added `updateEntryAt()` implementation
- `core/mappingcontroller.h` - Added `updateEntryAt()` declaration
- `core/mappingcontroller.cpp` - Added `updateEntryAt()` implementation
- `features/mappings/mappingrow.h` - Added `ignoredRows()` getter
- `ui/mainwindow.cpp` - Updated `onIgnoreRows()` to sync model
