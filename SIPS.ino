#include "Config.h"
#include "BleCore.h"

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

AppState currentState = STATE_WAIT_INTERVAL;
unsigned long stateTimer = 0;

void setup() {
  Serial.begin(115200);
  
  setupBLE();
  
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
    // 1. Capacity Metrics
    int avgSoc = (bms1Data.soc + bms2Data.soc) / 2;
    int socDelta = abs(bms1Data.soc - bms2Data.soc);

    // 2. Voltage Metrics
    float minVoltage = min(bms1Data.voltage, bms2Data.voltage);
    float maxVoltage = max(bms1Data.voltage, bms2Data.voltage);
    float voltageDelta = abs(bms1Data.voltage - bms2Data.voltage);

    // 3. Current & Power Flow
    // In parallel, the net current to/from the inverter is the sum of both BMS currents.
    float totalCurrent = bms1Data.current + bms2Data.current;
    
    // 4. System Status (Idle, Charging, Discharging)
    // Note: Standard JBD protocol usually reports discharge as positive and charge as negative. 
    // We use a 1.0A deadband to prevent fluctuating noise from showing as active charging/discharging.
    const char* systemStatus = "IDLE";
    if (totalCurrent > 1.0) {
      systemStatus = "DISCHARGING (Running on Battery)";
    } else if (totalCurrent < -1.0) {
      systemStatus = "CHARGING (Solar is Charging)";
    }

    // --- Print Telemetry ---
    Serial.printf("System Status  : %s\n", systemStatus);
    Serial.printf("Net Current    : %.2f A\n", totalCurrent);
    Serial.printf("Average SoC    : %d%% (Imbalance: %d%%)\n", avgSoc, socDelta);
    Serial.printf("Pack Voltages  : Min %.2f V | Max %.2f V\n", minVoltage, maxVoltage);
    Serial.printf("Voltage Delta  : %.3f V\n", voltageDelta);
    
    // --- Skeleton Switch Logic ---
    // Example: Drop grid and use battery if SoC is high, voltage is healthy, and packs are balanced
    if (avgSoc > 80 && minVoltage > 25.5 && voltageDelta < 0.5) {
      // Serial.println("ACTION: Disengaging Grid (Switching to Solar/Battery)");
      // digitalWrite(CONTACTOR_PIN, HIGH);
    } 
    // Example: Reconnect grid if battery is low, a cell group sags, or packs become dangerously unbalanced
    else if (avgSoc < 30 || minVoltage <= 24.0 || voltageDelta >= 1.0) {
      // Serial.println("ACTION: Engaging Grid (Failsafe/Charging Mode)");
      // digitalWrite(CONTACTOR_PIN, LOW);
    }

  } else {
    Serial.println("WARNING: BMS Data Missing. Defaulting to safe state.");
    // Force grid connection as a failsafe
    // digitalWrite(CONTACTOR_PIN, LOW);
  }
  Serial.println("-------------------------------");
}