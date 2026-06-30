#include "Config.h"
#include "BleCore.h"
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

AppState currentState = STATE_WAIT_INTERVAL;
unsigned long stateTimer = 0;

void setup() {
  Serial.begin(115200);
  setupBLE();
  setupDisplay();
  Serial.println("\n--- System Setup Complete. Waiting for initial interval... ---");
}

void loop() {
  // readAcSensors(); // Free-running loop for AC sensors (coming soon)

  switch (currentState) {

    case STATE_WAIT_INTERVAL:
      if (millis() - stateTimer >= READ_INTERVAL_MS) {
        Serial.printf("\n[DEBUG %lu] Interval triggered. Moving to BMS 1.\n", millis());
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
        Serial.printf("[DEBUG %lu] BMS 1 Data complete. Disconnecting...\n", millis());
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
        Serial.printf("[DEBUG %lu] Radio cooldown complete. Moving to BMS 2.\n", millis());
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
        Serial.printf("[DEBUG %lu] BMS 2 Data complete. Disconnecting...\n", millis());
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
      Serial.printf("[DEBUG %lu] Cycle complete. Waiting %dms for next interval.\n", millis(), READ_INTERVAL_MS);
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

    // 2. Set Status Enum
    if (sysMetrics.netCurrent > 1.0) sysMetrics.status = STATUS_CHARGING;
    else if (sysMetrics.netCurrent < -1.0) sysMetrics.status = STATUS_DISCHARGING;
    else sysMetrics.status = STATUS_IDLE;

    // --- Print Telemetry ---
    Serial.printf("System Status  : %s\n", sysMetrics.status);
    Serial.printf("Net Flow       : %.2f A (%.0f Watts)\n", sysMetrics.netCurrent, sysMetrics.netPower);
    Serial.printf("Average SoC    : %d%% (Imbalance: %d%%)\n", sysMetrics.avgSoc, sysMetrics.socDelta);
    Serial.printf("Min Voltage    : %.2f V (Delta: %.3f V)\n", sysMetrics.minVoltage, sysMetrics.voltageDelta);
    Serial.printf("Peak Temp      : %.1f °C\n", sysMetrics.peakTemp);

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