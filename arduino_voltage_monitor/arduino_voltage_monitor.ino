

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

String getVoltageHistoryJson() {
  String json = "{\"points\":[";

  int bucketSize = historyIntervalMinutes / HISTORY_BASE_INTERVAL_MINUTES;
  if (bucketSize < 1) {
    bucketSize = 1;
  }

  int displayCount = voltageHistoryCount / bucketSize;

  if (displayCount > 0) {
    int sourceStartIndex = voltageHistoryCount == VOLTAGE_HISTORY_POINTS ? voltageHistoryWriteIndex : 0;
    int sourceOffset = voltageHistoryCount - (displayCount * bucketSize);

    for (int displayIndex = 0; displayIndex < displayCount; displayIndex++) {
      float voltageTotal = 0.0f;
      unsigned long latestEpoch = 0;

      for (int bucketOffset = 0; bucketOffset < bucketSize; bucketOffset++) {
        int historyOffset = sourceOffset + (displayIndex * bucketSize) + bucketOffset;
        int historyIndex = (sourceStartIndex + historyOffset) % VOLTAGE_HISTORY_POINTS;
        voltageTotal += voltageHistory[historyIndex].voltage;
        latestEpoch = voltageHistory[historyIndex].epoch;
      }

      if (displayIndex > 0) {
        json += ",";
      }

      float averagedVoltage = voltageTotal / bucketSize;
      json += "{\"epoch\":" + String(latestEpoch) + ",\"voltage\":" + String(averagedVoltage, 3) + "}";
    }
  }

  json += "],\"maxPoints\":" + String(VOLTAGE_HISTORY_POINTS / bucketSize) + ",\"intervalMinutes\":" + String(historyIntervalMinutes) + ",\"baseIntervalMinutes\":" + String(HISTORY_BASE_INTERVAL_MINUTES) + ",\"totalHours\":" + String(HISTORY_TOTAL_HOURS) + "}";
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

bool sendVoltageEmail(const String& subject, const String& body) {
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
  return sendVoltageEmail(subject, body);
}

bool sendTestEmail() {
  String subject = "Voltage Monitor Test Email";
  String body = "This is a manual test email from the ESP32 voltage monitor.\r\n\r\n";
  body += "Current measured voltage: " + String(lastVoltage, 3) + " V\r\n";
  body += "Current alert threshold: " + String(alertThreshold, 2) + " V\r\n";
  body += "Device IP: " + WiFi.localIP().toString() + "\r\n";
  return sendVoltageEmail(subject, body);
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
  html += ".check-row{display:flex;align-items:center;gap:10px;padding:12px 14px;border-radius:14px;background:#f8f2e4;border:1px solid var(--line);}input[type='checkbox']{width:18px;height:18px;accent-color:var(--accent);}";
  html += "canvas{width:100%;max-width:100%;height:260px;border-radius:18px;border:1px solid var(--line);background:linear-gradient(180deg,#fffdfa 0%,#f6f0df 100%);}#historyMeta{color:var(--muted);font-size:0.95rem;}";
  html += ".note{margin:0;padding:12px 14px;border-radius:14px;background:#f6f0df;color:var(--muted);} .inline-form{display:flex;gap:10px;flex-wrap:wrap;align-items:end;} .inline-form .field{flex:1 1 220px;}";
  html += "@media (max-width: 860px){.layout{grid-template-columns:1fr;}.shell{padding:14px 12px 28px;}.hero{padding:20px;}}";
  html += "</style>";
  html += "<script>";
  html += "function syncThresholdInput(t) { const input = document.getElementById('thresholdInput'); if (document.activeElement !== input) { input.value = t; } }";
  html += "function setTextIfPresent(id, value) { const element = document.getElementById(id); if (element) { element.innerText = value; } }";
  html += "function updateHistorySliderLabel(value) { const interval = parseInt(value, 10); const pointCount = Math.max(1, Math.floor((48 * 60) / interval)); setTextIfPresent('historyIntervalValue', interval + ' min'); setTextIfPresent('historySpanValue', pointCount + ' plotted points across 48 h'); }";
  html += "function formatHistoryLabel(epoch) { const d = new Date(epoch * 1000); return d.getHours().toString().padStart(2, '0') + ':00'; }";
  html += "function drawHistory(payload) { const canvas = document.getElementById('historyChart'); const meta = document.getElementById('historyMeta'); if (!canvas) return; const ctx = canvas.getContext('2d'); const ratio = window.devicePixelRatio || 1; const cssWidth = canvas.clientWidth || 960; const cssHeight = canvas.clientHeight || 240; canvas.width = cssWidth * ratio; canvas.height = cssHeight * ratio; ctx.setTransform(ratio, 0, 0, ratio, 0, 0); ctx.clearRect(0, 0, cssWidth, cssHeight); const points = payload.points || []; const intervalMinutes = payload.intervalMinutes || 60; const totalHours = payload.totalHours || 48; updateHistorySliderLabel(intervalMinutes); if (!points.length) { ctx.fillStyle = '#666'; ctx.font = '14px Arial'; ctx.fillText('No history yet. The chart fills as the device records each interval.', 16, 32); meta.textContent = 'History empty until the first interval sample is captured.'; return; } let minV = points[0].voltage; let maxV = points[0].voltage; for (const point of points) { if (point.voltage < minV) minV = point.voltage; if (point.voltage > maxV) maxV = point.voltage; } if (Math.abs(maxV - minV) < 0.2) { maxV += 0.1; minV -= 0.1; } const pad = { left: 46, right: 14, top: 18, bottom: 32 }; const chartWidth = cssWidth - pad.left - pad.right; const chartHeight = cssHeight - pad.top - pad.bottom; ctx.strokeStyle = '#d7d7d7'; ctx.lineWidth = 1; ctx.beginPath(); for (let i = 0; i <= 4; i++) { const y = pad.top + (chartHeight * i / 4); ctx.moveTo(pad.left, y); ctx.lineTo(cssWidth - pad.right, y); } ctx.stroke(); ctx.fillStyle = '#444'; ctx.font = '12px Arial'; for (let i = 0; i <= 4; i++) { const value = maxV - ((maxV - minV) * i / 4); const y = pad.top + (chartHeight * i / 4); ctx.fillText(value.toFixed(2) + 'V', 4, y + 4); } ctx.strokeStyle = '#1f6feb'; ctx.lineWidth = 2; ctx.beginPath(); points.forEach((point, index) => { const x = pad.left + (points.length === 1 ? chartWidth / 2 : (chartWidth * index / (points.length - 1))); const normalized = (point.voltage - minV) / (maxV - minV); const y = pad.top + chartHeight - (normalized * chartHeight); if (index === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y); }); ctx.stroke(); ctx.fillStyle = '#1f6feb'; points.forEach((point, index) => { const x = pad.left + (points.length === 1 ? chartWidth / 2 : (chartWidth * index / (points.length - 1))); const normalized = (point.voltage - minV) / (maxV - minV); const y = pad.top + chartHeight - (normalized * chartHeight); ctx.beginPath(); ctx.arc(x, y, 2.5, 0, Math.PI * 2); ctx.fill(); if (index === 0 || index === points.length - 1 || index % 12 === 0) { ctx.fillStyle = '#444'; ctx.fillText(formatHistoryLabel(point.epoch), x - 14, cssHeight - 10); ctx.fillStyle = '#1f6feb'; } }); const start = new Date(points[0].epoch * 1000); const end = new Date(points[points.length - 1].epoch * 1000); meta.textContent = 'Showing up to the last ' + totalHours + ' hour(s), grouped into ' + intervalMinutes + '-minute averages from ' + start.toLocaleString() + ' to ' + end.toLocaleString() + '.'; }";
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
  html += "<div class='field'><label for='historyIntervalSlider'>Plot interval</label><input type='range' id='historyIntervalSlider' min='" + String(HISTORY_INTERVAL_MIN_MINUTES) + "' max='" + String(HISTORY_INTERVAL_MAX_MINUTES) + "' step='" + String(HISTORY_BASE_INTERVAL_MINUTES) + "' value='" + String(historyIntervalMinutes) + "' oninput='updateHistorySliderLabel(this.value)'><small><span id='historyIntervalValue'>" + String(historyIntervalMinutes) + " min</span> per point, <span id='historySpanValue'>" + String((HISTORY_TOTAL_HOURS * 60) / historyIntervalMinutes) + " plotted points across 48 h</span>.</small></div>";
  html += "<form method='POST' action='/history_settings'><input type='hidden' id='historyIntervalInput' name='interval_minutes' value='" + String(historyIntervalMinutes) + "'><div class='button-row'><button class='button-secondary' type='submit' onclick=\"document.getElementById('historyIntervalInput').value=document.getElementById('historyIntervalSlider').value;\">Save Plot Interval</button></div></form>";
  html += "<canvas id='historyChart'></canvas>";
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
