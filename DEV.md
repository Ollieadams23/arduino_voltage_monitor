# Developer Notes

## Persistence Model

This project stores runtime configuration on the ESP32 using `Preferences`, not in the sketch source.

Namespaces:
- `settings` for threshold and email configuration
- `history` for the rolling timestamped voltage history buffer

Stored on-device:
- alert threshold in the `settings` namespace as `vthresh`
- email enabled flag as `mail_on`
- email sender address as `mail_from`
- email app password as `mail_pass`
- email recipient as `mail_to`
- repeat reminder interval in hours as `mail_rpt_h`
- graph plot interval in minutes as `hist_int_m`
- 48-hour voltage history payload in `history.points`
- stored history point count in `history.count`
- circular-buffer write index in `history.write_idx`
- latest persisted bucket marker in `history.last_bucket`

Implications:
- uploading the same sketch to a different board will not copy settings from another board
- re-uploading the sketch to the same board usually keeps saved settings unless flash or data is erased
- the web page starts with empty email settings on a fresh board

## HTTP Route Surface

Current `WebServer` routes:
- `GET /` renders the dashboard HTML
- `GET /voltage` returns the current measured voltage as plain text
- `GET /threshold` returns the current threshold as plain text
- `GET /history` returns grouped history as JSON with plotted points, plot interval, base interval, total span, and threshold
- `GET /set_threshold?value=...` updates the threshold
- `POST /history_settings` updates the graph plot interval
- `POST /email_settings` updates email configuration and repeat interval
- `GET /test_email` sends a manual test email
- `POST /reset_wifi` clears saved Wi-Fi credentials and reboots

Notes:
- threshold update is currently done with a GET route, not POST
- the page depends on client-side polling for `voltage`, `threshold`, and `history`
- `history_settings` only changes how stored base points are grouped for display; it does not rewrite the saved history buffer
- `test_email` returns a short plain-text success or failure message to the browser, but the outbound email itself is multipart text plus HTML

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
- the `/history` route returns grouped points plus `threshold`, `intervalMinutes`, `baseIntervalMinutes`, and `totalHours`
- email snapshots use the same stored history and the current grouped display interval to build the email summary rows

Implications:
- the graph does not backfill missing past time buckets
- immediately after flashing, the graph fills gradually over time
- if time is not yet synced, timestamped history is not recorded
- the current in-progress 5-minute sample is not persisted to flash until bucket rollover, so a reboot inside the same bucket can lose the in-progress point
- changing the plot interval does not clear history; it only changes how stored base points are grouped for display
- the email snapshot can show fewer than 12 rows if the board has not yet accumulated enough valid timestamped history

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
- alert and test emails now send multipart content: plain-text fallback plus HTML when the library and client support it
- the HTML email includes a styled list of timestamp and voltage rows plus a dashboard link, but the email version is not interactive or scrollable

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

Graph-specific behavior:
- the dashboard chart is drawn client-side on two canvases: a fixed left axis canvas and a horizontally scrollable plot canvas
- the visible chart window stays fixed-width while the plotted content can grow wider than the viewport
- a bottom range input controls the horizontal scroll position
- redraw logic always snaps the graph to the far right so the newest data is visible by default
- the threshold is drawn as a dashed overlay line when a positive threshold is configured

Implications:
- page changes increase sketch size quickly
- large UI additions can push the build over the default ESP32 partition size
- if build size becomes an issue, the first place to simplify is inline HTML/CSS/JS or SMTP support

## Email Rendering Notes

Email sending currently uses Gmail SMTP over SSL on port `465` via `ESP_Mail_Client`.

Message construction:
- `sendVoltageEmail(...)` always sends a plain-text body
- if an HTML body is provided, the same send path adds `message.html.content` so capable mail clients get a richer email
- low-voltage alerts and manual test emails both generate the same style of HTML wrapper and then embed a styled timestamp and voltage summary

Snapshot behavior:
- the plain-text fallback still includes an ASCII-style trend snapshot for clients that strip HTML
- the HTML version renders up to 12 rows containing timestamp, voltage, and a simple proportional bar
- the HTML snapshot uses the same grouped history logic that feeds the dashboard API
- low readings are highlighted against the configured threshold
- because email clients do not run the dashboard JavaScript, the email summary is intentionally static and non-scrollable

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
- history graph data appears only after time sync and 5-minute timestamped sampling begin
- Wi-Fi reset does not wipe app settings or history
- no explicit factory-reset route exists yet for clearing all `Preferences` namespaces
- multipart HTML email increases email-body size compared with the original plain-text alert path