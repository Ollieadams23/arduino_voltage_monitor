param(
    [string]$PythonExe = "python"
)

$ErrorActionPreference = "Stop"

$serviceRoot = Split-Path -Parent $PSCommandPath
$projectRoot = Split-Path -Parent $serviceRoot

function Set-PythonServiceRegistry {
    param(
        [string]$Name,
        [string]$PythonClass,
        [string]$PythonPath
    )

    $serviceKey = "HKLM\SYSTEM\CurrentControlSet\Services\$Name"
    reg add "$serviceKey\PythonClass" /ve /t REG_SZ /d $PythonClass /f | Out-Null
    reg add "$serviceKey\PythonPath" /ve /t REG_SZ /d $PythonPath /f | Out-Null
}

function Ensure-ServiceInstalled {
    param(
        [string]$Name,
        [string]$ScriptPath,
        [string]$Description,
        [string]$Dependency = "",
        [string]$PythonClass
    )

    if (Get-Service -Name $Name -ErrorAction SilentlyContinue) {
        Write-Host "Service '$Name' already exists. Updating registration."
        & $PythonExe $ScriptPath --startup auto update
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to update service '$Name'."
        }
    }
    else {
        & $PythonExe $ScriptPath --startup auto install
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to install service '$Name'."
        }
    }

    Set-PythonServiceRegistry -Name $Name -PythonClass $PythonClass -PythonPath $serviceRoot

    sc.exe description $Name $Description | Out-Null
    sc.exe failure $Name reset= 86400 actions= restart/5000/restart/5000/restart/5000 | Out-Null

    if ($Dependency) {
        sc.exe config $Name depend= $Dependency | Out-Null
    }

    & $PythonExe $ScriptPath restart | Out-Null
    if ($LASTEXITCODE -ne 0) {
        & $PythonExe $ScriptPath start | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to start service '$Name'."
        }
    }

    Write-Host "Service '$Name' is installed and running."
}

Ensure-ServiceInstalled -Name "ESP32VoltageReceiver" -ScriptPath (Join-Path $serviceRoot "pc_receiver_service.py") -Description "Receives ESP32 uploads and writes local voltage data files." -PythonClass "pc_receiver_service.PCReceiverService"
Ensure-ServiceInstalled -Name "ESP32GitSync" -ScriptPath (Join-Path $serviceRoot "git_sync_service.py") -Description "Watches latest.json and pushes voltage updates to GitHub." -Dependency "ESP32VoltageReceiver" -PythonClass "git_sync_service.GitSyncService"

Write-Host ""
Write-Host "Services installed. Check status with:"
Write-Host "  Get-Service ESP32VoltageReceiver, ESP32GitSync"