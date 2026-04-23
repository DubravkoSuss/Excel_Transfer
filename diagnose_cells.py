#!/usr/bin/env python3
"""Diagnose why certain labels can't be found in xlsx files.
Usage: python diagnose_cells.py <source.xlsx> <dest.xlsx>
"""
import sys
import zipfile
import xml.etree.ElementTree as ET
import re

def load_shared_strings(zf):
    """Parse shared strings from xlsx."""
    try:
        data = zf.read('xl/sharedStrings.xml')
    except KeyError:
        return []
    
    root = ET.fromstring(data)
    ns = {'s': 'http://schemas.openxmlformats.org/spreadsheetml/2006/main'}
    strings = []
    for si in root.findall('.//s:si', ns):
        # Concatenate all <t> elements (handles rich text)
        text = ''
        for t in si.iter('{http://schemas.openxmlformats.org/spreadsheetml/2006/main}t'):
            if t.text:
                text += t.text
        strings.append(text)
    return strings

def find_sheet_path(zf, sheet_name):
    """Find the xl/worksheets/sheetN.xml path for a given sheet name."""
    wb_data = zf.read('xl/workbook.xml')
    wb_root = ET.fromstring(wb_data)
    ns = {'s': 'http://schemas.openxmlformats.org/spreadsheetml/2006/main',
          'r': 'http://schemas.openxmlformats.org/officeDocument/2006/relationships'}
    
    # Find sheet rId
    rid = None
    for sheet in wb_root.findall('.//s:sheet', ns):
        if sheet.get('name') == sheet_name:
            rid = sheet.get('{http://schemas.openxmlformats.org/officeDocument/2006/relationships}id')
            break
    
    if not rid:
        print(f"  Sheet '{sheet_name}' not found in workbook!")
        return None
    
    # Resolve rId to file path
    rels_data = zf.read('xl/_rels/workbook.xml.rels')
    rels_root = ET.fromstring(rels_data)
    for rel in rels_root:
        if rel.get('Id') == rid:
            target = rel.get('Target')
            if not target.startswith('xl/'):
                target = 'xl/' + target
            return target
    return None

def col_letter_to_num(col):
    """Convert column letter to 1-based number."""
    num = 0
    for c in col.upper():
        num = num * 26 + (ord(c) - ord('A') + 1)
    return num

def parse_cell_ref(ref):
    """Parse 'B825' into ('B', 825)."""
    m = re.match(r'^([A-Za-z]+)(\d+)$', ref)
    if m:
        return m.group(1).upper(), int(m.group(2))
    return None, None

def scan_sheet_for_labels(zf, sheet_path, shared_strings, target_labels, label_cols=['A', 'B', 'C']):
    """Scan a sheet for specific labels, reporting what's in each cell."""
    data = zf.read(sheet_path)
    root = ET.fromstring(data)
    ns = {'s': 'http://schemas.openxmlformats.org/spreadsheetml/2006/main'}
    
    # Normalize targets for comparison
    def normalize(text):
        return re.sub(r'[^a-z0-9]', '', text.lower())
    
    target_norms = {normalize(lbl): lbl for lbl in target_labels}
    
    # Scan all cells
    found = {}
    col_nums = [col_letter_to_num(c) for c in label_cols]
    
    for row_el in root.findall('.//s:sheetData/s:row', ns):
        for cell in row_el.findall('s:c', ns):
            ref = cell.get('r', '')
            col_letter, row_num = parse_cell_ref(ref)
            if not col_letter:
                continue
            
            col_num = col_letter_to_num(col_letter)
            if col_num not in col_nums:
                continue
            
            cell_type = cell.get('t', '')
            v_el = cell.find('s:v', ns)
            f_el = cell.find('s:f', ns)
            is_el = cell.find('s:is', ns)
            
            # Get cell value
            value = ''
            if cell_type == 's' and v_el is not None and v_el.text:
                idx = int(v_el.text)
                if 0 <= idx < len(shared_strings):
                    value = shared_strings[idx]
            elif cell_type == 'str' and v_el is not None and v_el.text:
                value = v_el.text
            elif cell_type == 'inlineStr' and is_el is not None:
                for t in is_el.iter('{http://schemas.openxmlformats.org/spreadsheetml/2006/main}t'):
                    if t.text:
                        value += t.text
            elif v_el is not None and v_el.text:
                value = v_el.text
            
            norm = normalize(value)
            if norm in target_norms:
                lbl = target_norms[norm]
                if lbl not in found:
                    found[lbl] = []
                formula_text = f_el.text if f_el is not None else None
                found[lbl].append({
                    'ref': ref,
                    'row': row_num,
                    'col': col_letter,
                    'type': cell_type,
                    'value': value,
                    'has_formula': f_el is not None,
                    'formula': formula_text,
                    'has_v': v_el is not None
                })
    
    return found

def main():
    if len(sys.argv) < 3:
        print("Usage: python diagnose_cells.py <source.xlsx> <dest.xlsx>")
        sys.exit(1)
    
    src_path = sys.argv[1]
    dst_path = sys.argv[2]
    
    # Labels that are NOT FOUND
    target_labels = [
        "751180  GH Supporting staff",
        "GH IT services",
        "GH Supporting staff",
        "751184  GH Insurance",
        "GH Insurance",
        "751188  GH School center",
        "GH School center",
        "751190  GH Other revenues",
        "GH Other revenues",
        "751183  GH Security services",
        "GH Security services",
        "751187  GH Rent",
        "GH Rent",
        "Income from sale of MZUS",
        "Write off(Concession fee COVID-19)",
        "IAS 11 REVENUES",
        "IAS 11 COSTS",
        "IAS  11 COSTS",
        "449605  GH Auxiliary fireman",
        "GH Auxiliary fireman",
        "449607  GH Boarding Pass Control L2 NPT",
        "449608  GH Queueing L3 NPT",
        "Amortisation of Phase 1a intangible",
        "Refinancing gain income IFRS9",
        "Contracted health services (Dom zdravlja)",
        "Professional education",
        "Firefighters (DVD Hrašče, Securitas)",
        "Utility charges for land use",
        "Water management contrib.",
        "410700  Rent a car transport",
        "419810  Inspection costs",
        "419820  Customs costs",
        "446000  company tax",
        "449800  Other fees",
    ]
    
    print("=" * 70)
    print("DIAGNOSING SOURCE FILE:", src_path)
    print("=" * 70)
    
    with zipfile.ZipFile(src_path, 'r') as zf:
        ss = load_shared_strings(zf)
        print(f"Shared strings count: {len(ss)}")
        
        # Check if target labels exist in shared strings
        def normalize(text):
            return re.sub(r'[^a-z0-9]', '', text.lower())
        
        ss_norms = {}
        for i, s in enumerate(ss):
            n = normalize(s)
            if n not in ss_norms:
                ss_norms[n] = (i, s)
        
        print("\n--- Shared string matches ---")
        for lbl in target_labels:
            n = normalize(lbl)
            if n in ss_norms:
                idx, actual = ss_norms[n]
                print(f"  SST[{idx}] = {repr(actual)[:60]}  (matches '{lbl[:40]}')")
        
        sheet_path = find_sheet_path(zf, 'Report')
        if sheet_path:
            print(f"\nSheet path: {sheet_path}")
            found = scan_sheet_for_labels(zf, sheet_path, ss, target_labels, ['A', 'B', 'C'])
            print(f"\n--- Cells found in source (Report) ---")
            if not found:
                print("  NONE found!")
            for lbl, cells in sorted(found.items()):
                for c in cells:
                    print(f"  '{lbl[:45]}' -> {c['ref']} type={c['type']} "
                          f"has_v={c['has_v']} formula={c['has_formula']} "
                          f"val={repr(c['value'][:50])}")
            
            not_found = [lbl for lbl in target_labels if lbl not in found]
            print(f"\n--- NOT found in source cells ---")
            for lbl in not_found:
                print(f"  '{lbl}'")
    
    print("\n" + "=" * 70)
    print("DIAGNOSING DEST FILE:", dst_path)
    print("=" * 70)
    
    with zipfile.ZipFile(dst_path, 'r') as zf:
        ss = load_shared_strings(zf)
        print(f"Shared strings count: {len(ss)}")
        
        ss_norms = {}
        for i, s in enumerate(ss):
            n = normalize(s)
            if n not in ss_norms:
                ss_norms[n] = (i, s)
        
        print("\n--- Shared string matches ---")
        for lbl in target_labels:
            n = normalize(lbl)
            if n in ss_norms:
                idx, actual = ss_norms[n]
                print(f"  SST[{idx}] = {repr(actual)[:60]}  (matches '{lbl[:40]}')")
        
        sheet_path = find_sheet_path(zf, 'MZLZ Consolidated')
        if sheet_path:
            print(f"\nSheet path: {sheet_path}")
            found = scan_sheet_for_labels(zf, sheet_path, ss, target_labels, ['A', 'B', 'C'])
            print(f"\n--- Cells found in dest (MZLZ Consolidated) ---")
            if not found:
                print("  NONE found!")
            for lbl, cells in sorted(found.items()):
                for c in cells:
                    print(f"  '{lbl[:45]}' -> {c['ref']} type={c['type']} "
                          f"has_v={c['has_v']} formula={c['has_formula']} "
                          f"val={repr(c['value'][:50])}")
            
            not_found = [lbl for lbl in target_labels if lbl not in found]
            print(f"\n--- NOT found in dest cells ---")
            for lbl in not_found:
                print(f"  '{lbl}'")

if __name__ == '__main__':
    main()