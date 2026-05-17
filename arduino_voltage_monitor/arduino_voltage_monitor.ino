

// Main Arduino sketch for voltage monitor
#include <WiFiManager.h> // Install WiFiManager library via Library Manager
#include <WiFi.h>
#include <WebServer.h>
#define VOLTAGE_PIN 34 // Change to your actual analog pin if different

WebServer server(80);
float lastVoltage = 0.0;

String getVoltageHtml() {
  String html = "<html><head><title>Voltage Monitor</title>";
  html += "<script>\n";
  html += "function updateVoltage() {\n";
  html += "  fetch('/voltage').then(r => r.text()).then(v => {\n";
  html += "    document.getElementById('voltage').innerText = v + ' V';\n";
  html += "  });\n";
  html += "} setInterval(updateVoltage, 1000); window.onload = updateVoltage;\n";
  html += "</script></head><body>";
  html += "<h1>Voltage Monitor</h1>";
  html += "<p>Input Voltage: <span id='voltage'>--</span></p>";
  html += "<p>Updates every second.</p>";
  html += "</body></html>";
  return html;
}

void setup() {
  Serial.begin(115200);
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
  server.begin();
}







void loop() {
  // Read raw analog value
  int rawValue = analogRead(VOLTAGE_PIN);
  // Convert to voltage at the analog pin (3.3V reference, 12-bit ADC)
  float vOut = (rawValue / 4095.0) * 3.3;
  // Calculate input voltage using voltage divider (100k/30k)
  float vIn = vOut * (100.0 + 30.0) / 30.0; // = vOut * 4.333
  lastVoltage = vIn;
  Serial.print("Raw ADC: ");
  Serial.print(rawValue);
  Serial.print(" | Pin Voltage: ");
  Serial.print(vOut, 3);
  Serial.print(" V | Input Voltage: ");
  Serial.print(vIn, 3);
  Serial.println(" V");

  server.handleClient();
  delay(1000); // Read every second
}
