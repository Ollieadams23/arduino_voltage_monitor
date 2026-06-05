@echo off
setlocal
cd /d "%~dp0"
start "ESP32 Tray Status" pythonw "%~dp0tray_status.py"