# Building Excel Transfer Tool - Standalone Portable Version

## Prerequisites

1. **Visual Studio 2022** (Community Edition or higher)
   - Install "Desktop development with C++" workload
   - Install "C++ CMake tools for Windows"

2. **Qt 6.8.0** (or compatible version)
   - Download from: https://www.qt.io/download-qt-installer
   - Install MSVC 2022 64-bit component
   - Default path: `C:\Qt\6.8.0\msvc2022_64`

3. **CMake 3.16+** (usually included with Visual Studio)

## Quick Build

### Option 1: Automated Build (Recommended)

1. Open PowerShell in the project root directory
2. Run the build script:
   ```powershell
   .\build_standalone_portable.ps1
   ```

3. Follow the prompts:
   - Script will build the Release version
   - Deploy all Qt dependencies
   - Create portable folder with all files
   - Optionally create a ZIP archive

4. Output will be in `ExcelTransfer_Portable\` folder

### Option 2: Custom Build

If your Qt installation is in a different location:

```powershell
.\build_standalone_portable.ps1 -QtPath "C:\Qt\6.7.0\msvc2022_64" -BuildType Release
```

Parameters:
- `-QtPath`: Path to Qt MSVC installation
- `-BuildType`: `Release` or `Debug`
- `-OutputDir`: Custom output folder name (default: `ExcelTransfer_Portable`)

### Option 3: Manual Build

1. **Configure CMake:**
   ```powershell
   mkdir build-standalone
   cd build-standalone
   cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:\Qt\6.8.0\msvc2022_64"
   ```

2. **Build:**
   ```powershell
   cmake --build . --config Release --parallel
   ```

3. **Deploy Qt:**
   ```powershell
   cd Release
   C:\Qt\6.8.0\msvc2022_64\bin\windeployqt.exe ExcelTransfer.exe --release
   ```

4. **Copy JSON folder** (if exists):
   ```powershell
   Copy-Item ..\..\JSON . -Recurse
   ```

## What Gets Included

The standalone build includes:

### Core Files
- `ExcelTransfer.exe` - Main application (no console window)
- `README.txt` - User instructions
- `Launch.bat` - Optional launcher script

### Qt Runtime Libraries
- `Qt6Core.dll`
- `Qt6Gui.dll`
- `Qt6Widgets.dll`
- `Qt6Concurrent.dll`
- `Qt6AxContainer.dll` (for Excel COM automation)
- Additional Qt DLLs as needed

### Qt Plugins
- `platforms/qwindows.dll` - Windows platform plugin
- `styles/qmodernwindowsstyle.dll` - Modern Windows style
- `imageformats/*.dll` - Image format support
- `iconengines/*.dll` - Icon rendering
- `networkinformation/*.dll` - Network info
- `tls/*.dll` - SSL/TLS support

### Visual C++ Runtime
- `msvcp140.dll`
- `vcruntime140.dll`
- `vcruntime140_1.dll`

### Configuration
- `JSON/` folder - Mapping configuration files
  - `mappings.json`
  - `mappings_sap_ytd.json`
  - `mappings_budget_refi_prev_year.json`
  - `Traffic_mott.json`
  - `pax.json`
  - `staff.json`

## Distribution

### Creating a Distributable Package

1. **Run the build script** and choose "Yes" when asked to create ZIP
2. The script creates `ExcelTransfer_Portable.zip`
3. Share this ZIP file with users

### User Installation

Users simply:
1. Extract the ZIP to any folder
2. Double-click `ExcelTransfer.exe` or `Launch.bat`
3. No installation, no admin rights needed

### Portable Features

✅ **No installation required** - Extract and run  
✅ **No registry modifications** - Completely portable  
✅ **USB drive compatible** - Run from removable media  
✅ **Multiple instances** - Can run from different folders  
✅ **Self-contained** - All dependencies included  
✅ **No admin rights** - Runs as standard user  

## Troubleshooting

### Build Errors

**"Qt6 not found"**
- Check Qt installation path
- Verify MSVC 2022 64-bit component is installed
- Update `-DCMAKE_PREFIX_PATH` in script

**"Visual Studio not found"**
- Install Visual Studio 2022 with C++ workload
- Run script from "Developer PowerShell for VS 2022"

**"windeployqt failed"**
- Ensure Qt bin folder is in PATH
- Check Qt installation is complete
- Try running windeployqt manually

### Runtime Errors

**"Application failed to start"**
- Ensure all DLLs are in the same folder as .exe
- Check Windows Event Viewer for details
- Verify Visual C++ Runtime is included

**"Qt platform plugin not found"**
- Ensure `platforms/` folder exists
- Check `qwindows.dll` is present
- Re-run windeployqt

**"Excel COM errors"**
- Ensure Excel is installed
- Check `Qt6AxContainer.dll` is present
- Verify Excel version compatibility

## Build Output Structure

```
ExcelTransfer_Portable/
├── ExcelTransfer.exe          # Main application
├── README.txt                 # User guide
├── Launch.bat                 # Launcher script
├── Qt6Core.dll               # Qt runtime
├── Qt6Gui.dll
├── Qt6Widgets.dll
├── Qt6Concurrent.dll
├── Qt6AxContainer.dll
├── msvcp140.dll              # VC++ runtime
├── vcruntime140.dll
├── platforms/
│   └── qwindows.dll          # Windows platform
├── styles/
│   └── qmodernwindowsstyle.dll
├── imageformats/
│   ├── qgif.dll
│   ├── qjpeg.dll
│   └── ...
├── iconengines/
│   └── qsvgicon.dll
├── networkinformation/
│   └── ...
├── tls/
│   └── ...
└── JSON/
    ├── mappings.json
    ├── mappings_sap_ytd.json
    └── ...
```

## Size Expectations

- **Uncompressed**: ~80-120 MB
- **ZIP compressed**: ~30-40 MB
- **Executable only**: ~2-5 MB

## Testing the Build

1. **Copy to a clean folder** (simulate user environment)
2. **Run ExcelTransfer.exe**
3. **Test all features**:
   - Load Excel files
   - Execute transfers
   - Check all tabs work
   - Verify COM automation
4. **Test on a different PC** (without Qt/VS installed)

## Continuous Integration

For automated builds, you can integrate this into CI/CD:

```yaml
# Example GitHub Actions workflow
- name: Build Standalone
  run: |
    .\build_standalone_portable.ps1 -QtPath "C:\Qt\6.8.0\msvc2022_64"
    
- name: Upload Artifact
  uses: actions/upload-artifact@v3
  with:
    name: ExcelTransfer-Portable
    path: ExcelTransfer_Portable.zip
```

## Version Management

Update version in:
1. `ui/mainwindow.cpp` - Window title
2. `README.txt` - Generated by build script
3. `app_icon.rc` - Resource file (if exists)

## Support

For build issues:
1. Check this guide
2. Review build logs
3. Test on clean Windows 10/11 VM
4. Contact development team

---

**Last Updated**: 2026-04-22  
**Build Script Version**: 1.0  
**Tested With**: Qt 6.8.0, Visual Studio 2022, Windows 11
