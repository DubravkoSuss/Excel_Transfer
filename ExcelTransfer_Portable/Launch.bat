@echo off
REM Excel Transfer Launcher
cd /d "%~dp0"
if not exist "ExcelTransfer.exe" (
    echo ERROR: ExcelTransfer.exe not found!
    pause
    exit /b 1
)
start "" "ExcelTransfer.exe"
