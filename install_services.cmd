@echo off
setlocal
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0service_setup\install_services.ps1" %*
exit /b %errorlevel%