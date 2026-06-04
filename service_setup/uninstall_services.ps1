param(
    [string]$PythonExe = "python"
)

$ErrorActionPreference = "Stop"

$serviceRoot = Split-Path -Parent $PSCommandPath

function Remove-ServiceRegistration {
    param(
        [string]$Name,
        [string]$ScriptPath
    )

    if (-not (Get-Service -Name $Name -ErrorAction SilentlyContinue)) {
        Write-Host "Service '$Name' is not installed."
        return
    }

    try {
        & $PythonExe $ScriptPath stop | Out-Null
    }
    catch {
    }

    & $PythonExe $ScriptPath remove
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to remove service '$Name'."
    }

    Write-Host "Service '$Name' removed."
}

Remove-ServiceRegistration -Name "ESP32GitSync" -ScriptPath (Join-Path $serviceRoot "git_sync_service.py")
Remove-ServiceRegistration -Name "ESP32VoltageReceiver" -ScriptPath (Join-Path $serviceRoot "pc_receiver_service.py")