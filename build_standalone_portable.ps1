# ============================================================================
# Excel Transfer Tool - Standalone Portable Build Script
# Creates a fully portable executable with all dependencies bundled
# No installation required - just extract and run
# ============================================================================

param(
    [string]$BuildType = "Release",
    [string]$QtPath = "C:\Qt\6.8.0\msvc2022_64",
    [string]$OutputDir = "ExcelTransfer_Portable"
)

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Excel Transfer - Standalone Build" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# ── Step 1: Clean previous builds ──────────────────────────────────────────
Write-Host "[1/7] Cleaning previous builds..." -ForegroundColor Yellow
if (Test-Path "build-standalone") {
    Remove-Item -Recurse -Force "build-standalone"
}
if (Test-Path $OutputDir) {
    Remove-Item -Recurse -Force $OutputDir
}
New-Item -ItemType Directory -Path "build-standalone" | Out-Null
New-Item -ItemType Directory -Path $OutputDir | Out-Null

# ── Step 2: Configure CMake ────────────────────────────────────────────────
Write-Host "[2/7] Configuring CMake..." -ForegroundColor Yellow
$env:PATH = "$QtPath\bin;$env:PATH"

Push-Location "build-standalone"
cmake .. -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_PREFIX_PATH="$QtPath" `
    -DCMAKE_BUILD_TYPE=$BuildType `
    -DCMAKE_INSTALL_PREFIX="../$OutputDir"

if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ CMake configuration failed!" -ForegroundColor Red
    Pop-Location
    exit 1
}

# ── Step 3: Build the project ──────────────────────────────────────────────
Write-Host "[3/7] Building project ($BuildType)..." -ForegroundColor Yellow
cmake --build . --config $BuildType --parallel

if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ Build failed!" -ForegroundColor Red
    Pop-Location
    exit 1
}

Pop-Location

# ── Step 4: Copy executable ────────────────────────────────────────────────
Write-Host "[4/7] Copying executable..." -ForegroundColor Yellow
$exePath = "build-standalone\$BuildType\ExcelTransfer.exe"
if (-not (Test-Path $exePath)) {
    Write-Host "❌ Executable not found at: $exePath" -ForegroundColor Red
    exit 1
}
Copy-Item $exePath "$OutputDir\" -Force

# ── Step 5: Deploy Qt dependencies ─────────────────────────────────────────
Write-Host "[5/7] Deploying Qt dependencies..." -ForegroundColor Yellow
$windeployqt = "$QtPath\bin\windeployqt.exe"
if (-not (Test-Path $windeployqt)) {
    Write-Host "❌ windeployqt not found at: $windeployqt" -ForegroundColor Red
    exit 1
}

Push-Location $OutputDir
& $windeployqt "ExcelTransfer.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw
Pop-Location

# ── Step 6: Copy additional runtime files ──────────────────────────────────
Write-Host "[6/7] Copying additional runtime files..." -ForegroundColor Yellow

# Copy Visual C++ Runtime (if needed)
$vcRedistPath = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC"
if (Test-Path $vcRedistPath) {
    $latestVcRedist = Get-ChildItem $vcRedistPath | Sort-Object Name -Descending | Select-Object -First 1
    $vcRuntimePath = "$($latestVcRedist.FullName)\x64\Microsoft.VC143.CRT"
    if (Test-Path $vcRuntimePath) {
        Write-Host "  → Copying VC++ Runtime DLLs..." -ForegroundColor Gray
        Copy-Item "$vcRuntimePath\*.dll" "$OutputDir\" -Force -ErrorAction SilentlyContinue
    }
}

# Copy JSON folder if it exists
if (Test-Path "JSON") {
    Write-Host "  → Copying JSON configuration files..." -ForegroundColor Gray
    Copy-Item "JSON" "$OutputDir\" -Recurse -Force
}

# Copy repair script if it exists
if (Test-Path "repair.ps1") {
    Write-Host "  → Copying repair script..." -ForegroundColor Gray
    Copy-Item "repair.ps1" "$OutputDir\" -Force
}

# ── Step 7: Create README ───────────────────────────────────────────────────
Write-Host "[7/7] Creating README..." -ForegroundColor Yellow
$readmeContent = @"
Excel Transfer Tool - Portable Edition
=======================================

Version: 1.0
Build Date: $(Get-Date -Format "yyyy-MM-dd HH:mm")
Build Type: $BuildType

INSTALLATION
------------
No installation required! This is a fully portable application.

1. Extract this folder to any location on your computer
2. Double-click ExcelTransfer.exe to run
3. All settings and data are stored in this folder

REQUIREMENTS
------------
- Windows 10 or Windows 11 (64-bit)
- Microsoft Excel 2016 or later
- .NET Framework 4.8 or later (usually pre-installed)

FOLDER STRUCTURE
----------------
ExcelTransfer.exe       - Main application
JSON/                   - Mapping configuration files
*.dll                   - Required runtime libraries
platforms/              - Qt platform plugins
styles/                 - Qt style plugins
imageformats/           - Image format plugins
iconengines/            - Icon engine plugins
networkinformation/     - Network plugins
tls/                    - TLS/SSL plugins

USAGE
-----
1. Launch ExcelTransfer.exe
2. Select the Monthly Transfer, Fill All Months, or Individual Transfer tab
3. Load your Excel files
4. Configure mappings as needed
5. Click Execute to run the transfer

TROUBLESHOOTING
---------------
If the application fails to start:
1. Ensure all DLL files are in the same folder as ExcelTransfer.exe
2. Check that Excel is installed and working
3. Run repair.ps1 (if available) to fix common issues
4. Check Windows Event Viewer for error details

For more help, see the user manual or contact support.

NOTES
-----
- This portable version can be run from a USB drive
- No registry modifications are made
- All configuration is stored locally
- Safe to run multiple instances from different folders

© 2026 Finance Automation Team
"@

Set-Content -Path "$OutputDir\README.txt" -Value $readmeContent -Encoding UTF8

# ── Step 8: Create launcher script (optional) ──────────────────────────────
$launcherContent = @"
@echo off
REM Excel Transfer Tool Launcher
REM Ensures proper working directory and error handling

cd /d "%~dp0"
echo Starting Excel Transfer Tool...
echo.

if not exist "ExcelTransfer.exe" (
    echo ERROR: ExcelTransfer.exe not found!
    echo Please ensure all files are extracted properly.
    pause
    exit /b 1
)

start "" "ExcelTransfer.exe"

if errorlevel 1 (
    echo.
    echo ERROR: Failed to start application!
    echo Check that all DLL files are present.
    pause
    exit /b 1
)
"@

Set-Content -Path "$OutputDir\Launch.bat" -Value $launcherContent -Encoding ASCII

# ── Final Summary ───────────────────────────────────────────────────────────
Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "✅ Build Complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "Output folder: $OutputDir" -ForegroundColor Cyan
Write-Host ""

# Calculate folder size
$folderSize = (Get-ChildItem $OutputDir -Recurse | Measure-Object -Property Length -Sum).Sum / 1MB
Write-Host "Total size: $([math]::Round($folderSize, 2)) MB" -ForegroundColor Cyan

# List main files
Write-Host ""
Write-Host "Main files:" -ForegroundColor Yellow
Get-ChildItem $OutputDir -File | Select-Object Name, @{Name="Size (KB)";Expression={[math]::Round($_.Length/1KB, 2)}} | Format-Table -AutoSize

Write-Host ""
Write-Host "To distribute:" -ForegroundColor Yellow
Write-Host "  1. Zip the '$OutputDir' folder" -ForegroundColor Gray
Write-Host "  2. Share the zip file with users" -ForegroundColor Gray
Write-Host "  3. Users extract and run ExcelTransfer.exe" -ForegroundColor Gray
Write-Host ""

# Optional: Create ZIP archive
$createZip = Read-Host "Create ZIP archive? (y/n)"
if ($createZip -eq "y" -or $createZip -eq "Y") {
    Write-Host ""
    Write-Host "Creating ZIP archive..." -ForegroundColor Yellow
    $zipPath = "$OutputDir.zip"
    if (Test-Path $zipPath) {
        Remove-Item $zipPath -Force
    }
    Compress-Archive -Path "$OutputDir\*" -DestinationPath $zipPath -CompressionLevel Optimal
    Write-Host "✅ ZIP created: $zipPath" -ForegroundColor Green
    
    $zipSize = (Get-Item $zipPath).Length / 1MB
    Write-Host "ZIP size: $([math]::Round($zipSize, 2)) MB" -ForegroundColor Cyan
}

Write-Host ""
Write-Host "Done! 🎉" -ForegroundColor Green
