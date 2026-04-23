# ============================================================================
# Excel Transfer Tool - Standalone Portable Build Script (Ninja)
# Faster build using Ninja generator
# ============================================================================

param(
    [string]$BuildType = "Release",
    [string]$QtPath = "C:\Qt\6.8.0\msvc2022_64",
    [string]$OutputDir = "ExcelTransfer_Portable"
)

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Excel Transfer - Standalone Build (Ninja)" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Find Visual Studio installation
$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vsWhere) {
    $vsPath = & $vsWhere -latest -property installationPath
    if ($vsPath) {
        Write-Host "Found Visual Studio at: $vsPath" -ForegroundColor Green
        $vcvarsPath = "$vsPath\VC\Auxiliary\Build\vcvars64.bat"
        if (Test-Path $vcvarsPath) {
            Write-Host "Using: $vcvarsPath" -ForegroundColor Green
        }
    }
}

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

# ── Step 2: Setup environment ──────────────────────────────────────────────
Write-Host "[2/7] Setting up build environment..." -ForegroundColor Yellow
$env:PATH = "$QtPath\bin;$env:PATH"

# ── Step 3: Configure CMake with Ninja ─────────────────────────────────────
Write-Host "[3/7] Configuring CMake (Ninja)..." -ForegroundColor Yellow

Push-Location "build-standalone"

# Use cmd to run vcvars and then cmake
$cmakeCmd = @"
call "$vcvarsPath" && cmake .. -G Ninja -DCMAKE_PREFIX_PATH="$QtPath" -DCMAKE_BUILD_TYPE=$BuildType
"@

$cmakeCmd | cmd

if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ CMake configuration failed!" -ForegroundColor Red
    Pop-Location
    exit 1
}

# ── Step 4: Build the project ──────────────────────────────────────────────
Write-Host "[4/7] Building project ($BuildType)..." -ForegroundColor Yellow
cmake --build . --config $BuildType --parallel

if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ Build failed!" -ForegroundColor Red
    Pop-Location
    exit 1
}

Pop-Location

# ── Step 5: Copy executable ────────────────────────────────────────────────
Write-Host "[5/7] Copying executable..." -ForegroundColor Yellow
$exePath = "build-standalone\ExcelTransfer.exe"
if (-not (Test-Path $exePath)) {
    Write-Host "❌ Executable not found at: $exePath" -ForegroundColor Red
    exit 1
}
Copy-Item $exePath "$OutputDir\" -Force

# ── Step 6: Deploy Qt dependencies ─────────────────────────────────────────
Write-Host "[6/7] Deploying Qt dependencies..." -ForegroundColor Yellow
$windeployqt = "$QtPath\bin\windeployqt.exe"
if (-not (Test-Path $windeployqt)) {
    Write-Host "❌ windeployqt not found at: $windeployqt" -ForegroundColor Red
    Write-Host "Please check Qt installation path" -ForegroundColor Yellow
    exit 1
}

Push-Location $OutputDir
& $windeployqt "ExcelTransfer.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw
Pop-Location

# ── Step 7: Copy additional files ──────────────────────────────────────────
Write-Host "[7/7] Copying additional files..." -ForegroundColor Yellow

# Copy JSON folder
if (Test-Path "JSON") {
    Write-Host "  → Copying JSON configuration files..." -ForegroundColor Gray
    Copy-Item "JSON" "$OutputDir\" -Recurse -Force
}

# Copy repair script
if (Test-Path "repair.ps1") {
    Write-Host "  → Copying repair script..." -ForegroundColor Gray
    Copy-Item "repair.ps1" "$OutputDir\" -Force
}

# Create README
$readmeContent = @"
Excel Transfer Tool - Portable Edition
=======================================

Version: 1.0
Build Date: $(Get-Date -Format "yyyy-MM-dd HH:mm")
Build Type: $BuildType

QUICK START
-----------
1. Double-click ExcelTransfer.exe to run
2. No installation required!
3. All files must stay in this folder

REQUIREMENTS
------------
- Windows 10/11 (64-bit)
- Microsoft Excel 2016+

For full documentation, see the user manual.

© 2026 Finance Automation Team
"@

Set-Content -Path "$OutputDir\README.txt" -Value $readmeContent -Encoding UTF8

# Create launcher
$launcherContent = @"
@echo off
cd /d "%~dp0"
start "" "ExcelTransfer.exe"
"@

Set-Content -Path "$OutputDir\Launch.bat" -Value $launcherContent -Encoding ASCII

# ── Summary ─────────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "✅ Build Complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "Output folder: $OutputDir" -ForegroundColor Cyan

$folderSize = (Get-ChildItem $OutputDir -Recurse | Measure-Object -Property Length -Sum).Sum / 1MB
Write-Host "Total size: $([math]::Round($folderSize, 2)) MB" -ForegroundColor Cyan
Write-Host ""

# Create ZIP
$createZip = Read-Host "Create ZIP archive? (y/n)"
if ($createZip -eq "y" -or $createZip -eq "Y") {
    Write-Host "Creating ZIP..." -ForegroundColor Yellow
    $zipPath = "$OutputDir.zip"
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    Compress-Archive -Path "$OutputDir\*" -DestinationPath $zipPath -CompressionLevel Optimal
    $zipSize = (Get-Item $zipPath).Length / 1MB
    Write-Host "✅ ZIP created: $zipPath ($([math]::Round($zipSize, 2)) MB)" -ForegroundColor Green
}

Write-Host ""
Write-Host "Done! 🎉" -ForegroundColor Green
