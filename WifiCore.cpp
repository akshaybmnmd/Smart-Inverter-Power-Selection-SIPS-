#include "WifiCore.h"

const char* ssid = "2.4G";
const char* password = "123456789";

void setupWifiAndOTA() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected.");

  // OTA Setup
  ArduinoOTA.onStart([]() { Serial.println("OTA Update Starting..."); });
  ArduinoOTA.onEnd([]() { Serial.println("\nOTA Update Finished."); });
  ArduinoOTA.begin();
}

void handleWifiAndOTA() {
  // This non-blocking function checks for incoming update packets
  ArduinoOTA.handle();
}