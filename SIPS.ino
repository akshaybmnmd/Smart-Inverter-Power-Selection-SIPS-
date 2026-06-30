#include "Config.h"
#include "BleCore.h"
#include "AcSensorCore.h"
#include "DisplayDriver.h"

// --- State Machine Enums ---
enum AppState {
  STATE_WAIT_INTERVAL,
  STATE_CONNECT_BMS1,
  STATE_DELAY_BMS1,
  STATE_WAIT_BMS1_DATA,
  STATE_COOLDOWN,
  STATE_CONNECT_BMS2,
  STATE_DELAY_BMS2,
  STATE_WAIT_BMS2_DATA,
  STATE_PROCESS_LOGIC
};

// Instantiate global metrics object
SystemMetrics sysMetrics;

AppState currentState = STATE_CONNECT_BMS1;
unsigned long stateTimer = 0;
unsigned long lastAcRead = 0;

void setup() {
  Serial.begin(115200);
  setupAcSensors();
  setupBLE();
  setupDisplay();
  Serial.println("\n--- System Setup Complete. Waiting for initial interval... ---");
}

void loop() {
  if (millis() - lastAcRead >= 500) {
    readAcSensors();
    lastAcRead = millis();
  }

  switch (currentState) {

    case STATE_WAIT_INTERVAL:
      if (millis() - stateTimer >= READ_INTERVAL_MS) {
        currentState = STATE_CONNECT_BMS1;
      }
      break;

    case STATE_CONNECT_BMS1:
      activeBms = &bms1Data;
      if (connectAndSubscribe(BMS1_MAC)) {
        stateTimer = millis();
        currentState = STATE_DELAY_BMS1;
      } else {
        Serial.printf("[DEBUG %lu] BMS 1 connect failed. Jumping to Cooldown.\n", millis());
        stateTimer = millis();
        currentState = STATE_COOLDOWN;
      }
      break;

    case STATE_DELAY_BMS1:
      if (millis() - stateTimer >= 500) {
        if (triggerBmsRead()) {
          stateTimer = millis();
          currentState = STATE_WAIT_BMS1_DATA;
        } else {
          disconnectBLE();
          stateTimer = millis();
          currentState = STATE_COOLDOWN;
        }
      }
      break;

    case STATE_WAIT_BMS1_DATA:
      if (activeBms->dataReady) {
        disconnectBLE();
        stateTimer = millis();
        currentState = STATE_COOLDOWN;
      } else if (millis() - stateTimer >= TIMEOUT_MS) {
        Serial.printf("[DEBUG %lu] BMS 1 Request Timed Out!\n", millis());
        activeBms->isConnected = false;
        disconnectBLE();
        stateTimer = millis();
        currentState = STATE_COOLDOWN;
      }
      break;

    case STATE_COOLDOWN:
      if (millis() - stateTimer >= COOLDOWN_MS) {
        currentState = STATE_CONNECT_BMS2;
      }
      break;

    case STATE_CONNECT_BMS2:
      activeBms = &bms2Data;
      if (connectAndSubscribe(BMS2_MAC)) {
        stateTimer = millis();
        currentState = STATE_DELAY_BMS2;
      } else {
        Serial.printf("[DEBUG %lu] BMS 2 connect failed. Jumping to Process Logic.\n", millis());
        currentState = STATE_PROCESS_LOGIC;
      }
      break;

    case STATE_DELAY_BMS2:
      if (millis() - stateTimer >= 500) {
        if (triggerBmsRead()) {
          stateTimer = millis();
          currentState = STATE_WAIT_BMS2_DATA;
        } else {
          disconnectBLE();
          currentState = STATE_PROCESS_LOGIC;
        }
      }
      break;

    case STATE_WAIT_BMS2_DATA:
      if (activeBms->dataReady) {
        disconnectBLE();
        currentState = STATE_PROCESS_LOGIC;
      } else if (millis() - stateTimer >= TIMEOUT_MS) {
        Serial.printf("[DEBUG %lu] BMS 2 Request Timed Out!\n", millis());
        activeBms->isConnected = false;
        disconnectBLE();
        currentState = STATE_PROCESS_LOGIC;
      }
      break;

    case STATE_PROCESS_LOGIC:
      evaluateContactorLogic();
      updateDisplay(sysMetrics);
      activeBms = nullptr;
      stateTimer = millis();
      currentState = STATE_WAIT_INTERVAL;
      break;
  }
}

void evaluateContactorLogic() {
  Serial.println("\n--- Evaluating System State ---");
  
  // Define our fault-tolerance window (5 Minutes)
  const unsigned long STALE_TIMEOUT_MS = 5 * 60 * 1000; 
  unsigned long currentMillis = millis();
  
  // Check if data exists AND is newer than 5 minutes
  bool bms1Valid = (bms1Data.lastUpdateTime > 0) && (currentMillis - bms1Data.lastUpdateTime < STALE_TIMEOUT_MS);
  bool bms2Valid = (bms2Data.lastUpdateTime > 0) && (currentMillis - bms2Data.lastUpdateTime < STALE_TIMEOUT_MS);

  if (bms1Valid && bms2Valid) {
    
    // --- GRACE PERIOD WARNING ---
    // If we are disconnected, but still within the 5-minute window, print a warning.
    if (!bms1Data.isConnected || !bms2Data.isConnected) {
      Serial.println("[WARNING] BLE connection lost. Operating on cached BMS data (Grace Period).");
    }

    // 1. Calculations (Using the last known good data)
    sysMetrics.avgSoc = (bms1Data.soc + bms2Data.soc) / 2;
    sysMetrics.socDelta = abs(bms1Data.soc - bms2Data.soc);
    sysMetrics.minVoltage = (bms1Data.voltage < bms2Data.voltage) ? bms1Data.voltage : bms2Data.voltage;
    sysMetrics.voltageDelta = abs(bms1Data.voltage - bms2Data.voltage);
    sysMetrics.peakTemp = (bms1Data.maxTemp > bms2Data.maxTemp) ? bms1Data.maxTemp : bms2Data.maxTemp;
    
    sysMetrics.netCurrent = bms1Data.current + bms2Data.current;
    sysMetrics.netPower = bms1Data.power + bms2Data.power;
    
    // Incorporate AC calculations
    sysMetrics.acVoltage = acVoltage; 
    sysMetrics.acCurrent = acCurrent; 
    sysMetrics.acPower = acVoltage * acCurrent; 

    // 2. Set Status
    if (sysMetrics.netCurrent > 1.0) sysMetrics.status = STATUS_CHARGING;
    else if (sysMetrics.netCurrent < -1.0) sysMetrics.status = STATUS_DISCHARGING;
    else sysMetrics.status = STATUS_IDLE;

    // 3. Print
    Serial.printf("Status: %s | DC Net: %.2fA (%.0fW)\n", statusToString(sysMetrics.status), sysMetrics.netCurrent, sysMetrics.netPower);
    Serial.printf("AC Flow: %.2fA (%.0fVA) @ %.1fV\n", sysMetrics.acCurrent, sysMetrics.acPower, sysMetrics.acVoltage);
    Serial.printf("SoC: %d%% | Temp: %.1fC\n", sysMetrics.avgSoc, sysMetrics.peakTemp);
    
    // -> Contactor switching logic goes here safely <-

  } else {
    // 5 minutes have passed with no data. This is a HARD FAULT.
    sysMetrics.status = STATUS_ERROR;
    Serial.println("[CRITICAL ERROR] BMS Data Timeout (5+ min). Defaulting to safe state.");
    
    // FORCE GRID CONNECTION
    // digitalWrite(CONTACTOR_PIN, LOW); 
  }
  Serial.println("-------------------------------");
}