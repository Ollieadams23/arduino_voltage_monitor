# ESP32 Upload Setup Guide

The ESP32 firmware has been updated to upload voltage data to your PC receiver automatically.

## What Changed

### New Features Added:
1. **HTTP Client** - Sends data to PC receiver via HTTP POST
2. **PC Upload Settings** - New web UI panel to configure upload
3. **Persistent Configuration** - Settings saved to ESP32 flash memory
4. **Automatic Uploads** - Sends data on configurable interval
5. **Graceful Degradation** - Continues working if PC is offline

### New Code Sections:
- `#include <HTTPClient.h>` - HTTP client library
- Global variables for PC upload settings
- `buildUploadJson()` - Creates full JSON payload
- `uploadDataToPC()` - Sends data to PC receiver
- `savePCUploadSettings()` - Saves configuration
- Web endpoints: `/pc_upload_settings` and `/test_upload`
- New HTML panel for PC upload configuration
- Upload logic in `loop()` function

## How to Upload the Updated Code

1. **Open Arduino IDE**
2. **Load the sketch**: `arduino_voltage_monitor/arduino_voltage_monitor.ino`
3. **Connect your ESP32** via USB
4. **Select your board**: Tools → Board → ESP32 Dev Module (or your specific ESP32 board)
5. **Select the port**: Tools → Port → COM# (where your ESP32 is connected)
6. **Upload**: Click the Upload button (→)
7. **Wait** for "Done uploading" message

## Configure PC Upload

### Step 1: Find Your PC's IP Address

On your PC, run PowerShell:
```powershell
Get-NetIPAddress -AddressFamily IPv4 | Where-Object {$_.IPAddress -like "192.168.*"}
```

Note your PC's IP address (e.g., `192.168.1.100`).

### Step 2: Configure ESP32

1. **Access the ESP32 dashboard** in your browser (the IP shown in Serial Monitor)
2. **Scroll down to the "PC Upload" section** (new panel at the bottom)
3. **Enter settings**:
   - ☑ Enable PC upload
   - **PC Receiver URL**: `http://YOUR_PC_IP:52501` (e.g., `http://192.168.1.100:52501`)
   - **Upload interval**: `5` minutes (or your preference)
4. **Click "Save PC Upload Settings"**

### Step 3: Test the Connection

1. **Make sure the PC receiver is running**:
   ```powershell
   python pc_receiver.py
   ```

2. **Click "Test Upload Now"** button on the ESP32 dashboard

3. **Check for success**:
   - ESP32: Should show "Test upload successful!"
   - PC receiver: Should show log entry with voltage data
   - File: `data/latest.json` should be updated

## Monitoring Uploads

### On the ESP32 (Serial Monitor)

You'll see messages like:
```
Uploading data to PC receiver...
PC Upload successful! Response code: 200
Response: {"status":"success","message":"Data received and saved"...}
```

Or if the PC is offline:
```
Uploading data to PC receiver...
PC Upload failed! Error code: -1
Error: connection refused
```

### On the PC (Receiver Console)

You'll see messages like:
```
192.168.1.50 - "POST / HTTP/1.1" 200 -
Updated data/latest.json - Voltage: 12.450V
```

## Troubleshooting

### ESP32 can't connect to PC

**Error: "connection refused" or timeout**

Solutions:
1. Verify PC receiver is running: `Get-NetTCPConnection -LocalPort 52501`
2. Check firewall (see PC_RECEIVER_SETUP.md)
3. Verify ESP32 has correct PC IP address
4. Test from browser: `http://YOUR_PC_IP:52501`
5. Make sure ESP32 and PC are on same network

### Uploads work but data not in GitHub

This is expected! Phase 4 (Git automation) is next. Currently:
- ✓ ESP32 → PC receiver (working)
- ✗ PC → Git (not implemented yet)

### Want to change upload frequency

1. Go to ESP32 web dashboard
2. PC Upload section → Upload interval
3. Change to desired minutes (1-60)
4. Save settings

**Recommended intervals:**
- **1 minute** - High frequency, more data
- **5 minutes** - Default, balanced (matches history bucket)
- **15-30 minutes** - Lower frequency, less network traffic

### PC IP address changed

If your PC gets a new IP (common with DHCP):
1. Find new IP: `ipconfig` in PowerShell
2. Update ESP32: Go to web dashboard → PC Upload → Update URL
3. Save settings

**Tip**: Configure a static IP for your PC to avoid this.

## Data Format Sent to PC

The ESP32 sends this JSON structure **with the complete 48-hour history**:
```json
{
  "voltage": 12.450,
  "threshold": 11.50,
  "timestamp": 1717523456,
  "ssid": "YourNetwork",
  "ip": "192.168.1.50",
  "emailEnabled": true,
  "emailSender": "your@gmail.com",
  "emailRecipient": "alerts@example.com",
  "hasAppPassword": true,
  "repeatAlertHours": 2.00,
  "history": {
    "points": [
      {"voltage": 12.34, "epoch": 1717520000},
      {"voltage": 12.38, "epoch": 1717520300}
    ],
    "intervalMinutes": 5,
    "totalHours": 48,
    "threshold": 11.50
  }
}
```

**Important:** Each upload contains the full 48-hour rolling history from the ESP32. The PC receiver overwrites `data/latest.json` with this complete dataset, so the file stays a constant size and always contains the most recent 48 hours of data.

## What Happens If PC Is Offline?

The ESP32 handles this gracefully:

✓ **Local dashboard** - Still works (access via ESP32 IP)  
✓ **Email alerts** - Still works (sends directly to Gmail)  
✓ **Voltage monitoring** - Still works (LED, readings, history)  
✗ **PC uploads** - Fails silently, tries again next interval

**No blocking or freezing** - the ESP32 continues monitoring normally.

## Disable PC Upload

If you want to stop uploads temporarily:
1. Go to ESP32 web dashboard
2. PC Upload section → Uncheck "Enable PC upload"
3. Save settings

Or permanently delete the feature by reverting the code changes.

## Next Steps

After PC upload is working:
1. ✓ **Phase 6 complete!**
2. Next: Set up Git automation (Phase 4) to push data from PC to GitHub
3. Then: Enable GitHub Pages to display your static site
4. Finally: Configure Windows startup automation (Phase 5)

## Technical Details

- **Library**: Uses ESP32's built-in `HTTPClient.h`
- **HTTP Method**: POST with JSON body
- **Timeout**: 5 seconds per request
- **Retry**: Automatic on next interval (no retry loop to avoid blocking)
- **Memory**: Minimal heap usage (~2KB for JSON buffer)
- **Storage**: ~100 bytes in NVS flash for settings
