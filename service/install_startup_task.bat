@echo off
:: Force the script to look at its own current folder
cd /d "%~dp0"

:: Get the full path of your VBScript dynamically
set "VBS_PATH=%cd%\launch_all.vbs"

echo Creating hidden startup task...
echo.

:: Register the task in Task Scheduler
schtasks /create /tn "ESP32_Voltage_Monitor" /tr "wscript.exe \"%VBS_PATH%\"" /sc onlogon /rl highest /f

echo.
echo Task created successfully!
echo It will now run completely hidden every time you log into Windows.
pause