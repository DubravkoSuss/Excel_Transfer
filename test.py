def run():
    xml = b'<sheetData><row r="1" /><row r="2"><c r="A2"/></row></sheetData>'
    output = bytearray(xml)
    rowTagPos = output.find(b'<row r="1"')
    rowTagEnd = output.find(b'>', rowTagPos)
    rowClose = output.find(b'</row>', rowTagEnd)
    print("rowTagEnd:", rowTagEnd, output[rowTagEnd-1:rowTagEnd+1])
    print("rowClose:", rowClose)

if __name__ == "__main__":
    run()
