# Arduino Voltage Monitor

ESP32-based voltage monitor with:
- analog voltage measurement through a resistor divider
- local web page for live readings and threshold changes
- LED indication when voltage is below the threshold
- optional Gmail email alerts with configurable repeat reminders

## Hardware

Current sketch assumptions:
- board: ESP32
- analog input: GPIO34
- alert LED: GPIO2
- voltage divider: 100k over 30k

That divider gives a scale factor of about `4.333`, which is what the sketch uses to convert the ADC pin voltage back to the input voltage.

## Features

- Reads voltage once per second
- Hosts a local web page over Wi-Fi
- Stores threshold and email settings in ESP32 Preferences
- Email alerts are disabled by default
- Sends one email when voltage first drops below the threshold
- Rearms after the voltage rises above the threshold plus a small hysteresis margin
- Can optionally repeat reminder emails every configured number of hours while voltage stays low

## Required Arduino Libraries

- WiFiManager
- ESP Mail Client

## Arduino IDE Notes

This sketch targets ESP32, not a generic Arduino Uno.

If the sketch becomes too large after enabling email support, try a larger ESP32 partition scheme in Arduino IDE, such as `Huge APP` or `No OTA`.

## Wi-Fi Setup

On first boot, WiFiManager will open a configuration portal if the board does not already know your Wi-Fi network.

Default captive portal details in the sketch:
- SSID: `arduino voltage monitor`
- password: `00000000`

After connecting the board to Wi-Fi, open the device IP address in a browser.

## Web Page Functions

The self-hosted page lets you:
- view the current measured input voltage
- change the low-voltage threshold
- enable or disable email alerts
- enter the Gmail sender address
- enter the Gmail app password
- enter the recipient email address
- set repeat reminder interval in hours
- send a manual test email

If the app password field is left blank while saving, the device keeps the previously stored password.

## Gmail Setup

To use Gmail SMTP directly from the ESP32:

1. Create or use a Gmail account dedicated to the device.
2. Turn on 2-Step Verification for that Google account.
3. Go to Google Account Security.
4. Create an App Password for Mail.
5. Copy the 16-character app password.
6. Enter those details on the device web page.

Important:
- use the Gmail app password, not the normal Gmail login password
- the sender account and the app password must belong to the same Google account

## Time Sync Requirement

Gmail SMTP over TLS requires correct device time.

The sketch now syncs time automatically using NTP after Wi-Fi connects and again before sending email if time is not already valid.

## Alert Behavior

Low-voltage email behavior is currently:
- first alert sends when voltage drops below the threshold
- no repeated alerts while voltage stays low unless repeat reminders are enabled
- repeat reminders send every configured number of hours while voltage remains low
- setting repeat hours to `0` disables repeat reminders
- once voltage recovers above threshold plus hysteresis, the alert state resets and the next drop can send a new first alert

## Test Flow

1. Upload the sketch to the ESP32.
2. Open Serial Monitor at `115200`.
3. Connect the board to Wi-Fi.
4. Open the device IP address in a browser.
5. Configure threshold and email settings.
6. Press `Send Test Email`.
7. Check inbox and spam folder.
8. If email fails, inspect Serial Monitor output.

## Project Structure

- `arduino_voltage_monitor.ino`: main sketch
- `lib/`: custom libraries if added later
- `data/`: files for SPIFFS or LittleFS if used later
- `src/`: additional source files if split out later
- `images/`: documentation images
