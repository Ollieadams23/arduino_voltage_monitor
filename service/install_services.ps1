param(
    [string]$PythonExe = "python"
)

& (Join-Path (Split-Path -Parent $PSCommandPath) "service_setup\install_services.ps1") -PythonExe $PythonExe