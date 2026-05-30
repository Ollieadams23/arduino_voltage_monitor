

// Main Arduino sketch for voltage monitor
#include <WiFiManager.h> // Install WiFiManager library via Library Manager
#include <ESP_Mail_Client.h> // Install ESP Mail Client library via Library Manager
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>

#define VOLTAGE_PIN 34 // Change to your actual analog pin if different
#define VLED_PIN 2     // GPIO pin for V LED (change if needed)

WebServer server(80);
Preferences preferences;
SMTPSession smtp;
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

const float ALERT_RESET_HYSTERESIS = 0.20;
const unsigned long EMAIL_RETRY_INTERVAL_MS = 60000;
const unsigned long TIME_SYNC_TIMEOUT_MS = 15000;

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
  preferences.end();
}

void saveThresholdSetting() {
  preferences.begin("settings", false);
  preferences.putFloat("vthresh", alertThreshold);
  preferences.end();
}

void saveEmailSettings(bool enabled, const String& sender, const String& appPassword, const String& recipient) {
  emailAlertsEnabled = enabled;
  emailSender = sender;
  emailAppPassword = appPassword;
  alertRecipient = recipient;

  preferences.begin("settings", false);
  preferences.putBool("mail_on", emailAlertsEnabled);
  preferences.putString("mail_from", emailSender);
  preferences.putString("mail_pass", emailAppPassword);
  preferences.putString("mail_to", alertRecipient);
  preferences.end();

  lowVoltageAlertSent = false;
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
  String html = "<html><head><title>Voltage Monitor</title>";
  html += "<script>";
  html += "function syncThresholdInput(t) { const input = document.getElementById('thresholdInput'); if (document.activeElement !== input) { input.value = t; } }";
  html += "function updateVoltage() {";
  html += "fetch('/voltage').then(r => r.text()).then(v => {document.getElementById('voltage').innerText = v + ' V';});";
  html += "fetch('/threshold').then(r => r.text()).then(t => {document.getElementById('threshold').innerText = t + ' V';syncThresholdInput(t);});";
  html += "} setInterval(updateVoltage, 1000); window.onload = updateVoltage;";
  html += "</script></head><body>";
  html += "<h1>Voltage Monitor</h1>";
  html += "<p>Input Voltage: <span id='voltage'>--</span></p>";
  html += "<p>Alert Threshold: <span id='threshold'>--</span>";
  html += "<form onsubmit=\"event.preventDefault();fetch('/set_threshold?value=";
  html += "' + document.getElementById('thresholdInput').value).then(() => setTimeout(updateVoltage, 100));\">Set Threshold: <input type='number' step='0.01' min='0' max='50' id='thresholdInput' value='" + String(alertThreshold, 2) + "'> V <input type='submit' value='Set'></form></p>";
  html += "<h2>Email Alerts</h2>";
  html += "<p>Status: " + emailStatus + "</p>";
  html += "<p>Sender: " + (emailSender.length() > 0 ? htmlEscape(emailSender) : String("Not set")) + "</p>";
  html += "<p>Recipient: " + (alertRecipient.length() > 0 ? htmlEscape(alertRecipient) : String("Not set")) + "</p>";
  html += "<p>App password stored: " + String(emailAppPassword.length() > 0 ? "Yes" : "No") + "</p>";
  html += "<form method='POST' action='/email_settings'>";
  html += "<p><label><input type='checkbox' name='enabled' value='1'";
  if (emailAlertsEnabled) {
    html += " checked";
  }
  html += "> Enable email alerts</label></p>";
  html += "<p>Gmail address: <input type='email' name='sender' value='" + htmlEscape(emailSender) + "'></p>";
  html += "<p>App password: <input type='password' name='app_password' value=''></p>";
  html += "<p>Leave the password blank to keep the currently stored app password.</p>";
  html += "<p>Alert recipient: <input type='email' name='recipient' value='" + htmlEscape(alertRecipient) + "'></p>";
  html += "<p><input type='submit' value='Save Email Settings'></p>";
  html += "</form>";
  html += "<p><button onclick=\"fetch('/test_email').then(r => r.text()).then(msg => alert(msg));\">Send Test Email</button></p>";
  html += "<p>Updates every second.</p>";
  html += "</body></html>";
  return html;
}

void setup() {
  pinMode(VLED_PIN, OUTPUT);
  digitalWrite(VLED_PIN, LOW); // LED off initially for active-high wiring
  Serial.begin(115200);
  loadSettings();
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

    if (appPassword.length() == 0) {
      appPassword = emailAppPassword;
    }

    saveEmailSettings(server.hasArg("enabled"), sender, appPassword, recipient);
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

  bool isLowVoltage = lastVoltage < alertThreshold;
  if (isLowVoltage) {
    if (!lowVoltageAlertSent && millis() - lastEmailAttemptMs >= EMAIL_RETRY_INTERVAL_MS) {
      lastEmailAttemptMs = millis();
      lowVoltageAlertSent = sendLowVoltageAlert(lastVoltage);
    }
  } else if (lastVoltage > alertThreshold + ALERT_RESET_HYSTERESIS) {
    lowVoltageAlertSent = false;
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
