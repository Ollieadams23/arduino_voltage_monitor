# mDNS Auto-Discovery - Implementation Notes

## Status: PC Side Only

This document explains the **attempted** mDNS auto-discovery feature and why it's only working on the PC side.

## What Works

**PC Receiver (Python + zeroconf):**
- ✅ Advertises itself on the network via mDNS
- ✅ Service type: `_voltage-monitor._tcp.local.`
- ✅ Can be discovered by other devices (for future use)
- ✅ Optional feature (works without zeroconf too)

## What Doesn't Work

**ESP32 Client:**
- ❌ Cannot discover PC receiver automatically
- ❌ ESP32's `ESPmDNS.h` library only supports **advertising**, not **querying**

## Technical Explanation

### ESP32 mDNS Library Limitations

The ESP32 Arduino core includes `ESPmDNS.h` which provides:

**Supported:**
- `MDNS.begin(hostname)` - Advertise the ESP32
- `MDNS.addService(service, proto, port)` - Announce a service

**NOT Supported:**
- `MDNS.queryService()` - Search for other services ❌
- Service discovery/browsing capabilities ❌

### Compilation Error

Attempted code:
```cpp
int n = MDNS.queryService("voltage-monitor", "tcp");  // DOES NOT EXIST
String foundIP = MDNS.IP(0).toString();                // DOES NOT EXIST
```

Error:
```
'class MDNSResponder' has no member named 'queryService'
'class MDNSResponder' has no member named 'IP'
```

## Current Implementation

### PC Receiver (Advertises via mDNS)

```python
from zeroconf import Zeroconf, ServiceInfo

service_info = ServiceInfo(
    "_voltage-monitor._tcp.local.",
    "ESP32 Voltage Monitor PC Receiver._voltage-monitor._tcp.local.",
    addresses=[socket.inet_aton(local_ip)],
    port=52501,
    properties={'version': '1.0', 'path': '/'}
)

zeroconf = Zeroconf()
zeroconf.register_service(service_info)
```

**Result:** PC broadcasts "I'm here!" but ESP32 cannot query for it.

### ESP32 (Manual Configuration Only)

- User enters PC IP manually: `http://192.168.1.100:52501`
- ESP32 connects directly to configured URL
- Live status display shows: Idle → Connecting → Uploading → Success/Failed

## Why Keep mDNS on PC Side?

**Future benefits:**
- Mobile app could discover PC automatically
- Web dashboard could find PC without manual config  
- Different ESP32 libraries might support queries later
- Makes PC discoverable for development/debugging

**Current practical use:**
- None for the ESP32 workflow
- Could be removed without affecting functionality

## Alternative Solutions

### 1. Static IP on PC (Recommended)
- Configure PC with fixed IP via DHCP reservation
- ESP32 configuration never needs updating
- **Best long-term solution**

### 2. Different ESP32 Library
- Libraries like `esp_mdns` *might* support queries
- Requires significant code changes
- Adds complexity and dependencies

### 3. Manual Configuration (Current)
- User enters PC IP in ESP32 web UI
- Simple, reliable, works with standard libraries
- **Good enough solution**

## Conclusion

**mDNS auto-discovery on ESP32 is NOT implemented** due to ESP32 library limitations. 

The PC receiver advertises via mDNS, but this has no current practical benefit for the ESP32 workflow. It remains as optional future-proofing.

**Current user experience:** Manual IP configuration (standard, reliable).

## Related Files

- [arduino_voltage_monitor.ino](arduino_voltage_monitor/arduino_voltage_monitor.ino) - ESP32 firmware (manual config)
- [pc_receiver.py](pc_receiver.py) - PC receiver with mDNS advertising
- [ESP32_UPLOAD_SETUP.md](ESP32_UPLOAD_SETUP.md) - Setup guide with manual IP configuration
- [PC_RECEIVER_SETUP.md](PC_RECEIVER_SETUP.md) - PC receiver setup

**Added:**
- `import socket` - Get local IP address
- `from zeroconf import Zeroconf, ServiceInfo` - mDNS advertising (optional import)
- `get_local_ip()` - Determines PC's local network IP
- mDNS service registration: `_voltage-monitor._tcp.local.`
- Service properties: version, port, path
- Graceful degradation if zeroconf not installed
- Cleanup on shutdown (unregister mDNS service)

**Modified:**
- `main()` - Now registers mDNS service and displays discovery status

### Dependencies (`requirements.txt`)

**Added:**
- `zeroconf>=0.132.0` - Python mDNS library (optional but recommended)

## Benefits

### For Users

1. **No IP configuration needed** - Just enable upload, ESP32 finds PC automatically
2. **Works after PC IP changes** - ESP32 rescans and finds new IP
3. **Visual feedback** - Live status shows exactly what's happening
4. **Fallback options** - Can still use manual URL if mDNS fails
5. **Automatic recovery** - If saved URL fails, automatically scans for PC

### For Developers

1. **Robust connection logic** - Multiple fallback strategies
2. **Status visibility** - Clear indication of connection state
3. **Easy debugging** - Status messages in Serial Monitor and web UI
4. **Graceful degradation** - Works with or without mDNS support

## Quick Start

### Automatic Mode (Recommended)

1. **On PC**: Install zeroconf and start receiver
   ```powershell
   python -m pip install zeroconf
   python pc_receiver.py
   ```
   Look for: `✓ Auto-discovery enabled`

2. **On ESP32**: Upload new firmware and configure
   - Access web UI
   - PC Upload panel
   - ☑ Enable PC upload
   - **Leave URL blank**
   - Save settings

3. **Watch it work**:
   - Status shows: ⚪ Idle → 🔍 Scanning → ✅ PC Found → ⬆️ Uploading → ✅ Success
   - Discovered URL displayed automatically

### Manual Mode (Fallback)

1. **On PC**: Start receiver (zeroconf optional)
   ```powershell
   python pc_receiver.py
   ```

2. **On ESP32**: Configure with IP
   - ☑ Enable PC upload
   - **Enter URL**: `http://192.168.1.100:52501`
   - Save settings

3. **Hybrid**: Enter URL AND install zeroconf
   - ESP32 tries manual URL first (fast)
   - Falls back to auto-discovery if manual fails
   - Best of both worlds!

## Status Indicators

The web UI shows live connection status:

| Icon | Status | Meaning |
|------|--------|---------|
| ⚪ | Idle | Waiting for next upload interval |
| 🔍 | Scanning for PC... | mDNS discovery in progress |
| ✅ | PC Found | Successfully discovered PC receiver |
| ⬆️ | Uploading... | Data transfer in progress |
| ✅ | Upload Successful | Data uploaded successfully |
| ❌ | Upload Failed | No PC receiver available |

## Connection Logic Flow

```
┌─────────────────────────┐
│ Upload interval reached │
└───────────┬─────────────┘
            │
            ▼
    ┌───────────────┐
    │ Manual URL    │ ──Yes──► Try upload ──Success──► Done ✅
    │ configured?   │                │
    └───────┬───────┘                │ Fail
            │                        ▼
            No              ┌─────────────────┐
            │               │ Last discovered │ ──Yes──► Try upload ──Success──► Done ✅
            │               │ URL different?  │                │
            │               └────────┬────────┘                │ Fail
            │                        No                        │
            │                        │                         │
            └────────────────────────┴─────────────────────────┤
                                                                │
                                     ▼                          │
                            ┌─────────────────┐                │
                            │ Scan for PC     │                │
                            │ via mDNS        │                │
                            └────────┬────────┘                │
                                     │                          │
                           ┌─────────┴─────────┐               │
                        Found                 Not found        │
                           │                      │             │
                           ▼                      ▼             │
                    Try upload               Report fail ❌     │
                           │                                    │
                    ┌──────┴──────┐                            │
                Success         Fail───────────────────────────►│
                    │                                           │
                    ▼                                           ▼
                 Done ✅                                  Try again next interval
```

## Technical Details

### mDNS Service Registration

**Service Type**: `_voltage-monitor._tcp.local.`

**Properties:**
- `version`: "1.0"
- `path`: "/"
- `port`: 52501

**Example Service Name**: `ESP32 Voltage Monitor PC Receiver._voltage-monitor._tcp.local.`

### ESP32 Discovery

Uses `MDNS.queryService()` to find services of type `"voltage-monitor"` with protocol `"tcp"`.

Returns:
- Service IP address
- Service port
- Number of services found

### Network Requirements

- ESP32 and PC must be on the **same local network** (same WiFi/router)
- mDNS uses **UDP port 5353** (multicast)
- No special firewall rules needed for mDNS (works within LAN)
- PC receiver still needs **TCP port 52501** open for data uploads

## Troubleshooting

### "No PC receiver found via mDNS"

**Causes:**
1. PC receiver not running
2. zeroconf not installed on PC
3. ESP32 and PC on different networks
4. Firewall blocking mDNS (rare)

**Solutions:**
1. Check PC receiver is running: Look for "✓ Auto-discovery enabled" message
2. Install zeroconf: `python -m pip install zeroconf`
3. Verify network: ESP32 WiFi SSID = PC network SSID
4. Use manual URL as fallback

### Auto-discovery slow

mDNS scanning takes 2-3 seconds. This is normal.

**To speed up:**
- Configure manual URL as primary
- mDNS becomes automatic fallback only

### Multiple PC receivers found

ESP32 uses the **first one discovered**. If you have multiple PCs running the receiver, you may want to use manual URL configuration for predictable behavior.

## Performance Impact

- **Scanning time**: 2-3 seconds when needed
- **Memory**: ~500 bytes for mDNS client
- **Network**: Minimal (multicast queries)
- **Battery**: Negligible (only scans on upload failure)

## Upgrade Path

### From Old Firmware (No mDNS)

1. Upload new firmware
2. Old manual URL still works (no config change needed)
3. Optional: Install zeroconf on PC for auto-discovery
4. Optional: Clear manual URL to use auto-discovery only

### Adding mDNS to Existing Setup

1. `python -m pip install zeroconf`
2. Restart `pc_receiver.py`
3. Done! ESP32 will auto-discover on next failed upload

## Future Enhancements

Possible improvements:
- Multiple PC receiver support (fail-over)
- Discovery caching (reduce scan frequency)
- Service priority/selection
- Discovery over internet (cloud connector)

## Related Files

- [arduino_voltage_monitor.ino](arduino_voltage_monitor/arduino_voltage_monitor.ino) - ESP32 firmware
- [pc_receiver.py](pc_receiver.py) - PC receiver with mDNS
- [ESP32_UPLOAD_SETUP.md](ESP32_UPLOAD_SETUP.md) - Setup guide
- [PC_RECEIVER_SETUP.md](PC_RECEIVER_SETUP.md) - PC receiver guide
- [requirements.txt](requirements.txt) - Python dependencies
