# ESP32 To PC To Git TODO

This file tracks the work needed to move voltage data from the ESP32 to a PC, write it to a local file, and automatically push updates to Git so a static site can read the latest data.

## Goal

Build an automation path like this:

`ESP32 -> PC receiver -> local data file -> git commit/push -> GitHub Pages or other static site`

## Recommended Design

- Use the ESP32 to send JSON to a small HTTP server running on the PC
- Have the PC server write a local file such as `data/latest.json`
- Use a watcher or timed sync script on the PC to detect changes
- Commit and push from the PC, not from the ESP32
- Start the receiver and sync process automatically after Windows restarts

## Phase 1: Define Data Format ✓ COMPLETED

**Status:** JSON format defined in `data/latest.json` (sample)

The ESP32 should send this JSON structure:
```json
{
  "voltage": 12.456,
  "threshold": 11.50,
  "timestamp": 1717401234,
  "ssid": "YourNetwork",
  "ip": "192.168.1.100",
  "emailEnabled": true,
  "emailSender": "your.esp32@gmail.com",
  "emailRecipient": "alerts@example.com",
  "hasAppPassword": true,
  "repeatAlertHours": 2.00,
  "history": {
    "points": [
      {"voltage": 12.34, "epoch": 1717384234},
      {"voltage": 12.38, "epoch": 1717384534}
    ],
    "intervalMinutes": 5,
    "totalHours": 48,
    "threshold": 11.50
  }
}
```

Upload frequency: Every 5 minutes (aligned with history bucket interval)

## Phase 2: PC Receiver ✓ IN PROGRESS

**Status:** Python server created in `pc_receiver.py`

Features implemented:
- ✓ HTTP server listening on port 52501
- ✓ Accepts POST requests from ESP32
- ✓ Validates incoming JSON structure
- ✓ Writes to `data/latest.json`
- ✓ Appends to `data/history.jsonl`
- ✓ Returns success/error responses
- ✓ Logging to console and `data/receiver.log`
- ✓ Status page at http://localhost:52501

Files created:
- `pc_receiver.py` - Main server script
- `test_receiver.py` - Test script to verify server
- `PC_RECEIVER_SETUP.md` - Complete setup and troubleshooting guide
- `requirements.txt` - Python dependencies (requests for testing, watchdog for git sync)

Next steps:
- Run `python pc_receiver.py` to start the server
- Run `python -m pip install requests` then `python test_receiver.py` to test
- Follow `PC_RECEIVER_SETUP.md` to set up automatic startup
- ESP32 can auto-discover PC (no manual IP needed!)

## Phase 3: File Change Handling ✓ COMPLETED

**Status:** File watcher implemented in `git_sync.py`

- ✓ Detects when `data/latest.json` changes using watchdog library
- ✓ Avoids committing unchanged content (checks git status)
- ✓ Debouncing implemented (10 second default, configurable)
- ✓ Push strategy: debounced automatic (waits 10s after change)

## Phase 4: Git Automation ✓ IN PROGRESS

**Status:** Git sync script created in `git_sync.py`

Features implemented:
- ✓ Automated sync script using watchdog library
- ✓ Runs `git add`, `git commit`, `git push` automatically
- ✓ Commit messages include voltage and timestamp
- ✓ Checks for changes before committing (avoids empty commits)
- ✓ Debouncing prevents commit spam
- ✓ Full error handling and logging
- ✓ Graceful shutdown on Ctrl+C

Files created:
- `git_sync.py` - Main file watcher and Git automation
- `start_all.bat` - Launcher for both receiver and sync
- `GIT_SYNC_SETUP.md` - Complete setup guide
- `requirements.txt` updated with watchdog dependency

Next steps:
- Install watchdog: `python -m pip install watchdog`
- Configure Git credentials (see GIT_SYNC_SETUP.md)
- Test with: `python git_sync.py`
- Run both services with: `start_all.bat`

## Phase 5: Windows Startup Automation

- Make sure the PC receiver starts automatically after reboot
- Make sure the Git sync watcher starts automatically after reboot
- Choose one startup method:
  - Task Scheduler at startup
  - Task Scheduler at logon
  - NSSM or Windows service wrapper for long-running processes
- Configure restart behavior if the receiver crashes

## Phase 6: ESP32 Firmware Changes ✓ IN PROGRESS

**Status:** HTTP client functionality added to `arduino_voltage_monitor.ino`

Features implemented:
- ✓ HTTPClient library included
- ✓ **Live status display** - Real-time connection status with emoji indicators
- ✓ **Status API** - `/upload_status` endpoint for JavaScript polling
- ✓ PC receiver URL configuration (saved to preferences)
- ✓ Upload interval control (configurable in minutes)
- ✓ JSON builder function with full voltage + history data
- ✓ uploadDataToPC() function with status tracking
- ✓ Web UI panel for PC upload settings with live status updates
- ✓ Test upload button in web interface
- ✓ Automatic uploads on configured interval
- ✓ Graceful failure handling (keeps working if PC is offline)
- ✓ Settings persist across reboots

Next steps:
- Upload the updated sketch to your ESP32
- Configure PC receiver URL in the ESP32 web interface
- Test the upload functionality and watch live status updates
- Verify that local dashboard and email alerts still work when PC is offline

## Phase 7: Static Site Consumption ✓ COMPLETED

**Status:** Static page created at `index.html`

The static GitHub Pages dashboard:
- Fetches data from `data/latest.json` (pushed by PC-to-Git automation)
- Displays current voltage, threshold, and last update time
- Renders the full 48-hour history chart with threshold overlay
- Shows device information (WiFi SSID, IP, email alert status)
- Auto-refreshes every 60 seconds
- Uses the same styling and chart rendering as the ESP32-hosted dashboard
- Works as a read-only remote view (no settings controls)

To enable on GitHub Pages:
1. Push `index.html` and `data/` folder to your repository
2. Enable GitHub Pages in repository settings (deploy from `main` branch, root folder)
3. Access at `https://yourusername.github.io/voltage-monitor/`

## Reliability Checks

- Test with the PC turned off
- Test with the PC IP address changed
- Test with GitHub temporarily unreachable
- Test with invalid JSON from the ESP32
- Test restart behavior after a Windows reboot
- Test duplicate uploads to confirm the repo does not get spammed with noise commits

## Nice To Have

- Add a heartbeat field so the static site can show whether the device is stale
- Add batching so multiple readings become one commit
- Add local log rotation on the PC
- Add a second file for graph history instead of only `latest.json`
- Add basic authentication or a shared token between ESP32 and the PC receiver

## Completion Criteria

- ESP32 can upload voltage data to the PC reliably
- The PC updates a local JSON file automatically
- Changes are committed and pushed to Git automatically
- The automation survives Windows restarts
- A static page can read the pushed file without talking directly to the ESP32