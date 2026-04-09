# copyFullSheet Enhancement - Implementation Progress

## Goal
Enhance `copyFullSheet()` to preserve ALL sheet elements including charts, images, drawings, and styles.

## Current Status: Phase 1 Complete ✅

### Phase 1: Helper Functions Added
All helper function declarations and basic implementations have been added.

#### ✅ Completed Functions:

1. **`resolveRelPath()`** - Converts relative ZIP paths to absolute
   - Handles "../" navigation
   - Handles absolute paths starting with "/"

2. **`generateUniqueTarget()`** - Creates unique filenames to avoid collisions
   - Checks existing files in destination
   - Appends "_copy1", "_copy2", etc. as needed

3. **`copySubRels()`** - Recursively copies nested relationship files
   - Copies charts, drawings, images
   - Remaps relationship targets
   - Handles nested relationships

4. **`parseSharedStringTable()`** - Extracts strings from sharedStrings.xml
   - Parses `<si><t>` elements
   - Returns vector of strings

5. **`buildSharedStringTable()`** - Builds sharedStrings.xml from vector
   - Creates proper XML structure
   - HTML-escapes content

6. **`mergeSharedStrings()`** - Merges source shared strings into destination
   - Builds index remap (src → dst)
   - Rewrites cell references in sheet XML
   - Updates destination sharedStrings.xml

7. **`addContentType()`** - Registers new parts in [Content_Types].xml
   - Checks for duplicates
   - Adds Override entries

8. **`addWorkbookSheet()`** - Adds sheet entry to workbook.xml
   - Generates unique rId
   - Adds `<sheet>` element

9. **`addWorkbookRel()`** - Adds relationship to workbook.xml.rels
   - Creates worksheet relationship
   - Links rId to target path

#### ⏳ Placeholder Functions (To Implement):

10. **`mergeStyles()`** - Currently logs warning
11. **`parseCellXfs()`** - Returns empty vector
12. **`appendStyleSection()`** - Returns 0
13. **`remapXfIndices()`** - Returns input unchanged
14. **`rebuildStylesXml()`** - No-op

---

## Next Steps

### Phase 2: Implement Style Merging (Complex)
Style merging is the most complex part because styles.xml contains:
- `<numFmts>` - Number formats
- `<fonts>` - Font definitions
- `<fills>` - Fill patterns/colors
- `<borders>` - Border styles
- `<cellStyleXfs>` - Cell style formats
- `<cellXfs>` - Cell formats (referenced by cells via 's' attribute)

Each `<xf>` entry references indices from fonts, fills, borders, and numFmts.
When merging, we need to:
1. Copy all style components from source to destination
2. Remap indices in the copied `<xf>` entries
3. Remap 's' attributes in cell elements

**Estimated complexity:** High
**Estimated time:** 2-3 hours

### Phase 3: Rewrite copyFullSheet() to Use New Helpers
Once all helpers are complete, rewrite the main `copyFullSheet()` function to:
1. Copy raw sheet XML (no stripping)
2. Copy all relationships using `copySubRels()`
3. Merge shared strings using `mergeSharedStrings()`
4. Merge styles using `mergeStyles()`
5. Register sheet using `addWorkbookSheet()` and `addWorkbookRel()`
6. Add content type using `addContentType()`

**Estimated complexity:** Medium
**Estimated time:** 1 hour

### Phase 4: Testing
Test with sheets containing:
- Charts
- Images
- Conditional formatting
- Complex styles
- Formulas
- Data validation
- Comments

---

## Current Limitations

### What Works Now:
- ✅ Basic helper infrastructure in place
- ✅ Shared string merging fully implemented
- ✅ Relationship copying implemented
- ✅ Workbook registration implemented

### What Doesn't Work Yet:
- ❌ Style merging (placeholder only)
- ❌ Main `copyFullSheet()` still uses old implementation
- ❌ Charts/images still stripped in current implementation

---

## Testing Strategy

### Unit Tests Needed:
1. `resolveRelPath()` with various relative paths
2. `generateUniqueTarget()` with collisions
3. `parseSharedStringTable()` with real SST XML
4. `buildSharedStringTable()` round-trip test
5. `mergeSharedStrings()` with overlapping strings

### Integration Tests Needed:
1. Copy sheet with charts → verify charts appear
2. Copy sheet with images → verify images appear
3. Copy sheet with complex styles → verify styles preserved
4. Copy sheet with formulas → verify formulas work
5. Copy sheet twice → verify no collisions

---

## Risk Assessment

### Low Risk:
- Helper functions are isolated
- Old implementation still works
- Can be tested incrementally

### Medium Risk:
- Style merging is complex
- Index remapping must be perfect
- XML parsing must handle edge cases

### High Risk:
- Breaking existing `copyFullSheet()` functionality
- Corrupting workbook structure
- Performance impact on large sheets

### Mitigation:
- Keep old implementation as fallback
- Add feature flag to enable new implementation
- Extensive testing before deployment

---

## Feature Flag Approach

Add environment variable to control which implementation is used:

```cpp
bool ExcelHandler::copyFullSheet(const QString& srcKey, const QString& srcSheet,
                                 const QString& destKey, const QString& newSheetName,
                                 const QVector<int>& highlightRows)
{
    bool useEnhanced = qEnvironmentVariableIsSet("EXCEL_ENHANCED_COPY");
    
    if (useEnhanced) {
        return copyFullSheetEnhanced(srcKey, srcSheet, destKey, newSheetName, highlightRows);
    } else {
        return copyFullSheetLegacy(srcKey, srcSheet, destKey, newSheetName, highlightRows);
    }
}
```

This allows:
- Testing new implementation without breaking production
- Easy rollback if issues found
- Gradual migration

---

## Files Modified

### Phase 1:
- ✅ `services/excelhandler.h` - Added 15 helper function declarations
- ✅ `services/excelhandler.cpp` - Added ~400 lines of helper implementations

### Phase 2 (Pending):
- ⏳ `services/excelhandler.cpp` - Implement style merging helpers (~300 lines)

### Phase 3 (Pending):
- ⏳ `services/excelhandler.cpp` - Rewrite `copyFullSheet()` (~200 lines)

---

## Decision Point

Before proceeding to Phase 2 (style merging), we should decide:

1. **Do we need full style preservation?**
   - If sheets being copied don't have complex styles, we can skip this
   - If they do, we need full implementation

2. **Can we use a simpler approach?**
   - Copy entire styles.xml from source (may cause conflicts)
   - Only copy styles actually used by the sheet (requires parsing)

3. **What's the priority?**
   - Charts/images (Phase 3 without style merging)
   - Full fidelity (complete Phase 2 first)

**Recommendation:** Proceed to Phase 3 without style merging first, test with real sheets, then decide if style merging is needed based on results.

---

## Summary

✅ **Phase 1 Complete:** All helper functions added, basic infrastructure in place
⏳ **Phase 2 Pending:** Style merging (complex, can be deferred)
⏳ **Phase 3 Pending:** Rewrite main function (ready to start)
⏳ **Phase 4 Pending:** Testing and validation

**Next Action:** Implement Phase 3 (rewrite `copyFullSheet()`) to test chart/image preservation without style merging.
