# PC Receiver Setup Guide

This guide explains how to run and configure the PC receiver server for your ESP32 voltage monitor.

## Quick Start

### 1. Test the Server Manually

Open PowerShell in the project directory and run:

```powershell
python pc_receiver.py
```

You should see:
```
============================================================
ESP32 Voltage Monitor - PC Receiver Starting
============================================================
Server listening on http://0.0.0.0:52501
...
```

### 2. Test from Your Browser

Open `http://localhost:52501` in your browser. You should see a status page confirming the server is running.

### 3. Test with Sample Data

Open a second PowerShell window and send test data:

```powershell
$body = @{
    voltage = 12.45
    threshold = 11.50
    timestamp = [int][double]::Parse((Get-Date -UFormat %s))
    ssid = "TestNetwork"
    ip = "192.168.1.100"
} | ConvertTo-Json

Invoke-RestMethod -Uri http://localhost:52501 -Method Post -Body $body -ContentType "application/json"
```

If successful, you'll see:
- Success message in PowerShell
- Log entry in the server window
- New file created: `data/latest.json`
- Entry appended to: `data/history.jsonl`

## Finding Your PC's IP Address

The ESP32 needs to know your PC's IP address. Find it with:

```powershell
Get-NetIPAddress -AddressFamily IPv4 | Where-Object {$_.IPAddress -like "192.168.*"} | Select-Object IPAddress
```

Your ESP32 will send data to: `http://YOUR_PC_IP:52501`

## Running the Scripts

The voltage monitor scripts run continuously in the background to receive ESP32 data and sync to GitHub.

### Option 1: Manual Start (Simplest)

Open PowerShell in the `service/` folder and run:

```powershell
python pc_receiver.py
python git_sync.py
```

Run each in a separate terminal window, or use a process manager to keep them running.

### Option 2: Using the Tray Status App

The system tray app provides visual status monitoring and control:

```powershell
python tray_status.py
```

See [System Tray Status Monitor](#system-tray-status-monitor) section below for details.

### Option 3: Task Scheduler (Auto-start at Boot)

To auto-start the scripts when Windows boots:

1. Open Task Scheduler (`taskschd.msc`)
2. Create a new task for `pc_receiver.py`:
   - **General**: Run whether user is logged on or not
   - **Trigger**: At startup (with 30-second delay)
   - **Action**: Run `python pc_receiver.py` in the service folder
   - **Settings**: Restart on failure

3. Repeat for `git_sync.py` and `tray_status.py` (optional)

### Option 4: Startup Shortcut

Create a `.vbs` script to launch the Python scripts hidden in the background on login:

```vbs
' start_scripts.vbs
Set objShell = CreateObject("WScript.Shell")
objShell.Run "python C:\Path\To\service\pc_receiver.py", 0, False
objShell.Run "python C:\Path\To\service\git_sync.py", 0, False
```

Place in your Startup folder (`shell:startup`) for auto-launch on login.

## System Tray Status Monitor

A system tray application provides real-time status monitoring and service control from the Windows taskbar.

### Starting the Tray App

Run from the service folder:
```powershell
python tray_status.py
```

Or create a shortcut in your Startup folder for automatic launch on login.

### Tray Icon Status Indicators

The icon color indicates overall system health:

- **Green circle** – Healthy: Both scripts running AND fresh data received
- **Yellow circle** – Partial: Scripts running but no fresh data yet (waiting for ESP32 updates)
- **Red circle** – Action needed: Scripts stopped or disconnected

### Tray Menu Features

Click the icon to access:

**Status Information:**
- Overall status and health summary
- Receiver script state
- Git Sync script state
- Latest voltage reading and age (minutes since last update)
- Last Git push timestamp

**Quick Actions:**
- Open Receiver Status Page – Opens http://localhost:52501 in default browser
- Open Receiver Log – Opens `data/receiver.log` in Notepad
- Open Git Sync Log – Opens `data/git_sync.log` in Notepad
- Refresh Now – Force immediate status update
- Quit – Exit the tray app

### Data Flow Monitoring

The tray app displays:
- **Latest Voltage** – Most recent value received from ESP32
- **Data Age** – How many minutes since the last ESP32 upload
- **Last Git Push** – Timestamp of the most recent automatic git sync

If no ESP32 data has been received yet, these fields show "not available".

### Auto-Refresh Behavior

Status updates happen every 30 seconds, showing:
- Script state
- New voltage readings
- Latest sync times

## Verifying It's Running

### Check if the server is listening:
```powershell
Get-NetTCPConnection -LocalPort 52501 -State Listen
```

### Check the logs:
```powershell
Get-Content "data\receiver.log" -Tail 20
```

### Test the endpoint:
```powershell
Invoke-WebRequest -Uri http://localhost:52501
```

## Firewall Configuration

If your ESP32 can't reach the server:

```powershell
# Allow Python through Windows Firewall
New-NetFirewallRule -DisplayName "ESP32 Voltage Receiver" -Direction Inbound -Program "C:\Python\python.exe" -Action Allow
```

Or manually:
1. Windows Security → Firewall & network protection
2. Advanced settings → Inbound Rules → New Rule
3. Program → Next → Browse to `python.exe`
4. Allow the connection → Apply to all profiles
5. Name: "ESP32 Voltage Receiver"

## Troubleshooting

### Server won't start - "Address already in use"
Another program is using port 52501. Find it:
```powershell
Get-NetTCPConnection -LocalPort 52501 | Select-Object OwningProcess
Get-Process -Id <PID>
```

Change the port in `pc_receiver.py` if needed.

### Server starts but ESP32 can't connect
- Verify your PC's IP hasn't changed: `ipconfig`
- Check Windows Firewall (see above)
- Verify ESP32 is on the same network
- Test from another PC: `curl http://YOUR_PC_IP:52501`

### Data file not updating
Check the logs:
```powershell
Get-Content "data\receiver.log" -Tail 50
```

Look for validation errors or permission issues.

## Files Created

- `data/latest.json` - Most recent reading with full 48-hour history (overwritten each time, stays ~5-10KB)
- `data/history.jsonl` - Append-only log of all readings (automatically rotated to keep last 1000 entries)
- `data/receiver.log` - Server activity log

**For Git automation (Phase 4):** Only `data/latest.json` will be pushed to GitHub. It contains the complete 48-hour history and stays a constant size.

## Next Steps

After the PC receiver is working:
1. ✓ **Phase 2 complete!**
2. Next: Set up Git automation (Phase 4) to push `latest.json` to GitHub
3. Then: Update ESP32 firmware (Phase 6) to send data here

## ESP32 Configuration

When you update your ESP32 code, use these settings:

```cpp
// PC Receiver Configuration
const char* PC_RECEIVER_URL = "http://YOUR_PC_IP:52501";
const unsigned long UPLOAD_INTERVAL = 300000; // 5 minutes in milliseconds
```
