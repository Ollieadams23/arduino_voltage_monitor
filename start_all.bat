@echo off
REM Combined launcher for ESP32 Voltage Monitor PC services
REM Starts both the receiver and Git sync in separate windows

cd /d "%~dp0"

echo Starting ESP32 Voltage Monitor services...
echo.

REM Start PC receiver in its own window
start "ESP32 Voltage Receiver" python pc_receiver.py

REM Wait 5 seconds for receiver to initialize
echo Waiting for receiver to start...
timeout /t 5 /nobreak > nul

REM Start Git sync in its own window
start "ESP32 Git Sync" python git_sync.py

echo.
echo Both services started!
echo.
echo Windows:
echo   - ESP32 Voltage Receiver (port 52501)
echo   - ESP32 Git Sync (watching data/latest.json)
echo.
echo Close the terminal windows to stop the services.
pause
