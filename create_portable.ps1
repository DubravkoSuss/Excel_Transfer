# ============================================================================
# Simple Portable Package Creator
# Uses existing build output to create portable package
# ============================================================================

param(
    [string]$BuildFolder = "cmake-build-release",
    [string]$QtPath = "C:\Qt\6.8.0\msvc2022_64",
    [string]$OutputDir = "ExcelTransfer_Portable"
)

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Creating Portable Package" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Check for existing build
$exePath = "$BuildFolder\ExcelTransfer.exe"
if (-not (Test-Path $exePath)) {
    Write-Host "[ERROR] Executable not found at: $exePath" -ForegroundColor Red
    Write-Host ""
    Write-Host "Please build the project first using one of:" -ForegroundColor Yellow
    Write-Host "  - Visual Studio (Build → Build Solution)" -ForegroundColor Gray
    Write-Host "  - CMake (cmake --build cmake-build-release --config Release)" -ForegroundColor Gray
    Write-Host "  - Qt Creator (Build → Build Project)" -ForegroundColor Gray
    Write-Host ""
    Write-Host "Or specify a different build folder:" -ForegroundColor Yellow
    Write-Host "  .\create_portable.ps1 -BuildFolder 'build\Release'" -ForegroundColor Gray
    exit 1
}

Write-Host "[OK] Found executable: $exePath" -ForegroundColor Green
Write-Host ""

# Clean output folder
Write-Host "[1/4] Preparing output folder..." -ForegroundColor Yellow
if (Test-Path $OutputDir) {
    Remove-Item -Recurse -Force $OutputDir
}
New-Item -ItemType Directory -Path $OutputDir | Out-Null

# Copy executable
Write-Host "[2/4] Copying executable..." -ForegroundColor Yellow
Copy-Item $exePath "$OutputDir\" -Force

# Deploy Qt
Write-Host "[3/4] Deploying Qt dependencies..." -ForegroundColor Yellow
$windeployqt = "$QtPath\bin\windeployqt.exe"

if (-not (Test-Path $windeployqt)) {
    Write-Host "[WARN] windeployqt not found at: $windeployqt" -ForegroundColor Yellow
    Write-Host "Trying to find Qt automatically..." -ForegroundColor Yellow
    
    # Try to find Qt in common locations
    $qtLocations = @(
        "C:\Qt\6.8.0\msvc2022_64",
        "C:\Qt\6.7.0\msvc2022_64",
        "C:\Qt\6.6.0\msvc2022_64",
        "C:\Qt6\6.8.0\msvc2022_64"
    )
    
    foreach ($loc in $qtLocations) {
        $testPath = "$loc\bin\windeployqt.exe"
        if (Test-Path $testPath) {
            $windeployqt = $testPath
            $QtPath = $loc
            Write-Host "[OK] Found Qt at: $QtPath" -ForegroundColor Green
            break
        }
    }
    
    if (-not (Test-Path $windeployqt)) {
        Write-Host "[ERROR] Could not find windeployqt!" -ForegroundColor Red
        Write-Host "Please specify Qt path:" -ForegroundColor Yellow
        Write-Host "  .\create_portable.ps1 -QtPath 'C:\Qt\6.8.0\msvc2022_64'" -ForegroundColor Gray
        exit 1
    }
}

Push-Location $OutputDir
& $windeployqt "ExcelTransfer.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw

if ($LASTEXITCODE -ne 0) {
    Write-Host "[WARN] windeployqt reported warnings (this is usually OK)" -ForegroundColor Yellow
}
Pop-Location

# Copy additional files
Write-Host "[4/4] Copying additional files..." -ForegroundColor Yellow

if (Test-Path "JSON") {
    Write-Host "  → JSON folder" -ForegroundColor Gray
    Copy-Item "JSON" "$OutputDir\" -Recurse -Force
}

if (Test-Path "repair.ps1") {
    Write-Host "  → repair.ps1" -ForegroundColor Gray
    Copy-Item "repair.ps1" "$OutputDir\" -Force
}

# Create README
$readme = @"
Excel Transfer Tool - Portable Edition
=======================================

Build Date: $(Get-Date -Format "yyyy-MM-dd HH:mm")

QUICK START
-----------
1. Double-click ExcelTransfer.exe
2. No installation needed!

REQUIREMENTS
------------
- Windows 10/11 (64-bit)
- Microsoft Excel 2016+

FOLDER CONTENTS
---------------
ExcelTransfer.exe  - Main application
JSON/              - Configuration files
*.dll              - Required libraries
platforms/         - Qt plugins
styles/            - UI styles
(other folders)    - Additional plugins

TROUBLESHOOTING
---------------
If the app doesn't start:
1. Ensure Excel is installed
2. Check all DLL files are present
3. Run as administrator (if needed)
4. Check Windows Event Viewer for errors

© 2026 Finance Automation Team
"@

Set-Content -Path "$OutputDir\README.txt" -Value $readme -Encoding UTF8

# Create launcher
$launcher = @"
@echo off
REM Excel Transfer Launcher
cd /d "%~dp0"
if not exist "ExcelTransfer.exe" (
    echo ERROR: ExcelTransfer.exe not found!
    pause
    exit /b 1
)
start "" "ExcelTransfer.exe"
"@

Set-Content -Path "$OutputDir\Launch.bat" -Value $launcher -Encoding ASCII

# Summary
Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "[OK] Portable Package Created!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""

$folderSize = (Get-ChildItem $OutputDir -Recurse | Measure-Object -Property Length -Sum).Sum / 1MB
Write-Host "Location: $OutputDir" -ForegroundColor Cyan
Write-Host "Size: $([math]::Round($folderSize, 2)) MB" -ForegroundColor Cyan
Write-Host ""

Write-Host "Files included:" -ForegroundColor Yellow
$fileCount = (Get-ChildItem $OutputDir -Recurse -File).Count
$folderCount = (Get-ChildItem $OutputDir -Recurse -Directory).Count
Write-Host "  $fileCount files in $folderCount folders" -ForegroundColor Gray
Write-Host ""

# Offer to create ZIP
$createZip = Read-Host "Create ZIP archive for distribution? (y/n)"
if ($createZip -eq "y" -or $createZip -eq "Y") {
    Write-Host ""
    Write-Host "Creating ZIP archive..." -ForegroundColor Yellow
    $zipPath = "$OutputDir.zip"
    
    if (Test-Path $zipPath) {
        Remove-Item $zipPath -Force
    }
    
    Compress-Archive -Path "$OutputDir\*" -DestinationPath $zipPath -CompressionLevel Optimal
    
    $zipSize = (Get-Item $zipPath).Length / 1MB
    Write-Host "[OK] ZIP created: $zipPath" -ForegroundColor Green
    Write-Host "   Size: $([math]::Round($zipSize, 2)) MB" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Ready to distribute!" -ForegroundColor Green
}

Write-Host ""
Write-Host "To test:" -ForegroundColor Yellow
Write-Host "  1. Copy '$OutputDir' folder to another location" -ForegroundColor Gray
Write-Host "  2. Run ExcelTransfer.exe" -ForegroundColor Gray
Write-Host "  3. Verify all features work" -ForegroundColor Gray
Write-Host ""
Write-Host "Done!" -ForegroundColor Green
