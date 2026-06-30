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
    int avgSoc = (bms1Data.soc + bms2Data.soc) / 2;
    Serial.printf("Average SoC: %d%%\n", avgSoc);
  } else {
    Serial.println("Warning: One or both BMS unreachable. Defaulting to safe state.");
  }
  Serial.println("-------------------------------");
}