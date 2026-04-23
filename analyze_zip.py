import zipfile, os, re

f = r'C:\Users\dposavac\CLionProjects\Excel_transfer\YTD_01_2026.xlsm'
with zipfile.ZipFile(f) as z:
    names = z.namelist()
    print('ZIP entries (%d total):' % len(names))
    for n in sorted(names):
        if 'sheet' in n.lower() or 'workbook' in n.lower() or 'content' in n.lower() or 'shared' in n.lower():
            print('  ', n, '(%d bytes)' % z.getinfo(n).file_size)
    
    # Find sheet1.xml
    sheet_path = None
    for n in names:
        if 'sheet1.xml' in n.lower():
            sheet_path = n
            break
    
    print()
    print('Sheet XML path:', sheet_path)
    
    if sheet_path:
        data = z.read(sheet_path).decode('utf-8', errors='replace')
        print('Sheet XML size:', len(data))
        
        # Find cells in column C around rows 9, 35, 466
        # Search for cell references like C9, C35, C466
        for target_row in [9, 35, 466, 587]:
            pattern = r'<c[^>]*r="C%d"[^>]*>.*?</c>' % target_row
            match = re.search(pattern, data, re.DOTALL)
            if match:
                print('  Cell C%d: %s' % (target_row, match.group()[:300]))
            else:
                # Try without quotes
                pattern2 = r'<c [^>]*r="C%d"' % target_row
                match2 = re.search(pattern2, data)
                if match2:
                    # Get surrounding context
                    start = max(0, match2.start() - 10)
                    end = min(len(data), match2.end() + 200)
                    print('  Cell C%d (partial): %s' % (target_row, data[start:end]))
                else:
                    print('  Cell C%d: NOT FOUND in XML!' % target_row)
        
        # Show first few <c> elements to see general format
        print()
        print('First 5 <c> elements:')
        cells = re.findall(r'<c[^>]*>.*?</c>', data[:10000], re.DOTALL)
        for c in cells[:5]:
            print('  ', c[:300])