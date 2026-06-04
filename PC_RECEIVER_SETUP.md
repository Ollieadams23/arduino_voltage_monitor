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

## Windows Startup Automation

### Option A: Task Scheduler (Recommended)

1. Open Task Scheduler (`taskschd.msc`)
2. Click **Create Task** (not "Create Basic Task")
3. **General** tab:
   - Name: `ESP32 Voltage Receiver`
   - Description: `HTTP server to receive voltage data from ESP32`
   - Run whether user is logged on or not: ✓
   - Run with highest privileges: ✓
   - Hidden: ✓ (optional)

4. **Triggers** tab → New:
   - Begin the task: **At startup**
   - Delay task for: **30 seconds** (gives network time to connect)
   - Enabled: ✓

5. **Actions** tab → New:
   - Action: **Start a program**
   - Program/script: `python`
   - Add arguments: `pc_receiver.py`
   - Start in: `C:\Users\Ollie\Documents\projects\voltage monitor`

6. **Conditions** tab:
   - Uncheck "Start the task only if the computer is on AC power"
   - Check "Wake the computer to run this task" (optional)

7. **Settings** tab:
   - Allow task to be run on demand: ✓
   - Run task as soon as possible after a scheduled start is missed: ✓
   - If the task fails, restart every: **1 minute**
   - Attempt to restart up to: **3 times**
   - Stop the task if it runs longer than: **Unchecked** (needs to run continuously)
   - If the running task does not end when requested: **Do not stop**

8. Click **OK** and enter your Windows password

**Test the task:**
```powershell
Start-ScheduledTask -TaskName "ESP32 Voltage Receiver"
```

**Check if it's running:**
```powershell
Get-ScheduledTask -TaskName "ESP32 Voltage Receiver" | Get-ScheduledTaskInfo
```

**View logs:**
```powershell
Get-Content "data\receiver.log" -Tail 20 -Wait
```

### Option B: Startup Folder (Simple, but requires login)

1. Create a batch file `start_receiver.bat`:

```batch
@echo off
cd /d "%~dp0"
python pc_receiver.py
pause
```

2. Press `Win+R`, type `shell:startup`, press Enter
3. Create a shortcut to `start_receiver.bat` in the Startup folder
4. Right-click the shortcut → Properties → Run: **Minimized**

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
