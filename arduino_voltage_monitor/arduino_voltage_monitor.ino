

// Main Arduino sketch for voltage monitor
#include <WiFiManager.h> // Install WiFiManager library via Library Manager
#include <ESP_Mail_Client.h> // Install ESP Mail Client library via Library Manager
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>
#include <math.h>

#define VOLTAGE_PIN 34 // Change to your actual analog pin if different
#define VLED_PIN 2     // GPIO pin for V LED (change if needed)

const float ALERT_RESET_HYSTERESIS = 0.20;
const unsigned long EMAIL_RETRY_INTERVAL_MS = 60000;
const unsigned long TIME_SYNC_TIMEOUT_MS = 15000;
const int HISTORY_BASE_INTERVAL_MINUTES = 5;
const int HISTORY_TOTAL_HOURS = 48;
const int VOLTAGE_HISTORY_POINTS = (HISTORY_TOTAL_HOURS * 60) / HISTORY_BASE_INTERVAL_MINUTES;
const int HISTORY_INTERVAL_MIN_MINUTES = 5;
const int HISTORY_INTERVAL_MAX_MINUTES = 180;
const int EMAIL_SNAPSHOT_POINTS = 12;

WebServer server(80);
Preferences preferences;
SMTPSession smtp;
struct VoltageHistoryPoint {
  float voltage;
  unsigned long epoch;
};

float lastVoltage = 0.0;
float alertThreshold = 5.0; // Default alert voltage threshold
unsigned long lastReadMs = 0;
const unsigned long READ_INTERVAL_MS = 1000;
unsigned long lastEmailAttemptMs = 0;
bool lowVoltageAlertSent = false;
bool emailAlertsEnabled = false;
String emailSender;
String emailAppPassword;
String alertRecipient;
float repeatAlertHours = 0.0;
VoltageHistoryPoint voltageHistory[VOLTAGE_HISTORY_POINTS];
int voltageHistoryCount = 0;
int voltageHistoryWriteIndex = 0;
unsigned long lastHistoryBucket = 0;
int historyIntervalMinutes = 60;

const char* SMTP_HOST = "smtp.gmail.com";
const int SMTP_PORT = 465;
const char* NTP_SERVER_1 = "pool.ntp.org";
const char* NTP_SERVER_2 = "time.nist.gov";

bool hasEmailCredentials() {
  return emailSender.length() > 0 && emailAppPassword.length() > 0 && alertRecipient.length() > 0;
}

bool isEmailConfigured() {
  return emailAlertsEnabled && hasEmailCredentials();
}

void clearVoltageHistory() {
  for (int index = 0; index < VOLTAGE_HISTORY_POINTS; index++) {
    voltageHistory[index].voltage = 0.0f;
    voltageHistory[index].epoch = 0;
  }
  voltageHistoryCount = 0;
  voltageHistoryWriteIndex = 0;
  lastHistoryBucket = 0;
}

int sanitizeHistoryIntervalMinutes(int minutes) {
  if (minutes < HISTORY_INTERVAL_MIN_MINUTES) {
    return HISTORY_INTERVAL_MIN_MINUTES;
  }
  if (minutes > HISTORY_INTERVAL_MAX_MINUTES) {
    return HISTORY_INTERVAL_MAX_MINUTES;
  }
  if (minutes % HISTORY_BASE_INTERVAL_MINUTES != 0) {
    minutes = ((minutes + HISTORY_BASE_INTERVAL_MINUTES - 1) / HISTORY_BASE_INTERVAL_MINUTES) * HISTORY_BASE_INTERVAL_MINUTES;
  }
  return minutes;
}

unsigned long getHistoryIntervalSeconds() {
  return (unsigned long)HISTORY_BASE_INTERVAL_MINUTES * 60UL;
}

void loadVoltageHistory() {
  clearVoltageHistory();

  preferences.begin("history", true);
  size_t storedBytes = preferences.getBytesLength("points");
  if (storedBytes == sizeof(voltageHistory)) {
    preferences.getBytes("points", voltageHistory, sizeof(voltageHistory));
    voltageHistoryCount = preferences.getInt("count", 0);
    voltageHistoryWriteIndex = preferences.getInt("write_idx", 0);
    lastHistoryBucket = preferences.getULong("last_bucket", 0);
  }
  preferences.end();

  if (voltageHistoryCount < 0 || voltageHistoryCount > VOLTAGE_HISTORY_POINTS) {
    clearVoltageHistory();
    return;
  }

  if (voltageHistoryWriteIndex < 0 || voltageHistoryWriteIndex >= VOLTAGE_HISTORY_POINTS) {
    voltageHistoryWriteIndex = voltageHistoryCount % VOLTAGE_HISTORY_POINTS;
  }
}

void saveVoltageHistory() {
  preferences.begin("history", false);
  preferences.putBytes("points", voltageHistory, sizeof(voltageHistory));
  preferences.putInt("count", voltageHistoryCount);
  preferences.putInt("write_idx", voltageHistoryWriteIndex);
  preferences.putULong("last_bucket", lastHistoryBucket);
  preferences.end();
}

void appendVoltageHistory(float voltage, unsigned long epoch) {
  voltageHistory[voltageHistoryWriteIndex].voltage = voltage;
  voltageHistory[voltageHistoryWriteIndex].epoch = epoch;
  voltageHistoryWriteIndex = (voltageHistoryWriteIndex + 1) % VOLTAGE_HISTORY_POINTS;
  if (voltageHistoryCount < VOLTAGE_HISTORY_POINTS) {
    voltageHistoryCount++;
  }
}

void updateLatestVoltageHistory(float voltage, unsigned long epoch) {
  if (voltageHistoryCount == 0) {
    appendVoltageHistory(voltage, epoch);
    return;
  }

  int latestIndex = (voltageHistoryWriteIndex + VOLTAGE_HISTORY_POINTS - 1) % VOLTAGE_HISTORY_POINTS;
  voltageHistory[latestIndex].voltage = voltage;
  voltageHistory[latestIndex].epoch = epoch;
}

void recordVoltageHistory(float voltage) {
  time_t now = time(nullptr);
  if (now <= 1700000000) {
    return;
  }

  unsigned long currentBucket = (unsigned long)(now / getHistoryIntervalSeconds());
  if (currentBucket != lastHistoryBucket) {
    appendVoltageHistory(voltage, (unsigned long)now);
    lastHistoryBucket = currentBucket;
    saveVoltageHistory();
  } else {
    updateLatestVoltageHistory(voltage, (unsigned long)now);
  }
}

int getHistoryBucketSize() {
  int bucketSize = historyIntervalMinutes / HISTORY_BASE_INTERVAL_MINUTES;
  if (bucketSize < 1) {
    bucketSize = 1;
  }
  return bucketSize;
}

bool getAveragedHistoryPoint(int bucketSize, int displayCount, int displayIndex, float& averagedVoltage, unsigned long& latestEpoch) {
  if (bucketSize < 1 || displayCount <= 0 || displayIndex < 0 || displayIndex >= displayCount) {
    return false;
  }

  int sourceStartIndex = voltageHistoryCount == VOLTAGE_HISTORY_POINTS ? voltageHistoryWriteIndex : 0;
  int sourceOffset = voltageHistoryCount - (displayCount * bucketSize);
  float voltageTotal = 0.0f;
  latestEpoch = 0;

  for (int bucketOffset = 0; bucketOffset < bucketSize; bucketOffset++) {
    int historyOffset = sourceOffset + (displayIndex * bucketSize) + bucketOffset;
    int historyIndex = (sourceStartIndex + historyOffset) % VOLTAGE_HISTORY_POINTS;
    voltageTotal += voltageHistory[historyIndex].voltage;
    latestEpoch = voltageHistory[historyIndex].epoch;
  }

  averagedVoltage = voltageTotal / bucketSize;
  return true;
}

String formatEmailSnapshotTimestamp(unsigned long epoch) {
  if (epoch == 0) {
    return String("unknown");
  }

  time_t timestamp = (time_t)epoch;
  struct tm timeInfo;
  if (localtime_r(&timestamp, &timeInfo) == nullptr) {
    return String(epoch);
  }

  char buffer[20];
  strftime(buffer, sizeof(buffer), "%m/%d %H:%M", &timeInfo);
  return String(buffer);
}

String buildEmailHistorySnapshot() {
  int bucketSize = getHistoryBucketSize();
  int displayCount = voltageHistoryCount / bucketSize;
  if (displayCount <= 0) {
    return String("Recent trend: history not available yet.\r\n");
  }

  int shownCount = displayCount < EMAIL_SNAPSHOT_POINTS ? displayCount : EMAIL_SNAPSHOT_POINTS;
  float snapshotVoltages[EMAIL_SNAPSHOT_POINTS];
  unsigned long snapshotEpochs[EMAIL_SNAPSHOT_POINTS];
  float minVoltage = 0.0f;
  float maxVoltage = 0.0f;

  for (int index = 0; index < shownCount; index++) {
    int displayIndex = displayCount - shownCount + index;
    if (!getAveragedHistoryPoint(bucketSize, displayCount, displayIndex, snapshotVoltages[index], snapshotEpochs[index])) {
      return String("Recent trend: history not available yet.\r\n");
    }

    if (index == 0 || snapshotVoltages[index] < minVoltage) {
      minVoltage = snapshotVoltages[index];
    }
    if (index == 0 || snapshotVoltages[index] > maxVoltage) {
      maxVoltage = snapshotVoltages[index];
    }
  }

  if (fabs(maxVoltage - minVoltage) < 0.2f) {
    maxVoltage += 0.1f;
    minVoltage -= 0.1f;
  }

  String snapshot = "Recent trend (static email snapshot, newest last):\r\n";
  for (int index = 0; index < shownCount; index++) {
    const int barWidth = 18;
    float normalized = (snapshotVoltages[index] - minVoltage) / (maxVoltage - minVoltage);
    if (normalized < 0.0f) {
      normalized = 0.0f;
    }
    if (normalized > 1.0f) {
      normalized = 1.0f;
    }

    int filled = (int)roundf(normalized * barWidth);
    if (filled < 0) {
      filled = 0;
    }
    if (filled > barWidth) {
      filled = barWidth;
    }

    String bar;
    for (int barIndex = 0; barIndex < barWidth; barIndex++) {
      bar += barIndex < filled ? '#' : '-';
    }

    snapshot += formatEmailSnapshotTimestamp(snapshotEpochs[index]);
    snapshot += " | ";
    snapshot += String(snapshotVoltages[index], 3);
    snapshot += " V | ";
    snapshot += bar;
    if (alertThreshold > 0.0f && snapshotVoltages[index] < alertThreshold) {
      snapshot += " LOW";
    }
    snapshot += "\r\n";
  }

  snapshot += "Snapshot scale: ";
  snapshot += String(minVoltage, 2);
  snapshot += " V to ";
  snapshot += String(maxVoltage, 2);
  snapshot += " V\r\n";
  snapshot += "Interactive scrolling is not supported in email clients. Use the web dashboard for the full scrollable graph.\r\n";
  return snapshot;
}

String buildEmailHistorySnapshotHtml() {
  int bucketSize = getHistoryBucketSize();
  int displayCount = voltageHistoryCount / bucketSize;
  if (displayCount <= 0) {
    return String("<p style='margin:16px 0 0;color:#5c685d;'>Recent trend not available yet.</p>");
  }

  float snapshotVoltages[EMAIL_SNAPSHOT_POINTS];
  unsigned long snapshotEpochs[EMAIL_SNAPSHOT_POINTS];
  int shownCount = displayCount < EMAIL_SNAPSHOT_POINTS ? displayCount : EMAIL_SNAPSHOT_POINTS;

  float minVoltage = 0.0f;
  float maxVoltage = 0.0f;

  for (int index = 0; index < shownCount; index++) {
    int displayIndex = displayCount - shownCount + index;
    if (!getAveragedHistoryPoint(bucketSize, displayCount, displayIndex, snapshotVoltages[index], snapshotEpochs[index])) {
      return String("<p style='margin:16px 0 0;color:#5c685d;'>Recent trend not available yet.</p>");
    }

    if (index == 0 || snapshotVoltages[index] < minVoltage) {
      minVoltage = snapshotVoltages[index];
    }
    if (index == 0 || snapshotVoltages[index] > maxVoltage) {
      maxVoltage = snapshotVoltages[index];
    }
  }

  if (fabs(maxVoltage - minVoltage) < 0.2f) {
    maxVoltage += 0.1f;
    minVoltage -= 0.1f;
  }

  String html = "<div style='margin-top:18px;padding:16px;border:1px solid #d5c9ab;border-radius:16px;background:#fffaf0;'>";
  html += "<div style='font:700 14px Arial,sans-serif;color:#1c241d;margin-bottom:10px;'>Recent Trend Snapshot</div>";
  html += "<div style='font:12px Arial,sans-serif;color:#5c685d;margin-bottom:12px;'>Static preview only. Open the dashboard for the full scrollable graph.</div>";
  for (int index = 0; index < shownCount; index++) {
    float normalized = (snapshotVoltages[index] - minVoltage) / (maxVoltage - minVoltage);
    if (normalized < 0.0f) {
      normalized = 0.0f;
    }
    if (normalized > 1.0f) {
      normalized = 1.0f;
    }
    int widthPercent = (int)roundf(normalized * 100.0f);
    if (widthPercent < 4) {
      widthPercent = 4;
    }
    if (widthPercent > 100) {
      widthPercent = 100;
    }

    bool isLow = alertThreshold > 0.0f && snapshotVoltages[index] < alertThreshold;
    html += "<div style='display:flex;align-items:center;gap:10px;margin:8px 0;'>";
    html += "<div style='width:76px;font:12px Arial,sans-serif;color:#5c685d;white-space:nowrap;'>" + htmlEscape(formatEmailSnapshotTimestamp(snapshotEpochs[index])) + "</div>";
    html += "<div style='flex:1;height:12px;background:#efe5ce;border-radius:999px;overflow:hidden;'>";
    html += "<div style='height:12px;width:" + String(widthPercent) + "%;background:" + String(isLow ? "#c65b3d" : "#1e6b52") + ";border-radius:999px;'></div>";
    html += "</div>";
    html += "<div style='width:64px;text-align:right;font:12px Arial,sans-serif;color:#1c241d;white-space:nowrap;'>" + String(snapshotVoltages[index], 3) + " V</div>";
    html += "</div>";
  }

  html += "<div style='margin-top:12px;font:12px Arial,sans-serif;color:#5c685d;'>Scale " + String(minVoltage, 2) + " V to " + String(maxVoltage, 2) + " V";
  if (alertThreshold > 0.0f) {
    html += " | Threshold " + String(alertThreshold, 2) + " V";
  }
  html += "</div></div>";
  return html;
}

String getVoltageHistoryJson() {
  String json = "{\"points\":[";

  int bucketSize = getHistoryBucketSize();

  int displayCount = voltageHistoryCount / bucketSize;

  if (displayCount > 0) {
    for (int displayIndex = 0; displayIndex < displayCount; displayIndex++) {
      float averagedVoltage = 0.0f;
      unsigned long latestEpoch = 0;

      if (!getAveragedHistoryPoint(bucketSize, displayCount, displayIndex, averagedVoltage, latestEpoch)) {
        continue;
      }

      if (displayIndex > 0) {
        json += ",";
      }

      json += "{\"epoch\":" + String(latestEpoch) + ",\"voltage\":" + String(averagedVoltage, 3) + "}";
    }
  }

  json += "],\"maxPoints\":" + String(VOLTAGE_HISTORY_POINTS / bucketSize) + ",\"intervalMinutes\":" + String(historyIntervalMinutes) + ",\"baseIntervalMinutes\":" + String(HISTORY_BASE_INTERVAL_MINUTES) + ",\"totalHours\":" + String(HISTORY_TOTAL_HOURS) + ",\"threshold\":" + String(alertThreshold, 2) + "}";
  return json;
}

unsigned long getRepeatAlertIntervalMs() {
  if (repeatAlertHours <= 0.0f) {
    return 0;
  }

  return (unsigned long)(repeatAlertHours * 3600000.0f);
}

String htmlEscape(const String& value) {
  String escaped = value;
  escaped.replace("&", "&amp;");
  escaped.replace("<", "&lt;");
  escaped.replace(">", "&gt;");
  escaped.replace("\"", "&quot;");
  escaped.replace("'", "&#39;");
  return escaped;
}

void loadSettings() {
  preferences.begin("settings", true);
  alertThreshold = preferences.getFloat("vthresh", alertThreshold);
  emailAlertsEnabled = preferences.getBool("mail_on", false);
  emailSender = preferences.getString("mail_from", "");
  emailAppPassword = preferences.getString("mail_pass", "");
  alertRecipient = preferences.getString("mail_to", "");
  repeatAlertHours = preferences.getFloat("mail_rpt_h", 0.0f);
  historyIntervalMinutes = sanitizeHistoryIntervalMinutes(preferences.getInt("hist_int_m", historyIntervalMinutes));
  preferences.end();
}

void saveHistoryIntervalSetting(int minutes) {
  historyIntervalMinutes = sanitizeHistoryIntervalMinutes(minutes);

  preferences.begin("settings", false);
  preferences.putInt("hist_int_m", historyIntervalMinutes);
  preferences.end();
}

void saveThresholdSetting() {
  preferences.begin("settings", false);
  preferences.putFloat("vthresh", alertThreshold);
  preferences.end();
}

void saveEmailSettings(bool enabled, const String& sender, const String& appPassword, const String& recipient, float repeatHours) {
  emailAlertsEnabled = enabled;
  emailSender = sender;
  emailAppPassword = appPassword;
  alertRecipient = recipient;
  repeatAlertHours = repeatHours < 0.0f ? 0.0f : repeatHours;

  preferences.begin("settings", false);
  preferences.putBool("mail_on", emailAlertsEnabled);
  preferences.putString("mail_from", emailSender);
  preferences.putString("mail_pass", emailAppPassword);
  preferences.putString("mail_to", alertRecipient);
  preferences.putFloat("mail_rpt_h", repeatAlertHours);
  preferences.end();

  lowVoltageAlertSent = false;
  lastEmailAttemptMs = 0;
}

void smtpCallback(SMTP_Status status) {
  Serial.println(status.info());
}

bool ensureSystemTime() {
  time_t now = time(nullptr);
  if (now > 1700000000) {
    return true;
  }

  Serial.println("Syncing time via NTP...");
  configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2);

  unsigned long startMs = millis();
  while (millis() - startMs < TIME_SYNC_TIMEOUT_MS) {
    now = time(nullptr);
    if (now > 1700000000) {
      Serial.print("Time synced: ");
      Serial.println(ctime(&now));
      return true;
    }
    delay(250);
  }

  Serial.println("Time sync failed.");
  return false;
}

bool sendVoltageEmail(const String& subject, const String& body, const String& htmlBody = String()) {
  if (!emailAlertsEnabled) {
    Serial.println("Email alerts are disabled.");
    return false;
  }

  if (!hasEmailCredentials()) {
    Serial.println("Email is enabled but credentials are incomplete.");
    return false;
  }

  if (!ensureSystemTime()) {
    Serial.println("Cannot send email until device time is set.");
    return false;
  }

  Session_Config config;
  config.server.host_name = SMTP_HOST;
  config.server.port = SMTP_PORT;
  config.login.email = emailSender.c_str();
  config.login.password = emailAppPassword.c_str();
  config.login.user_domain = "";

  SMTP_Message message;
  message.sender.name = "ESP32 Voltage Monitor";
  message.sender.email = emailSender.c_str();
  message.subject = subject;
  message.addRecipient("Voltage Monitor Owner", alertRecipient.c_str());
  message.text.content = body.c_str();
  message.text.charSet = "us-ascii";
  message.text.transfer_encoding = Content_Transfer_Encoding::enc_7bit;
  if (htmlBody.length() > 0) {
    message.html.content = htmlBody.c_str();
    message.html.charSet = "utf-8";
    message.html.transfer_encoding = Content_Transfer_Encoding::enc_7bit;
  }
  message.priority = esp_mail_smtp_priority::esp_mail_smtp_priority_high;

  smtp.callback(smtpCallback);
  smtp.setTCPTimeout(10);

  if (!smtp.connect(&config)) {
    Serial.println("SMTP connect failed.");
    return false;
  }

  if (!MailClient.sendMail(&smtp, &message, true)) {
    Serial.print("Email send failed: ");
    Serial.println(smtp.errorReason());
    return false;
  }

  Serial.println("Email alert sent.");
  return true;
}

bool sendLowVoltageAlert(float voltage) {
  String subject = "Voltage Alert: input dropped below threshold";
  String body = "Voltage monitor detected a low-voltage event.\r\n\r\n";
  body += "Measured voltage: " + String(voltage, 3) + " V\r\n";
  body += "Alert threshold: " + String(alertThreshold, 2) + " V\r\n";
  body += "Device IP: " + WiFi.localIP().toString() + "\r\n";
  body += "Dashboard: http://" + WiFi.localIP().toString() + "/\r\n\r\n";
  body += buildEmailHistorySnapshot();
  String html = "<html><body style='margin:0;padding:24px;background:#f3efe4;font-family:Arial,sans-serif;color:#1c241d;'>";
  html += "<div style='max-width:680px;margin:0 auto;background:#fffdf8;border:1px solid #d5c9ab;border-radius:20px;padding:24px;'>";
  html += "<div style='font-size:24px;font-weight:700;margin-bottom:10px;color:#0f4e3a;'>Voltage Alert</div>";
  html += "<div style='font-size:15px;line-height:1.5;color:#39433b;'>Voltage monitor detected a low-voltage event.</div>";
  html += "<div style='margin-top:16px;padding:14px 16px;background:#fff5ef;border:1px solid #e5c2b3;border-radius:14px;'>";
  html += "<div style='margin:4px 0;'><strong>Measured voltage:</strong> " + String(voltage, 3) + " V</div>";
  html += "<div style='margin:4px 0;'><strong>Alert threshold:</strong> " + String(alertThreshold, 2) + " V</div>";
  html += "<div style='margin:4px 0;'><strong>Device IP:</strong> " + htmlEscape(WiFi.localIP().toString()) + "</div>";
  html += "<div style='margin:4px 0;'><strong>Dashboard:</strong> <a href='http://" + WiFi.localIP().toString() + "/' style='color:#1e6b52;'>http://" + htmlEscape(WiFi.localIP().toString()) + "/</a></div>";
  html += "</div>";
  html += buildEmailHistorySnapshotHtml();
  html += "</div></body></html>";
  return sendVoltageEmail(subject, body, html);
}

bool sendTestEmail() {
  String subject = "Voltage Monitor Test Email";
  String body = "This is a manual test email from the ESP32 voltage monitor.\r\n\r\n";
  body += "Current measured voltage: " + String(lastVoltage, 3) + " V\r\n";
  body += "Current alert threshold: " + String(alertThreshold, 2) + " V\r\n";
  body += "Device IP: " + WiFi.localIP().toString() + "\r\n";
  body += "Dashboard: http://" + WiFi.localIP().toString() + "/\r\n\r\n";
  body += buildEmailHistorySnapshot();
  String html = "<html><body style='margin:0;padding:24px;background:#f3efe4;font-family:Arial,sans-serif;color:#1c241d;'>";
  html += "<div style='max-width:680px;margin:0 auto;background:#fffdf8;border:1px solid #d5c9ab;border-radius:20px;padding:24px;'>";
  html += "<div style='font-size:24px;font-weight:700;margin-bottom:10px;color:#0f4e3a;'>Voltage Monitor Test Email</div>";
  html += "<div style='font-size:15px;line-height:1.5;color:#39433b;'>This is a manual test email from the ESP32 voltage monitor.</div>";
  html += "<div style='margin-top:16px;padding:14px 16px;background:#f7f3e8;border:1px solid #d5c9ab;border-radius:14px;'>";
  html += "<div style='margin:4px 0;'><strong>Measured voltage:</strong> " + String(lastVoltage, 3) + " V</div>";
  html += "<div style='margin:4px 0;'><strong>Alert threshold:</strong> " + String(alertThreshold, 2) + " V</div>";
  html += "<div style='margin:4px 0;'><strong>Device IP:</strong> " + htmlEscape(WiFi.localIP().toString()) + "</div>";
  html += "<div style='margin:4px 0;'><strong>Dashboard:</strong> <a href='http://" + WiFi.localIP().toString() + "/' style='color:#1e6b52;'>http://" + htmlEscape(WiFi.localIP().toString()) + "/</a></div>";
  html += "</div>";
  html += buildEmailHistorySnapshotHtml();
  html += "</div></body></html>";
  return sendVoltageEmail(subject, body, html);
}




void updateLedState() {
  digitalWrite(VLED_PIN, lastVoltage < alertThreshold ? HIGH : LOW);
}

String getVoltageHtml() {
  String emailStatus = emailAlertsEnabled ? (hasEmailCredentials() ? "Enabled" : "Enabled, but setup is incomplete") : "Disabled";
  String repeatStatus = repeatAlertHours > 0.0f ? String(repeatAlertHours, 2) + " hour(s)" : "Off";
  String html = "<html><head><title>Voltage Monitor</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += ":root{--bg:#f3efe4;--panel:#fffdf8;--panel-strong:#fff7e4;--text:#1c241d;--muted:#5c685d;--line:#d5c9ab;--accent:#1e6b52;--accent-strong:#0f4e3a;--accent-soft:#d9efe6;--warn:#b86a00;--shadow:0 18px 40px rgba(73,56,24,0.10);}";
  html += "*{box-sizing:border-box;}body{margin:0;font-family:'Aptos','Trebuchet MS','Segoe UI',sans-serif;line-height:1.45;color:var(--text);background:radial-gradient(circle at top left,#fff8ea 0%,#f3efe4 42%,#ece4d2 100%);}";
  html += ".shell{max-width:1120px;margin:0 auto;padding:20px 16px 40px;}";
  html += ".hero{padding:24px;border-radius:24px;background:linear-gradient(135deg,#244636 0%,#3f7554 48%,#9cbf70 100%);color:#f8f5ec;box-shadow:var(--shadow);}";
  html += ".eyebrow{margin:0 0 8px;font-size:0.82rem;letter-spacing:0.16em;text-transform:uppercase;opacity:0.78;}";
  html += "h1{margin:0;font-size:clamp(2rem,4vw,3.2rem);line-height:1.05;}";
  html += ".hero-copy{max-width:700px;margin:12px 0 0;color:rgba(248,245,236,0.84);font-size:1rem;}";
  html += ".hero-stats{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px;margin-top:18px;}";
  html += ".hero-stat{padding:14px 16px;border-radius:18px;background:rgba(255,255,255,0.10);backdrop-filter:blur(4px);border:1px solid rgba(255,255,255,0.16);}";
  html += ".hero-stat-label{display:block;font-size:0.76rem;text-transform:uppercase;letter-spacing:0.12em;opacity:0.72;}";
  html += ".hero-stat-value{display:block;margin-top:6px;font-size:1.25rem;font-weight:700;}";
  html += ".layout{display:grid;grid-template-columns:1.45fr 0.95fr;gap:18px;margin-top:18px;}";
  html += ".stack{display:grid;gap:18px;}";
  html += ".panel{padding:20px;border-radius:22px;background:var(--panel);border:1px solid rgba(125,103,53,0.12);box-shadow:var(--shadow);}";
  html += ".panel-accent{background:var(--panel-strong);}";
  html += ".panel h2{margin:0 0 14px;font-size:1.15rem;}";
  html += ".meta{margin:8px 0 0;color:var(--muted);font-size:0.95rem;}";
  html += ".status-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:12px;margin-bottom:10px;}";
  html += ".status-card{padding:14px 16px;border-radius:18px;background:#faf6eb;border:1px solid var(--line);}";
  html += ".status-card span{display:block;}";
  html += ".status-label{font-size:0.78rem;text-transform:uppercase;letter-spacing:0.08em;color:var(--muted);}";
  html += ".status-value{margin-top:6px;font-size:1.15rem;font-weight:700;}";
  html += ".pill{display:inline-flex;align-items:center;padding:6px 10px;border-radius:999px;font-size:0.9rem;font-weight:700;background:var(--accent-soft);color:var(--accent-strong);}";
  html += ".pill.warn{background:#fde7c4;color:var(--warn);}";
  html += ".form-grid{display:grid;gap:12px;}";
  html += ".field{display:grid;gap:6px;}";
  html += ".field label{font-size:0.92rem;font-weight:700;color:#2e3c31;}";
  html += ".field small{color:var(--muted);font-size:0.84rem;}";
  html += "input[type='email'],input[type='number'],input[type='password']{width:100%;padding:12px 14px;border-radius:14px;border:1px solid var(--line);background:#fffdfa;color:var(--text);font:inherit;}";
  html += "input[type='range']{width:100%;accent-color:var(--accent);}";
  html += "input[type='submit'],button{appearance:none;border:none;border-radius:999px;padding:12px 18px;font:inherit;font-weight:700;cursor:pointer;background:var(--accent);color:#f7f2e7;box-shadow:0 8px 18px rgba(30,107,82,0.22);}";
  html += "input[type='submit']:hover,button:hover{background:var(--accent-strong);}";
  html += ".button-row{display:flex;flex-wrap:wrap;gap:10px;align-items:center;}";
  html += ".button-secondary{background:#e7efe6;color:var(--accent-strong);box-shadow:none;border:1px solid rgba(30,107,82,0.12);}";
  html += ".button-secondary:hover{background:#d6e9df;}";
  html += ".danger{background:#7c3f27;color:#fff5ef;box-shadow:0 8px 18px rgba(124,63,39,0.20);}";
  html += ".danger:hover{background:#5f2d1b;}";
  html += ".stack,.panel{min-width:0;}";
  html += ".check-row{display:flex;align-items:center;gap:10px;padding:12px 14px;border-radius:14px;background:#f8f2e4;border:1px solid var(--line);}input[type='checkbox']{width:18px;height:18px;accent-color:var(--accent);}";
  html += ".chart-summary{display:flex;flex-wrap:wrap;gap:10px;margin:8px 0 14px;}";
  html += ".chart-chip{padding:8px 12px;border-radius:999px;background:#f8f2e4;border:1px solid var(--line);font-size:0.88rem;color:var(--muted);}#historyMeta{color:var(--muted);font-size:0.95rem;}";
  html += ".chart-stage{display:flex;align-items:stretch;gap:0;margin-top:10px;width:100%;}";
  html += ".chart-axis{flex:0 0 58px;width:58px;overflow:hidden;}";
  html += ".chart-scroll{flex:1 1 auto;width:auto;max-width:100%;overflow-x:auto;overflow-y:hidden;padding-bottom:4px;margin-top:0;}";
  html += ".chart-nav{display:grid;gap:6px;margin-top:10px;}";
  html += ".chart-nav-row{display:flex;justify-content:space-between;align-items:center;gap:12px;color:var(--muted);font-size:0.84rem;}";
  html += ".chart-nav input[type='range']{width:100%;margin:0;}";
  html += "canvas{display:block;height:220px;border-radius:18px;border:1px solid var(--line);background:linear-gradient(180deg,#fffdfa 0%,#f6f0df 100%);} .chart-axis canvas{border-right:none;border-top-right-radius:0;border-bottom-right-radius:0;} .chart-scroll canvas{border-top-left-radius:0;border-bottom-left-radius:0;} .note{margin:0;padding:12px 14px;border-radius:14px;background:#f6f0df;color:var(--muted);} .inline-form{display:flex;gap:10px;flex-wrap:wrap;align-items:end;} .inline-form .field{flex:1 1 220px;}";
  html += "@media (max-width: 860px){.layout{grid-template-columns:1fr;}.shell{padding:14px 12px 28px;}.hero{padding:20px;}}";
  html += "</style>";
  html += "<script>";
  html += "function syncThresholdInput(t) { const input = document.getElementById('thresholdInput'); if (document.activeElement !== input) { input.value = t; } }";
  html += "function setTextIfPresent(id, value) { const element = document.getElementById(id); if (element) { element.innerText = value; } }";
  html += "function updateHistoryScrollControls(scrollLeft, maxScroll) { const slider = document.getElementById('historyScrollSlider'); const wrap = document.getElementById('historyScrollNav'); if (!slider || !wrap) return; const hasOverflow = maxScroll > 0; wrap.style.display = hasOverflow ? 'grid' : 'none'; slider.max = hasOverflow ? String(maxScroll) : '0'; slider.value = hasOverflow ? String(Math.min(maxScroll, Math.max(0, Math.round(scrollLeft)))) : '0'; }";
  html += "function attachHistoryScrollSync() { const scroll = document.getElementById('historyScroll'); const slider = document.getElementById('historyScrollSlider'); if (!scroll || !slider || scroll.dataset.sliderBound === '1') return; slider.addEventListener('input', function() { scroll.scrollLeft = parseInt(slider.value || '0', 10); }); scroll.addEventListener('scroll', function() { updateHistoryScrollControls(scroll.scrollLeft, Math.max(0, scroll.scrollWidth - scroll.clientWidth)); }); scroll.dataset.sliderBound = '1'; }";
  html += "function updateHistorySliderLabel(value) { const interval = parseInt(value, 10); const pointCount = Math.max(1, Math.floor((48 * 60) / interval)); setTextIfPresent('historyIntervalValue', interval + ' min averaged buckets'); setTextIfPresent('historySpanValue', pointCount + ' plotted points over the stored 48 h'); setTextIfPresent('historyMode', 'Displaying the last 48 hours as ' + interval + '-minute averaged buckets.'); }";
  html += "function sameDay(epochA, epochB) { const a = new Date(epochA * 1000); const b = new Date(epochB * 1000); return a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth() && a.getDate() === b.getDate(); }";
  html += "function formatHistoryLabel(epoch, includeDate) { const d = new Date(epoch * 1000); const time = d.getHours().toString().padStart(2, '0') + ':' + d.getMinutes().toString().padStart(2, '0'); if (!includeDate) { return time; } return (d.getMonth() + 1) + '/' + d.getDate() + ' ' + time; }";
  html += "function drawHistory(payload) { const axisCanvas = document.getElementById('historyAxis'); const plotCanvas = document.getElementById('historyChart'); const scroll = document.getElementById('historyScroll'); const meta = document.getElementById('historyMeta'); if (!axisCanvas || !plotCanvas || !scroll) return; attachHistoryScrollSync(); const axisCtx = axisCanvas.getContext('2d'); const plotCtx = plotCanvas.getContext('2d'); const ratio = window.devicePixelRatio || 1; const points = payload.points || []; const intervalMinutes = payload.intervalMinutes || 60; const totalHours = payload.totalHours || 48; const threshold = Number(payload.threshold || 0); updateHistorySliderLabel(intervalMinutes); const pointSpacing = points.length > 200 ? 12 : points.length > 120 ? 15 : points.length > 72 ? 20 : 30; const axisWidth = 58; const plotViewportWidth = scroll.clientWidth || 702; const contentWidth = points.length > 1 ? Math.max(plotViewportWidth, 88 + Math.max(1, points.length - 1) * pointSpacing) : plotViewportWidth; const cssHeight = 220; axisCanvas.style.width = axisWidth + 'px'; axisCanvas.style.height = cssHeight + 'px'; axisCanvas.width = axisWidth * ratio; axisCanvas.height = cssHeight * ratio; plotCanvas.style.width = contentWidth + 'px'; plotCanvas.style.height = cssHeight + 'px'; plotCanvas.width = contentWidth * ratio; plotCanvas.height = cssHeight * ratio; axisCtx.setTransform(ratio, 0, 0, ratio, 0, 0); plotCtx.setTransform(ratio, 0, 0, ratio, 0, 0); axisCtx.clearRect(0, 0, axisWidth, cssHeight); plotCtx.clearRect(0, 0, contentWidth, cssHeight); if (!points.length) { plotCtx.fillStyle = '#666'; plotCtx.font = '14px Arial'; plotCtx.fillText('No history yet. The chart fills as the device records each interval.', 16, 32); meta.textContent = 'History empty until the first interval sample is captured.'; updateHistoryScrollControls(0, 0); return; } let minV = points[0].voltage; let maxV = points[0].voltage; for (const point of points) { if (point.voltage < minV) minV = point.voltage; if (point.voltage > maxV) maxV = point.voltage; } if (threshold > 0) { if (threshold < minV) minV = threshold; if (threshold > maxV) maxV = threshold; } if (Math.abs(maxV - minV) < 0.2) { maxV += 0.1; minV -= 0.1; } const pad = { left: 0, right: 18, top: 18, bottom: 46 }; const chartWidth = contentWidth - pad.left - pad.right; const chartHeight = cssHeight - pad.top - pad.bottom; plotCtx.strokeStyle = '#d7d7d7'; plotCtx.lineWidth = 1; plotCtx.beginPath(); for (let i = 0; i <= 4; i++) { const y = pad.top + (chartHeight * i / 4); plotCtx.moveTo(pad.left, y); plotCtx.lineTo(contentWidth - pad.right, y); } plotCtx.stroke(); axisCtx.fillStyle = '#444'; axisCtx.font = '12px Arial'; axisCtx.textAlign = 'right'; for (let i = 0; i <= 4; i++) { const value = maxV - ((maxV - minV) * i / 4); const y = pad.top + (chartHeight * i / 4); axisCtx.fillText(value.toFixed(2) + 'V', axisWidth - 8, y + 4); } if (threshold > 0) { const thresholdY = pad.top + chartHeight - (((threshold - minV) / (maxV - minV)) * chartHeight); plotCtx.save(); plotCtx.setLineDash([6, 4]); plotCtx.strokeStyle = '#c65b3d'; plotCtx.lineWidth = 1.5; plotCtx.beginPath(); plotCtx.moveTo(pad.left, thresholdY); plotCtx.lineTo(contentWidth - pad.right, thresholdY); plotCtx.stroke(); plotCtx.restore(); plotCtx.fillStyle = '#c65b3d'; plotCtx.font = '12px Arial'; plotCtx.fillText('Threshold ' + threshold.toFixed(2) + 'V', 8, Math.max(14, thresholdY - 8)); } plotCtx.strokeStyle = '#1f6feb'; plotCtx.lineWidth = 2; plotCtx.beginPath(); points.forEach((point, index) => { const x = pad.left + (points.length === 1 ? chartWidth / 2 : (chartWidth * index / (points.length - 1))); const normalized = (point.voltage - minV) / (maxV - minV); const y = pad.top + chartHeight - (normalized * chartHeight); if (index === 0) plotCtx.moveTo(x, y); else plotCtx.lineTo(x, y); }); plotCtx.stroke(); plotCtx.fillStyle = '#1f6feb'; const labelStep = Math.max(1, Math.ceil(points.length / Math.max(6, Math.floor(plotViewportWidth / 120)))); points.forEach((point, index) => { const x = pad.left + (points.length === 1 ? chartWidth / 2 : (chartWidth * index / (points.length - 1))); const normalized = (point.voltage - minV) / (maxV - minV); const y = pad.top + chartHeight - (normalized * chartHeight); plotCtx.beginPath(); plotCtx.arc(x, y, 2.5, 0, Math.PI * 2); plotCtx.fill(); const previousEpoch = index > 0 ? points[index - 1].epoch : point.epoch; const dayChanged = index > 0 && !sameDay(point.epoch, previousEpoch); const shouldLabel = index === 0 || index === points.length - 1 || dayChanged || index % labelStep === 0; if (shouldLabel) { plotCtx.fillStyle = '#444'; plotCtx.font = '11px Arial'; plotCtx.fillText(formatHistoryLabel(point.epoch, dayChanged || index === 0 || index === points.length - 1), x - 20, cssHeight - 12); plotCtx.fillStyle = '#1f6feb'; } }); const start = new Date(points[0].epoch * 1000); const end = new Date(points[points.length - 1].epoch * 1000); meta.textContent = 'Showing the last ' + totalHours + ' hour(s) as ' + intervalMinutes + '-minute averages from ' + start.toLocaleString() + ' to ' + end.toLocaleString() + '.'; const maxScroll = Math.max(0, contentWidth - plotViewportWidth); const targetScrollLeft = maxScroll; scroll.scrollLeft = targetScrollLeft; updateHistoryScrollControls(targetScrollLeft, maxScroll); }";
  html += "function updateHistory() { fetch('/history').then(r => r.json()).then(drawHistory); }";
  html += "function updateVoltage() {";
  html += "fetch('/voltage').then(r => r.text()).then(v => { setTextIfPresent('voltage', v + ' V'); setTextIfPresent('voltageMirror', v + ' V'); });";
  html += "fetch('/threshold').then(r => r.text()).then(t => { setTextIfPresent('threshold', t + ' V'); setTextIfPresent('thresholdMirror', t + ' V'); syncThresholdInput(t); });";
  html += "} setInterval(updateVoltage, 1000); setInterval(updateHistory, 60000); window.onload = function(){ updateVoltage(); updateHistory(); }; window.onresize = updateHistory;";
  html += "</script></head><body>";
  html += "<div class='shell'>";
  html += "<header class='hero'>";
  html += "<p class='eyebrow'>ESP32 Telemetry Node</p>";
  html += "<h1>Voltage Monitor</h1>";
  html += "<p class='hero-copy'>Track your input voltage live, review the last 48 hours, and control alerting from one page without reflashing the board.</p>";
  html += "<div class='hero-stats'>";
  html += "<div class='hero-stat'><span class='hero-stat-label'>Current Voltage</span><span class='hero-stat-value' id='voltage'>--</span></div>";
  html += "<div class='hero-stat'><span class='hero-stat-label'>Alert Threshold</span><span class='hero-stat-value' id='threshold'>--</span></div>";
  html += "<div class='hero-stat'><span class='hero-stat-label'>Email Alerts</span><span class='hero-stat-value'>" + emailStatus + "</span></div>";
  html += "</div>";
  html += "</header>";
  html += "<div class='layout'>";
  html += "<div class='stack'>";
  html += "<section class='panel panel-accent'>";
  html += "<h2>History Graph</h2>";
  html += "<p class='meta' id='historyMode'>Displaying the last 48 hours as " + String(historyIntervalMinutes) + "-minute averaged buckets.</p>";
  html += "<div class='chart-summary'><span class='chart-chip' id='historyIntervalValue'>" + String(historyIntervalMinutes) + " min averaged buckets</span><span class='chart-chip' id='historySpanValue'>" + String((HISTORY_TOTAL_HOURS * 60) / historyIntervalMinutes) + " plotted points over the stored 48 h</span><span class='chart-chip'>Threshold overlay enabled</span></div>";
  html += "<div class='field'><label for='historyIntervalSlider'>Plot interval</label><input type='range' id='historyIntervalSlider' min='" + String(HISTORY_INTERVAL_MIN_MINUTES) + "' max='" + String(HISTORY_INTERVAL_MAX_MINUTES) + "' step='" + String(HISTORY_BASE_INTERVAL_MINUTES) + "' value='" + String(historyIntervalMinutes) + "' oninput='updateHistorySliderLabel(this.value)'><small>Move the slider, then save to redraw the chart using a different averaging interval.</small></div>";
  html += "<form method='POST' action='/history_settings'><input type='hidden' id='historyIntervalInput' name='interval_minutes' value='" + String(historyIntervalMinutes) + "'><div class='button-row'><button class='button-secondary' type='submit' onclick=\"document.getElementById('historyIntervalInput').value=document.getElementById('historyIntervalSlider').value;\">Save Plot Interval</button></div></form>";
  html += "<div class='chart-stage'><div class='chart-axis'><canvas id='historyAxis'></canvas></div><div class='chart-scroll' id='historyScroll'><canvas id='historyChart'></canvas></div></div>";
  html += "<div class='chart-nav' id='historyScrollNav'><input type='range' id='historyScrollSlider' min='0' max='0' value='0'><div class='chart-nav-row'><span>Earlier</span><span>Later</span></div></div>";
  html += "<p id='historyMeta' class='meta'>Loading history...</p>";
  html += "</section>";
  html += "<section class='panel'>";
  html += "<h2>Voltage Controls</h2>";
  html += "<div class='status-grid'>";
  html += "<div class='status-card'><span class='status-label'>Live Input</span><span class='status-value' id='voltageMirror'>" + String(lastVoltage, 3) + " V</span></div>";
  html += "<div class='status-card'><span class='status-label'>Threshold</span><span class='status-value' id='thresholdMirror'>" + String(alertThreshold, 2) + " V</span></div>";
  html += "<div class='status-card'><span class='status-label'>LED State</span><span class='status-value'>" + String(lastVoltage < alertThreshold ? "LOW ALERT" : "NORMAL") + "</span></div>";
  html += "</div>";
  html += "<form class='inline-form' onsubmit=\"event.preventDefault();fetch('/set_threshold?value=";
  html += "' + document.getElementById('thresholdInput').value).then(() => setTimeout(function(){ updateVoltage(); }, 100));\">";
  html += "<div class='field'><label for='thresholdInput'>Low-voltage threshold</label><input type='number' step='0.01' min='0' max='50' id='thresholdInput' value='" + String(alertThreshold, 2) + "'><small>Set the point where LED and email alerts should trigger.</small></div>";
  html += "<div class='button-row'><input type='submit' value='Save Threshold'></div>";
  html += "</form>";
  html += "</section>";
  html += "</div>";
  html += "<div class='stack'>";
  html += "<section class='panel'>";
  html += "<h2>Wi-Fi</h2>";
  html += "<div class='status-grid'>";
  html += "<div class='status-card'><span class='status-label'>Connected SSID</span><span class='status-value'>" + htmlEscape(WiFi.SSID()) + "</span></div>";
  html += "<div class='status-card'><span class='status-label'>Device IP</span><span class='status-value'>" + WiFi.localIP().toString() + "</span></div>";
  html += "</div>";
  html += "<p class='note'>Resetting Wi-Fi will clear the saved network and reboot the ESP32 into WiFiManager setup mode on the next boot.</p>";
  html += "<form method='POST' action='/reset_wifi' onsubmit=\"return confirm('Clear saved Wi-Fi credentials and reboot into setup mode?');\">";
  html += "<div class='button-row'><input class='danger' type='submit' value='Reset Wi-Fi Credentials'></div>";
  html += "</form>";
  html += "</section>";
  html += "<section class='panel'>";
  html += "<h2>Email Alerts</h2>";
  html += "<p class='pill" + String(emailAlertsEnabled ? "" : " warn") + "'>" + emailStatus + "</p>";
  html += "<p class='meta'>Sender: " + (emailSender.length() > 0 ? htmlEscape(emailSender) : String("Not set")) + "<br>Recipient: " + (alertRecipient.length() > 0 ? htmlEscape(alertRecipient) : String("Not set")) + "<br>App password stored: " + String(emailAppPassword.length() > 0 ? "Yes" : "No") + "<br>Repeat reminders: " + repeatStatus + "</p>";
  html += "<form class='form-grid' method='POST' action='/email_settings'>";
  html += "<label class='check-row'><input type='checkbox' name='enabled' value='1'";
  if (emailAlertsEnabled) {
    html += " checked";
  }
  html += "><span>Enable email alerts</span></label>";
  html += "<div class='field'><label>Gmail address</label><input type='email' name='sender' value='" + htmlEscape(emailSender) + "'></div>";
  html += "<div class='field'><label>App password</label><input type='password' name='app_password' value=''><small>Leave blank to keep the currently stored app password.</small></div>";
  html += "<div class='field'><label>Alert recipient</label><input type='email' name='recipient' value='" + htmlEscape(alertRecipient) + "'></div>";
  html += "<div class='field'><label>Repeat alert interval in hours</label><input type='number' step='0.25' min='0' name='repeat_hours' value='" + String(repeatAlertHours, 2) + "'><small>Set to 0 to disable repeat reminders while voltage stays low.</small></div>";
  html += "<div class='button-row'><input type='submit' value='Save Email Settings'><button class='button-secondary' type='button' onclick=\"fetch('/test_email').then(r => r.text()).then(msg => alert(msg));\">Send Test Email</button></div>";
  html += "</form>";
  html += "</section>";
  html += "</div>";
  html += "</div>";
  html += "</div></body></html>";
  return html;
}

void setup() {
  pinMode(VLED_PIN, OUTPUT);
  digitalWrite(VLED_PIN, LOW); // LED off initially for active-high wiring
  Serial.begin(115200);
  loadSettings();
  loadVoltageHistory();
  // Create WiFiManager object
  WiFiManager wifiManager;
  // Set custom AP SSID and password (password must be at least 8 chars)
  wifiManager.setConfigPortalTimeout(180); // Optional: auto-close after 3 min
  wifiManager.autoConnect("arduino voltage monitor", "00000000");
  // If you get here, you are connected to WiFi
  Serial.println("WiFi connected!");
  Serial.print("Connected! IP address: ");
  Serial.println(WiFi.localIP());
  MailClient.networkReconnect(true);
  ensureSystemTime();

  // Start web server
  server.on("/", []() {
    server.send(200, "text/html", getVoltageHtml());
  });
  server.on("/voltage", []() {
    server.send(200, "text/plain", String(lastVoltage, 3));
  });
  server.on("/threshold", []() {
    server.send(200, "text/plain", String(alertThreshold, 2));
  });
  server.on("/history", []() {
    server.send(200, "application/json", getVoltageHistoryJson());
  });
  server.on("/history_settings", HTTP_POST, []() {
    int requestedMinutes = sanitizeHistoryIntervalMinutes(server.arg("interval_minutes").toInt());
    saveHistoryIntervalSetting(requestedMinutes);
    server.sendHeader("Location", "/");
    server.send(303, "text/plain", "History interval saved.");
  });
  server.on("/reset_wifi", HTTP_POST, []() {
    server.send(200, "text/plain", "Wi-Fi credentials cleared. Rebooting into setup mode...");
    delay(500);
    WiFi.disconnect(true, true);
    delay(500);
    ESP.restart();
  });
  server.on("/set_threshold", []() {
    if (server.hasArg("value")) {
      alertThreshold = server.arg("value").toFloat();
      saveThresholdSetting();
      updateLedState();
      server.send(200, "text/plain", String(alertThreshold, 2));
    } else {
      server.send(400, "text/plain", "Missing value");
    }
  });
  server.on("/email_settings", HTTP_POST, []() {
    String sender = server.arg("sender");
    sender.trim();
    String appPassword = server.arg("app_password");
    appPassword.trim();
    String recipient = server.arg("recipient");
    recipient.trim();
    float repeatHours = server.arg("repeat_hours").toFloat();

    if (appPassword.length() == 0) {
      appPassword = emailAppPassword;
    }

    saveEmailSettings(server.hasArg("enabled"), sender, appPassword, recipient, repeatHours);
    server.sendHeader("Location", "/");
    server.send(303, "text/plain", "Email settings saved.");
  });
  server.on("/test_email", []() {
    if (sendTestEmail()) {
      server.send(200, "text/plain", "Test email sent. Check your inbox and spam folder.");
    } else {
      server.send(500, "text/plain", "Test email failed. Check Serial output and email settings.");
    }
  });
  server.begin();
}







void loop() {
  server.handleClient();

  if (millis() - lastReadMs < READ_INTERVAL_MS) {
    return;
  }

  lastReadMs = millis();

  // Read raw analog value
  int rawValue = analogRead(VOLTAGE_PIN);
  // Convert to voltage at the analog pin (3.3V reference, 12-bit ADC)
  float vOut = (rawValue / 4095.0) * 3.3;
  // Calculate input voltage using voltage divider (100k/30k)
  float vIn = vOut * (100.0 + 30.0) / 30.0; // = vOut * 4.333
  lastVoltage = vIn;
  updateLedState();
  recordVoltageHistory(lastVoltage);

  bool isLowVoltage = lastVoltage < alertThreshold;
  if (isLowVoltage) {
    bool shouldSendFirstAlert = !lowVoltageAlertSent && (lastEmailAttemptMs == 0 || millis() - lastEmailAttemptMs >= EMAIL_RETRY_INTERVAL_MS);
    bool shouldSendRepeatAlert = lowVoltageAlertSent && getRepeatAlertIntervalMs() > 0 && millis() - lastEmailAttemptMs >= getRepeatAlertIntervalMs();

    if (shouldSendFirstAlert || shouldSendRepeatAlert) {
      lastEmailAttemptMs = millis();
      bool emailSent = sendLowVoltageAlert(lastVoltage);
      if (emailSent) {
        lowVoltageAlertSent = true;
      }
    }
  } else if (lastVoltage > alertThreshold + ALERT_RESET_HYSTERESIS) {
    lowVoltageAlertSent = false;
    lastEmailAttemptMs = 0;
  }

  Serial.print("Raw ADC: ");
  Serial.print(rawValue);
  Serial.print(" | Pin Voltage: ");
  Serial.print(vOut, 3);
  Serial.print(" V | Input Voltage: ");
  Serial.print(vIn, 3);
  Serial.print(" V | Threshold: ");
  Serial.print(alertThreshold, 2);
  Serial.print(" V | VLED: ");
  Serial.println(lastVoltage < alertThreshold ? "ON" : "OFF");
}
