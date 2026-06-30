#include "AcSensorCore.h"

Adafruit_ADS1115 ads;
float acVoltage = 0.0;
float acCurrent = 0.0;

// Adjust these based on your multimeter readings
float calibrationFactorV = 1.0;
float calibrationFactorI = 1.0;

void setupAcSensors() {
  ads.setGain(GAIN_ONE);                 // 1x gain +/- 4.096V
  ads.setDataRate(RATE_ADS1115_860SPS);  // CRITICAL: Maximize sampling speed
  ads.begin();
}

void readAcSensors() {
  long sumSqV = 0;
  long sumSqI = 0;
  int count = 0;

  unsigned long startMillis = millis();

  // Sample continuously for exactly 40ms (Two 50Hz cycles)
  while (millis() - startMillis < 40) {
    int16_t adc0 = ads.readADC_SingleEnded(0);  // Voltage
    int16_t adc1 = ads.readADC_SingleEnded(1);  // Current

    // Convert to relative voltage (Subtract the 1.65V mid-point bias)
    float v = (adc0 * 0.125 / 1000.0) - 1.65;
    float i = (adc1 * 0.125 / 1000.0) - 1.65;

    sumSqV += (v * v);
    sumSqI += (i * i);
    count++;
  }

  // Prevent divide-by-zero, then calculate RMS
  if (count > 0) {
    acVoltage = sqrt((float)sumSqV / count) * calibrationFactorV;
    acCurrent = sqrt((float)sumSqI / count) * calibrationFactorI;
  }
}