// Main Arduino sketch for voltage monitor
#include <WiFiManager.h> // Install WiFiManager library via Library Manager

void setup() {
  Serial.begin(115200);
  // Create WiFiManager object
  WiFiManager wifiManager;
  // Set custom AP SSID and password (password must be at least 8 chars)
  wifiManager.setConfigPortalTimeout(180); // Optional: auto-close after 3 min
  wifiManager.autoConnect("arduino voltage monitor", "00000000");
  // If you get here, you are connected to WiFi
  Serial.println("WiFi connected!");
}

// Define the analog pin used for voltage sensing
#define VOLTAGE_PIN 34 // Change to your actual analog pin if different

void loop() {
  // Read raw analog value
  int rawValue = analogRead(VOLTAGE_PIN);
  // Convert to voltage (assuming 3.3V reference and 12-bit ADC)
  float voltage = (rawValue / 4095.0) * 3.3;
  Serial.print("Raw ADC: ");
  Serial.print(rawValue);
  Serial.print(" | Voltage: ");
  Serial.print(voltage, 3);
  Serial.println(" V");
  delay(1000); // Read every second
}
