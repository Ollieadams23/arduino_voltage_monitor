# Developer Notes

## Persistence Model

This project stores runtime configuration on the ESP32 using `Preferences`, not in the sketch source.

Namespaces:
- `settings` for threshold and email configuration
- `history` for the 48-point hourly graph buffer

Stored on-device:
- alert threshold in the `settings` namespace as `vthresh`
- email enabled flag as `mail_on`
- email sender address as `mail_from`
- email app password as `mail_pass`
- email recipient as `mail_to`
- repeat reminder interval in hours as `mail_rpt_h`
- graph plot interval in minutes as `hist_int_m`
- 48-hour voltage history in the `history` namespace

Implications:
- uploading the same sketch to a different board will not copy settings from another board
- re-uploading the sketch to the same board usually keeps saved settings unless flash or data is erased
- the web page starts with empty email settings on a fresh board

## HTTP Route Surface

Current `WebServer` routes:
- `GET /` renders the dashboard HTML
- `GET /voltage` returns the current measured voltage as plain text
- `GET /threshold` returns the current threshold as plain text
- `GET /history` returns hourly history as JSON
- `GET /set_threshold?value=...` updates the threshold
- `POST /history_settings` updates the graph plot interval
- `POST /email_settings` updates email configuration and repeat interval
- `GET /test_email` sends a manual test email
- `POST /reset_wifi` clears saved Wi-Fi credentials and reboots

Notes:
- threshold update is currently done with a GET route, not POST
- the page depends on client-side polling for `voltage`, `threshold`, and `history`

## Reset Scope

The `Reset Wi-Fi Credentials` button only clears saved Wi-Fi credentials by calling `WiFi.disconnect(true, true)` and then restarting the ESP32.

It does not clear:
- alert threshold
- email settings
- repeat reminder settings
- saved voltage history

After Wi-Fi reset:
- the ESP32 reboots
- WiFiManager runs again during `setup()`
- if no valid Wi-Fi credentials remain, the config portal opens again

## History Behavior

The sketch samples voltage every second, but stored graph history is recorded at a fixed 5-minute base interval across a rolling 48-hour window.

Details:
- up to 576 base points are stored in a circular buffer
- each stored base point represents one 5-minute bucket
- the current 5-minute bucket is updated in RAM as readings change
- the history is committed to `Preferences` when a new 5-minute bucket starts
- valid network time is required for timestamped history points
- the UI can group those stored 5-minute points into larger plot intervals such as 15, 30, 60, or 180 minutes
- each displayed plot point is the arithmetic average of all stored base points inside the selected display interval

Implications:
- the graph does not backfill missing past time buckets
- immediately after flashing, the graph fills gradually over time
- if time is not yet synced, hourly history is not recorded
- the current in-progress 5-minute sample is not persisted to flash until bucket rollover, so a reboot inside the same bucket can lose the in-progress point
- changing the plot interval does not clear history; it only changes how stored base points are grouped for display

## Measurement Model

Voltage conversion currently assumes:
- 12-bit ADC range of `0..4095`
- ADC reference of `3.3V`
- divider ratio of `(100k + 30k) / 30k`

Implications:
- absolute accuracy depends on resistor tolerance, ADC calibration, and actual ESP32 ADC behavior
- this is suitable for monitoring and alerting, but may need calibration for precise measurement use

## Alert Behavior

Low-voltage email alert logic is edge-triggered with optional reminder repeats.

Behavior:
- first email sends when voltage crosses below the threshold
- no more emails are sent while voltage stays low unless repeat reminders are enabled
- repeat reminders send every configured number of hours while voltage remains low
- when voltage recovers above `threshold + hysteresis`, the alert state rearms
- a new drop after recovery can send a fresh first alert

There is also a short retry gate for the initial send path to avoid rapid repeated send attempts.

Implementation details:
- `lowVoltageAlertSent` is the edge-trigger latch
- `lastEmailAttemptMs` is reused for both first-send retry gating and repeat reminder timing
- alert rearm happens only after voltage rises above `threshold + hysteresis`

## Time Requirement

Direct Gmail SMTP requires valid device time for TLS.

The sketch calls NTP time sync after Wi-Fi connection and also checks time again before sending email. If time is still invalid, email sending is blocked.

NTP configuration is currently hardcoded to:
- `pool.ntp.org`
- `time.nist.gov`

The current implementation treats any epoch above a fixed sanity threshold as valid time.

## Gmail SMTP Prerequisites

For email alerts to work with Gmail, the user must prepare the sender account correctly.

Required setup:
- enable 2-Step Verification on the Gmail account being used as the sender
- create a Google App Password for Mail on that same account
- enter that app password in the device web UI instead of the normal Gmail login password

Important constraints:
- normal Gmail account passwords are not supported for this flow
- the sender address and app password must belong to the same Google account
- authentication failures are usually caused by wrong sender/app-password pairing or missing 2-Step Verification

## Web UI Notes

The self-hosted page is served by `WebServer` after Wi-Fi is already connected. Wi-Fi setup itself is still handled separately by WiFiManager during boot.

That is why:
- the page can reset Wi-Fi credentials
- but it does not directly edit SSID and password fields inside the normal page flow

The dashboard HTML, CSS, and JavaScript are built as one large `String` in firmware.

Implications:
- page changes increase sketch size quickly
- large UI additions can push the build over the default ESP32 partition size
- if build size becomes an issue, the first place to simplify is inline HTML/CSS/JS or SMTP support

## Security Notes

Current security posture is intentionally simple for a local-network device.

Important points:
- the web UI is plain HTTP, not HTTPS
- Gmail app password is stored on-device in `Preferences`
- saved sender and recipient details are displayed on the page
- anyone on the same network who can reach the device can operate the page
- `test_email` and `reset_wifi` are not authenticated beyond LAN access

For a harder setup, likely next steps would be:
- add page authentication or an admin PIN
- hide or mask more stored email metadata
- move static assets off inline strings if the UI keeps growing

## Operational Constraints

Known constraints from the current implementation:
- Gmail support adds significant flash usage because of SMTP and TLS
- default ESP32 partition schemes may be too small once email and UI features accumulate
- history graph data appears only after time sync and hourly sampling begin
- Wi-Fi reset does not wipe app settings or history
- no explicit factory-reset route exists yet for clearing all `Preferences` namespaces