#include "AcSensorCore.h"
#include <Wire.h>  // Required for the I2C Ping

Adafruit_ADS1115 ads;
float acVoltage = 0.0;
float acCurrent = 0.0;
bool adsConnected = false;

// Timer to prevent spamming the I2C bus if the sensor is dead
unsigned long lastAdsRetry = 0;

// Adjust these based on your multimeter readings
float calibrationFactorV = 1.0;
float calibrationFactorI = 1.0;

// --- Helper: Safely Ping the I2C Address ---
bool pingI2C(uint8_t address) {
  Wire.beginTransmission(address);
  // 0 means success (device acknowledged).
  // Any other number means the device is missing/dead.
  return (Wire.endTransmission() == 0);
}

void setupAcSensors() {
  ads.setGain(GAIN_ONE);
  ads.setDataRate(RATE_ADS1115_860SPS);

  if (!ads.begin()) {
    Serial.println("[WARNING] ADS1115 not found at boot! AC Sensing disabled.");
    adsConnected = false;
  } else {
    Serial.println("[INFO] ADS1115 Initialized successfully.");
    adsConnected = true;
  }
}

void readAcSensors() {

  // 1. RECONNECTION LOGIC
  if (!adsConnected) {
    // Try to reconnect once every 10 seconds
    if (millis() - lastAdsRetry > 10000) {
      lastAdsRetry = millis();
      Serial.println("[INFO] Attempting to reconnect to ADS1115...");

      if (ads.begin()) {
        Serial.println("[INFO] ADS1115 Reconnected Successfully!");
        adsConnected = true;
      } else {
        return;  // Still dead, exit early
      }
    } else {
      return;  // Waiting for 10-second timer
    }
  }

  // 2. RUNTIME DISCONNECT CHECK
  // Ping the default ADS1115 address (0x48) before we commit to reading
  if (!pingI2C(0x48)) {
    Serial.println("[ERROR] ADS1115 connection lost during runtime!");
    adsConnected = false;  // Mark as disconnected
    acVoltage = 0.0;       // Zero out values for safety
    acCurrent = 0.0;
    return;  // Abort the read
  }

  // 3. NORMAL READING LOGIC (Safe to proceed)
  long sumSqV = 0;
  long sumSqI = 0;
  int count = 0;

  unsigned long startMillis = millis();

  while (millis() - startMillis < 40) {
    int16_t adc0 = ads.readADC_SingleEnded(0);
    int16_t adc1 = ads.readADC_SingleEnded(1);

    float v = (adc0 * 0.125 / 1000.0) - 1.65;
    float i = (adc1 * 0.125 / 1000.0) - 1.65;

    sumSqV += (v * v);
    sumSqI += (i * i);
    count++;
  }

  if (count > 0) {
    acVoltage = sqrt((float)sumSqV / count) * calibrationFactorV;
    acCurrent = sqrt((float)sumSqI / count) * calibrationFactorI;
  }
}