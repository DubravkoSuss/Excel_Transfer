import json, zipfile, re, sys

with open('JSON/mappings_sap_ytd.json') as f:
    data = json.load(f)

# It's a list, first entry is January
jan = data[0]
rm = jan.get('rowMap', {})

z = zipfile.ZipFile('YTD_01_2026.xlsm', 'r')
xml = z.read('xl/worksheets/sheet1.xml').decode('utf-8', errors='replace')

cell_vals = {}
for m in re.finditer(r'<c r="C(\d+)"[^>]*>.*?<v>([^<]*)</v>', xml, re.DOTALL):
    cell_vals[int(m.group(1))] = m.group(2)

sys.stderr.write(f"Total C cells in XML: {len(cell_vals)}\n")
sys.stderr.write(f"rowMap entries: {len(rm)}\n")

print("Dest rows 35-102 -> Source rows -> Values in YTD_01_2026.xlsm:")
for dest_str in sorted(rm.keys(), key=lambda x: int(x)):
    dest = int(dest_str)
    if 35 <= dest <= 102:
        src = rm[dest_str]
        if isinstance(src, int):
            val = cell_vals.get(src, "NOT_FOUND")
            marker = " *** ZERO ***" if val == "0" else (" *** MISSING ***" if val == "NOT_FOUND" else "")
            print(f"  dest {dest:3d} -> src {src:5d} -> C={val}{marker}")
        elif isinstance(src, dict) and "sum" in src:
            parts = []
            for s in src["sum"]:
                v = cell_vals.get(abs(s), "NOT_FOUND")
                parts.append(f"{s}={v}")
            print(f"  dest {dest:3d} -> SUM({', '.join(parts)})")
        elif isinstance(src, list):
            parts = []
            for s in src:
                v = cell_vals.get(s, "NOT_FOUND")
                parts.append(f"{s}={v}")
            print(f"  dest {dest:3d} -> LIST({', '.join(parts)})")
        else:
            print(f"  dest {dest:3d} -> src={src} (unknown type)")