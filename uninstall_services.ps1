param(
    [string]$PythonExe = "python"
)

& (Join-Path (Split-Path -Parent $PSCommandPath) "service_setup\uninstall_services.ps1") -PythonExe $PythonExe