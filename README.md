# Arduino Voltage Monitor

Project for ESP-32/ESP-8266 dev board to monitor voltage.

## Email Alerts

The sketch supports direct Gmail SMTP alerts from the ESP32 when voltage drops below the configured threshold.

### Required Arduino Libraries
- WiFiManager
- ESP Mail Client

### Gmail Setup
1. Sign in to the Gmail account you want the device to send from.
2. Turn on 2-Step Verification for that Google account.
3. Open the Google Account page, go to Security, then create an App Password.
4. Choose Mail as the app and give it a device name such as ESP32 Voltage Monitor.
5. Copy the 16-character app password that Google shows you.

### Device Setup
Email alerts are disabled by default.

After uploading the sketch:
1. Connect the ESP32 to Wi-Fi through WiFiManager.
2. Open the device IP address in a browser.
3. In the Email Alerts section, enter:
	- the Gmail sender address
	- the Gmail app password
	- the recipient email address
4. Enable email alerts and save.

### Test Flow
1. Upload the sketch to the ESP32.
2. Connect the board to Wi-Fi through WiFiManager.
3. Open the device IP address in a browser.
4. Press `Send Test Email`.
5. Watch Serial Monitor if the email fails.

### Time Sync Requirement
The ESP32 must have the correct time before Gmail SMTP will work over TLS. The sketch now syncs time automatically using NTP after Wi-Fi connects and again before sending email if needed.

The low-voltage email alert sends once when voltage first falls below the threshold, then rearms after the voltage rises back above the threshold plus a small hysteresis margin.

## Structure
- `arduino_voltage_monitor.ino`: Main sketch
- `lib/`: Custom libraries
- `data/`: Files for SPIFFS/LittleFS
- `src/`: Additional source files
- `images/`: Documentation images
