@echo off
:: Remove Windows startup task and kill running services

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

cd /d "%~dp0"

echo Removing startup task from Windows Task Scheduler...
echo.

:: Delete the Task Scheduler task
schtasks /delete /tn "ESP32_Voltage_Monitor" /f

echo.
echo Killing running Python services...
echo.

:: Kill all python.exe processes (our services run as Python)
taskkill /f /im python.exe 2>nul

echo.
echo Done! Startup task removed and services stopped.
echo Services will not start automatically on next login.
pause
