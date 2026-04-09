# Copy Full Sheet - Testing Guide

## Quick Test Scenarios

### Test 1: Verify No XML Repair Errors (Overwrite Protection)
**Objective**: Ensure no duplicate sheet entries in workbook.xml

**Steps**:
1. Open the application
2. Go to "Fill All Months" tab
3. Select a mapping with "Copy full sheet" enabled
4. Run the transfer once
5. Run the same transfer again (overwriting the same sheet)
6. Open the destination Excel file

**Expected Result**:
- ✅ No XML repair prompt from Excel
- ✅ Sheet is properly overwritten with new content
- ✅ No duplicate sheet tabs in Excel

**What Was Fixed**:
- Previously: Duplicate `<sheet>` entries in workbook.xml caused corruption
- Now: Detects existing sheets and overwrites without re-registering

---

### Test 2: Verify Style Preservation (Colors, Fonts, Borders)
**Objective**: Ensure all formatting is copied from source to destination

**Steps**:
1. Create a source Excel file with:
   - Colored cells (background colors)
   - Custom fonts (bold, italic, different sizes)
   - Borders (thick, thin, colored)
   - Number formats (currency, percentages)
2. Use "Copy full sheet" to copy this sheet to destination
3. Open destination file and compare formatting

**Expected Result**:
- ✅ All cell background colors match exactly
- ✅ Font styles (bold, italic, size, color) preserved
- ✅ All borders appear correctly
- ✅ Number formats maintained

**What Was Fixed**:
- Previously: All formatting was lost (plain text only)
- Now: Full style merging preserves all formatting

---

### Test 3: Multiple Sheet Copies with Different Styles
**Objective**: Ensure no style conflicts when copying multiple sheets

**Steps**:
1. Create 3 source sheets with different color schemes:
   - Sheet1: Blue theme (blue backgrounds, blue borders)
   - Sheet2: Green theme (green backgrounds, green borders)
   - Sheet3: Red theme (red backgrounds, red borders)
2. Copy all 3 sheets to the same destination workbook
3. Verify each sheet maintains its unique styling

**Expected Result**:
- ✅ Sheet1 remains blue-themed
- ✅ Sheet2 remains green-themed
- ✅ Sheet3 remains red-themed
- ✅ No color bleeding between sheets
- ✅ No style conflicts

**What Was Fixed**:
- Previously: Styles would conflict or be lost
- Now: Each sheet's styles are independently preserved

---

### Test 4: Large Workbook Stress Test
**Objective**: Verify performance and correctness with many styles

**Steps**:
1. Use a source workbook with 50+ different cell styles
2. Copy a sheet from this workbook
3. Verify all styles are preserved
4. Check performance (should complete in < 5 seconds)

**Expected Result**:
- ✅ All 50+ styles preserved correctly
- ✅ No performance degradation
- ✅ No memory issues
- ✅ Destination file opens without errors

---

## Debugging Tips

### If XML Repair Error Still Occurs:
1. Check the log for: `[copyFullSheet] Overwriting existing sheet:`
   - If you see this, the detection is working
   - If not, the sheet name might not match exactly (case-sensitive)

2. Verify `isNewSheet` flag:
   - Should be `false` when overwriting
   - Should be `true` when creating new sheet

### If Styles Are Not Preserved:
1. Check the log for: `[copyFullSheet] Styles merged securely. Added X XFs.`
   - If you see this, style merging is working
   - If not, check if source has `xl/styles.xml`

2. Verify style counts:
   - Source XF count should be > 0
   - Destination should show increased XF count after merge

3. Check cell style references:
   - Look for `s="X"` attributes in sheet XML
   - Values should be offset by destination's original XF count

### Common Issues:

**Issue**: Sheet appears blank after copy
- **Cause**: Shared strings not merged properly
- **Check**: Look for `mergeSharedStrings` log messages

**Issue**: Colors are wrong but present
- **Cause**: Style index offset calculation error
- **Check**: Verify `xfOffset` value in logs

**Issue**: Excel crashes when opening file
- **Cause**: Corrupted XML structure
- **Check**: Validate XML with external tool before opening in Excel

---

## Log Messages to Watch For

### Success Indicators:
```
[copyFullSheet] Overwriting existing sheet: SheetName at xl/worksheets/sheet3.xml
[copyFullSheet] Styles merged securely. Added 25 XFs.
[copyFullSheet] SUCCESS: copied SourceSheet as DestSheet (45678 bytes, with relationships)
```

### Warning Indicators:
```
[copyFullSheet] Missing workbook: srcKey or destKey
[copyFullSheet] Source sheet not found: SheetName
[copyFullSheet] Required ZIP cache unavailable
```

---

## Performance Benchmarks

Expected performance on typical hardware:

| Operation | Time | Notes |
|-----------|------|-------|
| Copy simple sheet (no styles) | < 1s | Baseline |
| Copy sheet with 50 styles | < 2s | Style merging overhead |
| Copy sheet with 200 styles | < 3s | Still acceptable |
| Copy sheet with relationships | < 4s | Includes charts, images |

If times exceed these by 2x, investigate:
- Network file access (use local files for testing)
- Large embedded objects (charts, images)
- Excessive style count (> 500 unique styles)

---

## Validation Checklist

Before marking as "Fixed":
- [ ] Test 1 passed (no XML repair errors)
- [ ] Test 2 passed (styles preserved)
- [ ] Test 3 passed (multiple sheets, no conflicts)
- [ ] Test 4 passed (large workbook stress test)
- [ ] No compilation errors
- [ ] No memory leaks (check with profiler)
- [ ] Log messages appear as expected
- [ ] Performance within acceptable range

---

## Rollback Plan

If issues are discovered in production:

1. **Immediate**: Disable "Copy full sheet" feature in UI
2. **Short-term**: Revert to previous version of `excelhandler.cpp`
3. **Long-term**: Fix issues and re-test thoroughly

Backup of original code is in git history:
```bash
git log --oneline services/excelhandler.cpp
git show <commit-hash>:services/excelhandler.cpp
```
