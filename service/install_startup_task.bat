@echo off
:: Force the script to look at its own current folder
cd /d "%~dp0"

:: Check for admin privileges
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo.
    echo ERROR: This script requires Administrator privileges!
    echo Please right-click and select "Run as administrator"
    echo.
    pause
    exit /b 1
)

:: Get the full path of your VBScript dynamically
set "VBS_PATH=%cd%\start_service.vbs"

echo Creating hidden startup task...
echo.

:: Register the task in Task Scheduler
schtasks /create /tn "ESP32_Voltage_Monitor" /tr "wscript.exe \"%VBS_PATH%\"" /sc onlogon /rl highest /f

echo.
echo Task created successfully!
echo It will now run completely hidden every time you log into Windows.
pause