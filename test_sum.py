import openpyxl

wb = openpyxl.load_workbook(r"C:\Users\dposavac\Cost control\Cost Control\test\Cost Control ZAG 03_2025_working.xlsm", data_only=True)
sht = wb["MZLZ Consolidated"]

def get_val(cell):
    v = sht[cell].value
    return float(v) if v is not None else 0.0

iq172 = get_val("IQ172")
iq174 = get_val("IQ174")
iq210 = get_val("IQ210")
iq212 = get_val("IQ212")
iq214 = get_val("IQ214")

print(f"IQ172: {iq172}")
print(f"IQ174: {iq174}")
print(f"IQ210: {iq210}")
print(f"IQ212: {iq212}")
print(f"SUM of components: {iq172 + iq174 + iq210 + iq212}")
print(f"IQ214 Actual Value in File: {iq214}")

wb.close()
