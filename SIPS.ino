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

  if (bms1Data.isConnected && bms2Data.isConnected) {
    // 1. Calculations
    sysMetrics.avgSoc = (bms1Data.soc + bms2Data.soc) / 2;
    sysMetrics.socDelta = abs(bms1Data.soc - bms2Data.soc);
    sysMetrics.minVoltage = min(bms1Data.voltage, bms2Data.voltage);
    sysMetrics.voltageDelta = abs(bms1Data.voltage - bms2Data.voltage);
    sysMetrics.peakTemp = max(bms1Data.maxTemp, bms2Data.maxTemp);
    sysMetrics.netCurrent = bms1Data.current + bms2Data.current;
    sysMetrics.netPower = bms1Data.power + bms2Data.power;

    sysMetrics.acVoltage = acVoltage; // Extracted from AcSensorCore
    sysMetrics.acCurrent = acCurrent; 
    sysMetrics.acPower = acVoltage * acCurrent; // Apparent power (VA)

    // Set Status
    if (sysMetrics.netCurrent > 1.0) sysMetrics.status = STATUS_CHARGING;
    else if (sysMetrics.netCurrent < -1.0) sysMetrics.status = STATUS_DISCHARGING;
    else sysMetrics.status = STATUS_IDLE;

    // Print with AC Data
    Serial.printf("\nStatus: %s | DC Net: %.2fA (%.0fW)\n", statusToString(sysMetrics.status), sysMetrics.netCurrent, sysMetrics.netPower);
    Serial.printf("AC Flow: %.2fA (%.0fVA) @ %.1fV\n", sysMetrics.acCurrent, sysMetrics.acPower, sysMetrics.acVoltage);
    Serial.printf("SoC: %d%% | Temp: %.1fC\n", sysMetrics.avgSoc, sysMetrics.peakTemp);

    // --- Advanced Switch Logic ---
    // Thermal limit check (e.g., standard lithium limit is ~55C, let's play it safe at 45C)
    if (sysMetrics.peakTemp >= 45.0) {
      Serial.println("ACTION: THERMAL ALARM! Engaging Grid to remove load from batteries.");
      // digitalWrite(CONTACTOR_PIN, LOW);
    }
    // High SoC, healthy voltage, balanced, and cool -> Switch to Solar/Battery
    else if (sysMetrics.avgSoc > 80 && sysMetrics.minVoltage > 25.5 && sysMetrics.voltageDelta < 0.5) {
      Serial.println("ACTION: Disengaging Grid (Switching to Solar/Battery)");
      // digitalWrite(CONTACTOR_PIN, HIGH);
    }
    // Low SoC, sagging voltage, or large imbalance -> Return to Grid
    else if (sysMetrics.avgSoc < 30 || sysMetrics.minVoltage <= 24.0 || sysMetrics.voltageDelta >= 1.0) {
      Serial.println("ACTION: Engaging Grid (Failsafe/Charging Mode)");
      // digitalWrite(CONTACTOR_PIN, LOW);
    }

  } else {
    Serial.println("WARNING: BMS Data Missing. Defaulting to safe state.");
    // Force grid connection as a failsafe
    // digitalWrite(CONTACTOR_PIN, LOW);
  }
  Serial.println("-------------------------------");
}