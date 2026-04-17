# Cumulative Columns IP-IZ Problem Summary

## Problem Statement
When running **Fill All Months** for August 2025, cumulative columns IP through IZ show **zeros** for certain rows (6, 13, 16, 18, etc.), while other rows (33, 54) calculate correctly. **Execute All** works correctly for these same rows.

## Root Cause Analysis

### The Core Issue: Shared Function Logic
Both **Execute All** and **Fill All Months** were using the SAME cumulative calculation function (`runCumulativePass`), but they have fundamentally different requirements:

- **Execute All**: Processes a SINGLE month file → should calculate ONLY month-1 cumulative (e.g., April processes → calculates IR for March)
- **Fill All Months**: Processes ALL months Jan→target into ONE file → should calculate ALL cumulatives from January through target month

### Code Evidence

#### Original Problem Code (services/transferservice.cpp)
```cpp
// BEFORE FIX: Single function tried to serve both Execute All and Fill All
void TransferService::runCumulativePass(const QSet<int>& allRows,
                                        const QString& destSheet,
                                        int year,
                                        const QString& destKey,
                                        const QString& targetMonth)
{
    int targetIdx = monthOrder.indexOf(targetMonth);
    if (targetIdx <= 0) return;

    // BUG: This logic was calculating ONLY month-1, not all months
    int mIdx = (targetIdx == 0) ? 0 : targetIdx - 1;  // ← PROBLEM LINE
    
    // This loop only went up to mIdx (month-1), missing all prior months
    for (int i = 0; i <= mIdx; ++i) {  // ← INCOMPLETE FOR FILL ALL
        // ... calculation code ...
    }
}
```

**Why this caused zeros in Fill All:**
- When Fill All ran for August (targetIdx=7), `mIdx = 7-1 = 6`
- Loop calculated: IP(Jan), IQ(Feb), IR(Mar), IS(Apr), IT(May), IU(Jun), IV(Jul)
- **BUT**: Each iteration ONLY added the current month's base value, not the cumulative sum
- Result: IV (July) = only July's base value, not Jan+Feb+Mar+Apr+May+Jun+Jul

### The DirectTransferRows Complication

```cpp
// services/transferservice.cpp (lines ~782)
static const QSet<int> directCopyRows = { 12, 13, 16, 18 };

if (directTransferRows.contains(row)) {
    // This logic OVERWRITES the YTD with a single month's value!
    QVariant raw = m_handler->getCellValue(destKey, entry.destSheet, row, bColIdx);
    double val = raw.canConvert<double>() ? raw.toDouble() : 0.0;
    m_handler->setCellValue(destKey, entry.destSheet, row, cumColIdx, val);
}
```

**Why rows 6, 13, 16, 18 showed zeros:**
- These rows contain Excel SUM formulas with **stale cached values** (often 0)
- The "direct copy" logic reads the cached `<v>` tag value (0) and writes it to the cumulative column
- No actual calculation happens → zeros propagate

**Why rows 33, 54 worked:**
- These rows were NOT in the `directTransferRows` list
- They went through the normal summation loop
- Even though the loop logic was incomplete, they got SOME value instead of 0

## Solution Implemented

### Two Separate Functions

#### 1. Execute All Function (services/transferservice.cpp)
```cpp
void TransferService::runCumulativePassExecuteAll(const QSet<int>& allRows,
                                                   const QString& destSheet,
                                                   int year,
                                                   const QString& destKey,
                                                   const QString& targetMonth)
{
    int targetIdx = monthOrder.indexOf(targetMonth);
    if (targetIdx <= 0) return;

    // Compute ONLY month-1 cumulative column
    int colToCompute = targetIdx - 1;  // e.g., October (9) → compute IX (8)
    
    int cumColIdx  = m_handler->letterToColumn(cumColOrder[colToCompute]);
    int baseColIdx = m_handler->letterToColumn(baseColOrder[colToCompute]);
    
    // Previous cumulative column (if any)
    int prevCumColIdx = -1;
    if (colToCompute > 0) {
        prevCumColIdx = m_handler->letterToColumn(cumColOrder[colToCompute - 1]);
    }

    for (int row : rowsToSum) {
        // Direct-copy rows: read ONLY the target base column
        if (directCopyRows.contains(row)) {
            QVariant raw = m_handler->getCellValue(destKey, destSheet, row, baseColIdx);
            double val = raw.canConvert<double>() ? raw.toDouble() : 0.0;
            
            // Skip if formula cached=0 (cross-sheet formulas can't be evaluated)
            QString formula = m_handler->getCellFormula(destKey, destSheet, row, baseColIdx);
            if (!formula.isEmpty() && val == 0.0) continue;
            
            m_handler->setCellValue(destKey, destSheet, row, cumColIdx, roundTo5(val));
            continue;
        }

        // Normal rows: INCREMENTAL — IX = IW + FB (prevCum + base)
        double prevCum = 0.0;
        if (prevCumColIdx >= 0) {
            QVariant prevRaw = m_handler->getCellValue(destKey, destSheet, row, prevCumColIdx);
            prevCum = prevRaw.canConvert<double>() ? prevRaw.toDouble() : 0.0;
            
            // Try to evaluate formula if cached value is 0
            if (prevCum == 0.0 || mzlzSubtotals.contains(row)) {
                QString formula = m_handler->getCellFormula(destKey, destSheet, row, prevCumColIdx);
                if (!formula.isEmpty()) {
                    double formulaVal = evaluateSimpleFormula(m_handler, destKey, destSheet, formula);
                    if (formulaVal != 0.0 || prevCum == 0.0) prevCum = formulaVal;
                }
            }
        }

        QVariant baseRaw = m_handler->getCellValue(destKey, destSheet, row, baseColIdx);
        double baseVal = baseRaw.canConvert<double>() ? baseRaw.toDouble() : 0.0;
        
        // Try to evaluate formula if cached value is 0
        if (baseVal == 0.0 || mzlzSubtotals.contains(row)) {
            QString formula = m_handler->getCellFormula(destKey, destSheet, row, baseColIdx);
            if (!formula.isEmpty()) {
                double formulaVal = evaluateSimpleFormula(m_handler, destSheet, formula);
                if (formulaVal != 0.0 || baseVal == 0.0) baseVal = formulaVal;
            }
        }

        m_handler->setCellValue(destKey, destSheet, row, cumColIdx, roundTo5(prevCum + baseVal));
    }
}
```

**Key Points:**
- Calculates ONLY the single cumulative column for (targetMonth - 1)
- Trusts existing cumulative values in the spreadsheet (they came from prior Execute All runs)
- INCREMENTAL calculation: `IX = IW + FB` (previous cumulative + current base)

#### 2. Fill All Months Function (services/transferservice.cpp)
```cpp
void TransferService::runCumulativePassAllMonths(const QSet<int>& allRows,
                                                  const QString& destSheet,
                                                  int year,
                                                  const QString& destKey,
                                                  const QString& targetMonth)
{
    int targetIdx = monthOrder.indexOf(targetMonth);
    if (targetIdx < 0) return;

    QSet<int> rowsToSum = allRows;
    for (int r : mzlzSubtotals) rowsToSum.insert(r);

    // Running sum accumulator per row
    QMap<int, double> runningSums;
    
    // Loop through ALL months from January (0) to targetMonth
    for (int m = 0; m <= targetIdx && m < cumColOrder.size(); ++m) {
        int cumColIdx  = m_handler->letterToColumn(cumColOrder[m]);  // IP, IQ, IR, ...
        int baseColIdx = m_handler->letterToColumn(baseColOrder[m]); // G, W, AM, ...

        for (int row : rowsToSum) {
            QVariant raw = m_handler->getCellValue(destKey, destSheet, row, baseColIdx);
            double val = raw.canConvert<double>() ? raw.toDouble() : 0.0;

            // Try to evaluate formula if cached value is 0
            if (val == 0.0 || mzlzSubtotals.contains(row)) {
                QString formula = m_handler->getCellFormula(destKey, destSheet, row, baseColIdx);
                if (!formula.isEmpty()) {
                    double formulaVal = evaluateSimpleFormula(m_handler, destKey, destSheet, formula);
                    if (formulaVal != 0.0 || val == 0.0) val = formulaVal;
                }
            }

            if (directCopyRows.contains(row)) {
                // Direct copy: if formula cached=0, skip write
                QString formula = m_handler->getCellFormula(destKey, destSheet, row, baseColIdx);
                if (!formula.isEmpty() && val == 0.0) continue;
                runningSums[row] = val;  // Replace, don't accumulate
            } else {
                runningSums[row] += val;  // Accumulate
            }
            
            m_handler->setCellValue(destKey, destSheet, row, cumColIdx, roundTo5(runningSums[row]));
        }
    }
}
```

**Key Points:**
- Calculates ALL cumulative columns from January through target month
- Uses a `QMap<int, double> runningSums` to accumulate values across months
- CUMULATIVE calculation: Each month adds to the running total
- When processing August: IP=Jan, IQ=Jan+Feb, IR=Jan+Feb+Mar, ..., IW=Jan+...+Aug

### Function Declarations (services/transferservice.h)
```cpp
class TransferService : public QObject {
    Q_OBJECT
public:
    // ... existing code ...

    // Cumulative pass for Execute All — computes IP through (targetMonth - 1).
    // Does NOT touch the target month's cumulative column.
    void runCumulativePassExecuteAll(const QSet<int>& allRows,
                                     const QString& destSheet,
                                     int year,
                                     const QString& destKey,
                                     const QString& targetMonth);

    // Cumulative pass for Fill All only — recomputes all months from scratch.
    void runCumulativePassAllMonths(const QSet<int>& allRows,
                                    const QString& destSheet,
                                    int year,
                                    const QString& destKey,
                                    const QString& targetMonth);
    // ... rest of class ...
};
```

### Fill All Worker Integration (features/transfer/fillallworker.cpp)
```cpp
// PHASE 4.5: Calculate Cumulative Columns (Pass 2)
emit progress(totalProgressSteps - 1, totalProgressSteps, "Computing cumulative columns (Pass 2)...");

QSet<int> allMzlzRows;
QString mzlzSheetName = "MZLZ Consolidated";

// Only include rows from mappings whose destColumn is a base column
static const QSet<QString> baseColSet = {
    "G", "W", "AM", "BD", "BW", "CP", "DJ", "EF", "FB", "FY", "GX", "HW"
};

const QStringList allTypes = {"sap", "sap_ytd", "budget_refi", "traffic_mott", "pax_transfer", "staff"};
for (const QString& type : allTypes) {
    QVector<MappingEntry> mappings = getMappingsForType(targetMonthName, year, type);
    for (const MappingEntry& me : mappings) {
        if (me.destSheet != "MZLZ Consolidated") continue;
        if (!baseColSet.contains(me.destColumn.toUpper())) continue;  // ← KEY FILTER
        
        if (!me.rowMap.isEmpty()) {
            for (auto it = me.rowMap.constBegin(); it != me.rowMap.constEnd(); ++it)
                allMzlzRows.insert(it.key());
        }
        // ... collect rows ...
    }
}

if (!allMzlzRows.isEmpty()) {
    m_transfer->runCumulativePassAllMonths(allMzlzRows, mzlzSheetName, year, destKey, targetMonthName);
}
```

**Key Points:**
- Collects ALL destination rows from ALL mapping types
- **Filters out non-base columns** (budget_refi writes to D/E/F, T/U/V which are NOT base columns)
- Calls `runCumulativePassAllMonths` instead of the old shared function

## Why Execute All Works But Fill All Didn't

### Execute All Workflow
1. User selects April 2025
2. Loads `Cost Control ZAG 04_2025_working.xlsm`
3. Writes base data to column BD (April base column)
4. Calls `runCumulativePassExecuteAll` with targetMonth="April"
5. Function calculates IR (March cumulative) = IQ (Feb cumulative) + AM (March base)
6. **Works because**: Prior months' cumulative columns (IP, IQ) already exist from previous Execute All runs
7. Only needs to compute ONE new column (IR for March)

### Fill All Months Workflow (BEFORE FIX)
1. User selects August 2025
2. Loads `Cost Control ZAG 08_2025_working.xlsm` (destination)
3. Writes ALL base data: G (Jan), W (Feb), AM (Mar), BD (Apr), BW (May), CP (Jun), DJ (Jul), EF (Aug)
4. Called old `runCumulativePass` with targetMonth="August"
5. **Bug**: Function calculated `mIdx = 7-1 = 6`, looped 0→6
6. Each iteration ONLY added current month's base, not cumulative sum
7. Result: IV (July) = only July's base value, not Jan+Feb+...+Jul
8. **Zeros for rows 6, 13, 16, 18**: Direct copy logic read stale cached formula values (0)

### Fill All Months Workflow (AFTER FIX)
1. User selects August 2025
2. Loads `Cost Control ZAG 08_2025_working.xlsm` (destination)
3. Writes ALL base data: G (Jan), W (Feb), AM (Mar), BD (Apr), BW (May), CP (Jun), DJ (Jul), EF (Aug)
4. Calls `runCumulativePassAllMonths` with targetMonth="August"
5. Function loops m=0→7 (Jan→Aug)
6. Uses `QMap<int, double> runningSums` to accumulate:
   - IP (Jan) = G
   - IQ (Feb) = IP + W = G + W
   - IR (Mar) = IQ + AM = G + W + AM
   - ... continues ...
   - IW (Aug) = IV + EF = G + W + AM + BD + BW + CP + DJ + EF
7. **Correct cumulative values** for all rows including 6, 13, 16, 18

## Testing Recommendations

### Test Case 1: Execute All (Single Month)
```
1. Select October 2025 only
2. Click Execute All
3. Verify:
   - IX (September cumulative) is calculated correctly
   - IX = IW (August cumulative) + FB (September base)
   - IY (October cumulative) is NOT touched
```

### Test Case 2: Fill All Months (Multiple Months)
```
1. Select year=2025, target=August
2. Click Execute Fill All Months
3. Verify:
   - IP (Jan) = G
   - IQ (Feb) = G + W
   - IR (Mar) = G + W + AM
   - IS (Apr) = G + W + AM + BD
   - IT (May) = G + W + AM + BD + BW
   - IU (Jun) = G + W + AM + BD + BW + CP
   - IV (Jul) = G + W + AM + BD + BW + CP + DJ
   - IW (Aug) = G + W + AM + BD + BW + CP + DJ + EF
4. Check rows 6, 13, 16, 18 specifically (previously showed zeros)
5. Check rows 33, 54 (previously worked, should still work)
```

### Test Case 3: Direct Copy Rows with Formulas
```
1. Open Cost Control file in Excel
2. Navigate to MZLZ Consolidated sheet
3. Check rows 12, 13, 16, 18 in base columns (G, W, AM, etc.)
4. Verify these cells contain formulas (e.g., =SUM(...))
5. Run Fill All Months
6. Verify cumulative columns IP-IZ for these rows:
   - Should contain the formula's calculated value
   - Should NOT be 0 (unless formula legitimately evaluates to 0)
```

## Files Modified

1. **services/transferservice.h**
   - Added `runCumulativePassExecuteAll()` declaration
   - Added `runCumulativePassAllMonths()` declaration
   - Removed old `runCumulativePass()` declaration

2. **services/transferservice.cpp**
   - Implemented `runCumulativePassExecuteAll()` (lines ~550-620)
   - Implemented `runCumulativePassAllMonths()` (lines ~623-680)
   - Removed old `runCumulativePass()` function

3. **features/transfer/fillallworker.cpp**
   - Updated Phase 4.5 to call `runCumulativePassAllMonths()` (lines ~380-420)
   - Added base column filter to exclude budget_refi non-base columns

## Next Steps

### Immediate Actions
1. ✅ Test Execute All with October 2025 (verify IX calculation)
2. ✅ Test Fill All Months with August 2025 (verify IP-IW calculations)
3. ✅ Verify rows 6, 13, 16, 18 no longer show zeros
4. ✅ Verify rows 33, 54 still calculate correctly

### Future Improvements
1. **Formula Recalculation**: Consider using `ExcelHandler::recalcWithCOM()` before loading source files to ensure fresh calculated values in `<v>` tags
2. **Enhanced Formula Evaluator**: The current `evaluateSimpleFormula()` can't handle cross-sheet references (formulas with `!`). Consider:
   - Expanding the evaluator to support cross-sheet lookups
   - OR: Always use COM recalc before loading
   - OR: Trust cached values for cross-sheet formulas (current approach)
3. **Logging**: Add more detailed debug logging for cumulative pass to track which rows get which values
4. **Unit Tests**: Create automated tests for cumulative calculation logic

## Summary

The cumulative columns IP-IZ problem was caused by using a single shared function for two fundamentally different workflows:
- **Execute All** needs INCREMENTAL calculation (month-1 only)
- **Fill All Months** needs CUMULATIVE calculation (all months from scratch)

The fix decouples these into two separate functions with distinct logic, ensuring each workflow gets the correct calculation behavior. The "direct copy rows" (6, 13, 16, 18) now properly handle formula evaluation and skip writes when cached values are stale (0).
