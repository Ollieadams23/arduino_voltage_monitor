

// Main Arduino sketch for voltage monitor
#include <WiFiManager.h> // Install WiFiManager library via Library Manager
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

#define VOLTAGE_PIN 34 // Change to your actual analog pin if different
#define VLED_PIN 2     // GPIO pin for V LED (change if needed)

WebServer server(80);
Preferences preferences;
float lastVoltage = 0.0;
float alertThreshold = 5.0; // Default alert voltage threshold
unsigned long lastReadMs = 0;
const unsigned long READ_INTERVAL_MS = 1000;




void updateLedState() {
  digitalWrite(VLED_PIN, lastVoltage < alertThreshold ? HIGH : LOW);
}

String getVoltageHtml() {
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
  html += "<p>Updates every second.</p>";
  html += "</body></html>";
  return html;
}

void setup() {
  pinMode(VLED_PIN, OUTPUT);
  digitalWrite(VLED_PIN, LOW); // LED off initially for active-high wiring
  Serial.begin(115200);
  // Load alert threshold from Preferences
  preferences.begin("settings", true); // read-only
  alertThreshold = preferences.getFloat("vthresh", alertThreshold); // fallback to current value if not set
  preferences.end();
  // Create WiFiManager object
  WiFiManager wifiManager;
  // Set custom AP SSID and password (password must be at least 8 chars)
  wifiManager.setConfigPortalTimeout(180); // Optional: auto-close after 3 min
  wifiManager.autoConnect("arduino voltage monitor", "00000000");
  // If you get here, you are connected to WiFi
  Serial.println("WiFi connected!");
  Serial.print("Connected! IP address: ");
  Serial.println(WiFi.localIP());

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
      // Save new threshold to Preferences
      preferences.begin("settings", false); // write mode
      preferences.putFloat("vthresh", alertThreshold);
      preferences.end();
      updateLedState();
      server.send(200, "text/plain", String(alertThreshold, 2));
    } else {
      server.send(400, "text/plain", "Missing value");
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
