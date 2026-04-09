# Ignore Rows Fix - Final Implementation

## Problem
Users could select rows to ignore in the dialog, but those rows were still being transferred during execution.

## Root Cause
Two-part MVC desynchronization:
1. **Model never updated** - Widget changes didn't sync back to model
2. **Incomplete widget sync** - Execute All patched some fields but not `ignoredDestRows`

## Solution Overview

### Part 1: Model Synchronization
Added infrastructure to keep model in sync with widget changes.

### Part 2: Execution Path Fixes
Fixed both execution paths (Execute All and Rolling Transfer) to use ignored rows.

### Part 3: Robustness Improvements
Added fine-grained signals and centralized sync helper for maintainability.

---

## Implementation Details

### 1. Fine-Grained Model Signal

**File: `core/mappingmodel.h`**
```cpp
signals:
    void dataChanged();
    void entryUpdated(int index);  // NEW: Fine-grained signal
```

**File: `core/mappingmodel.cpp`**
```cpp
void MappingModel::updateEntryAt(int index, const MappingEntry& entry)
{
    if (index >= 0 && index < m_items.size()) {
        m_items[index].entry = entry;
        emit entryUpdated(index);  // Avoids full UI rebuild
    }
}
```

**Why:** Allows downstream consumers to react to single-entry changes without triggering full UI rebuild (which would destroy and recreate all widgets).

---

### 2. Centralized Sync Helper

**File: `core/mappingcontroller.h`**
```cpp
void syncRowToModel(int index);  // Sync widget state back to model
```

**File: `core/mappingcontroller.cpp`**
```cpp
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

**Why:** Single source of truth for widget→model sync. New UI-settable fields only need to be added here.

---

### 3. Widget Getter

**File: `features/mappings/mappingrow.h`**
```cpp
QSet<int> ignoredRows() const { return m_entry.ignoredDestRows; }
```

**Why:** Cleaner API for accessing ignored rows from widget.

---

### 4. Dialog Close Handler

**File: `ui/mainwindow.cpp` - `onIgnoreRows()` method**
```cpp
if (dlg.exec() == QDialog::Accepted) {
    entry.ignoredDestRows = dlg.ignoredDestRows();
    row->setIgnoredRows(dlg.ignoredDestRows());
    
    // Sync all widget state back to model
    m_mappingController->syncRowToModel(index);
    
    // ... toast messages ...
}
```

**Why:** Keeps model in sync when user closes ignore dialog. Required for Rolling Transfer path.

---

### 5. Execute All Widget Sync

**File: `ui/mainwindow.cpp` - `onExecuteAll()` method (line ~1295)**
```cpp
TransferMapping tm;
tm.entry = item.entry;  // Read from model
tm.year  = item.year;
tm.month = item.month;
tm.rowIndex = i;

// TODO: TECHNICAL DEBT - Workaround for model staleness
tm.entry.copyFullSheet = row->isCopyFullSheet();
tm.entry.customSheetName = row->getCustomSheetName();
tm.entry.insertAfterSheet = row->getInsertAfterSheet();
tm.entry.ignoredDestRows = row->ignoredRows();  // ✅ ADDED
```

**Why:** Execute All reads from model first, then patches with widget values. The `ignoredDestRows` patch was missing.

---

## Files Modified

1. ✅ `core/mappingmodel.h` - Added `entryUpdated` signal and `updateEntryAt()` declaration
2. ✅ `core/mappingmodel.cpp` - Implemented `updateEntryAt()` with fine-grained signal
3. ✅ `core/mappingcontroller.h` - Added `updateEntryAt()` and `syncRowToModel()` declarations
4. ✅ `core/mappingcontroller.cpp` - Implemented both methods
5. ✅ `features/mappings/mappingrow.h` - Added `ignoredRows()` getter
6. ✅ `ui/mainwindow.cpp` (`onIgnoreRows`) - Use `syncRowToModel()` after dialog
7. ✅ `ui/mainwindow.cpp` (`onExecuteAll`) - Added `ignoredDestRows` widget sync

---

## Testing Checklist

### Execute All Path:
- [x] Load mappings for a month
- [x] Click "Ignore" on a card
- [x] Select rows to ignore
- [x] Click "Apply"
- [x] Click "Execute All"
- [x] Verify ignored rows are NOT transferred
- [x] Check cell count excludes ignored rows

### Rolling Transfer Path:
- [x] Load mappings for multiple months
- [x] Click "Ignore" on cards
- [x] Select rows to ignore
- [x] Click "Apply"
- [x] Click "Load RT"
- [x] Click "Execute RT"
- [x] Verify ignored rows are NOT transferred

### Edge Cases:
- [x] Ignore rows, uncheck card, re-check card → ignored rows preserved
- [x] Multiple cards with different ignored rows → each card respects its own
- [x] Ignore all rows → transfer shows 0 cells for that mapping

---

## Architecture Improvements

### Before (Fragile):
```
User clicks Ignore
    ↓
Widget updated ✅
    ↓
Model NOT updated ❌
    ↓
Execute reads from model → ignored rows lost ❌
```

### After (Robust):
```
User clicks Ignore
    ↓
Widget updated ✅
    ↓
syncRowToModel() called ✅
    ↓
Model updated ✅
    ↓
entryUpdated(index) signal emitted ✅
    ↓
Execute reads from model → ignored rows preserved ✅
Execute also patches from widget (belt & suspenders) ✅
```

---

## Remaining Technical Debt

The widget sync workaround in `onExecuteAll` is still needed because:
- Model may be stale if user changes widgets without triggering sync
- Other fields (`copyFullSheet`, `customSheetName`, `insertAfterSheet`) also use this pattern

**To eliminate:**
1. Connect all widget change signals to `syncRowToModel()`
2. Ensure model is always current
3. Remove widget sync patches from `onExecuteAll`
4. Trust model as single source of truth

**Challenges:**
- Need to identify all widget change points
- May impact performance if syncing too frequently
- Need to avoid sync loops

**Recommended approach:**
- Sync on focus loss / editing finished, not on every keystroke
- Use `QSignalBlocker` during programmatic updates
- Add debouncing for text fields

---

## Design Patterns Used

### 1. Model-View-Controller (MVC)
- **Model:** `MappingModel` - stores data
- **View:** `MappingRow` - displays data
- **Controller:** `MappingController` - coordinates updates

### 2. Fine-Grained Signals
- `dataChanged()` - full model changed (triggers rebuild)
- `entryUpdated(int)` - single entry changed (no rebuild)

### 3. Centralized Sync
- `syncRowToModel()` - single place for widget→model sync logic
- Reduces duplication and maintenance burden

### 4. Belt and Suspenders
- Model is synced when dialog closes
- Execute also patches from widgets
- Both paths work independently, providing redundancy

---

## Lessons Learned

1. **MVC requires discipline** - Changes must flow through proper channels
2. **Signals matter** - Fine-grained signals prevent unnecessary work
3. **Centralize sync logic** - Reduces bugs when adding new fields
4. **Document technical debt** - Makes future refactoring easier
5. **Test both paths** - Multiple execution paths need separate verification

---

## Success Criteria

✅ Ignored rows are respected during Execute All
✅ Ignored rows are respected during Rolling Transfer  
✅ Model stays in sync with widget changes
✅ No full UI rebuilds on single entry updates
✅ Centralized sync logic for maintainability
✅ Technical debt documented for future work
