#include <NimBLEDevice.h>

// --- Configuration ---
// Replace with your actual BMS MAC addresses
const std::string BMS1_MAC = "a5:c2:39:1d:e6:2e";
const std::string BMS2_MAC = "a5:c2:39:1d:e5:9b";

// Replace with your BMS specific UUIDs
const char* BMS_SERVICE_UUID = "FF00";
const char* BMS_CHAR_NOTIFY_UUID = "FF01";
const char* BMS_CHAR_WRITE_UUID = "FF02";

// The command to trigger live data
uint8_t basicInfoCmd[] = { 0xDD, 0xA5, 0x03, 0x00, 0xFF, 0xFD, 0x77 };

const unsigned long READ_INTERVAL_MS = 30000;  // 30 seconds
const unsigned long TIMEOUT_MS = 2000;         // 2 seconds max to wait for a response
const unsigned long COOLDOWN_MS = 1000;

// --- State Machine Enums ---
enum AppState {
  STATE_WAIT_INTERVAL,
  STATE_CONNECT_BMS1,
  STATE_DELAY_BMS1,  // NEW: Non-blocking wait state
  STATE_WAIT_BMS1_DATA,
  STATE_COOLDOWN,
  STATE_CONNECT_BMS2,
  STATE_DELAY_BMS2,  // NEW: Non-blocking wait state
  STATE_WAIT_BMS2_DATA,
  STATE_PROCESS_LOGIC
};

AppState currentState = STATE_WAIT_INTERVAL;
unsigned long stateTimer = 0;
NimBLEClient* pClient = nullptr;
NimBLERemoteCharacteristic* pActiveWriteChar = nullptr;  // Globally track the write channel

// --- Data Storage ---
struct BmsData {
  uint8_t id;
  float voltage;
  float current;
  float lastCurrent;
  int soc;
  int lastSoC;
  bool isConnected;
  bool dataReady;
  uint8_t buffer[64];
  size_t bufferIdx;
};

BmsData bms1Data = { 1, 0, 0, 0, 0, 0, false, false, { 0 }, 0 };
BmsData bms2Data = { 2, 0, 0, 0, 0, 0, false, false, { 0 }, 0 };
BmsData* activeBms = nullptr;

// --- Notification Callback (Contains your parsing logic) ---
static void notifyCB(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
  if (activeBms == nullptr) return;

  Serial.printf("[DEBUG %lu] Notification chunk received: %d bytes\n", millis(), length);

  for (size_t i = 0; i < length; i++) {
    if (activeBms->bufferIdx < 64) activeBms->buffer[activeBms->bufferIdx++] = pData[i];
  }

  if (pData[length - 1] == 0x77) {
    Serial.printf("[DEBUG %lu] Footer 0x77 detected. Processing payload...\n", millis());
    if (activeBms->bufferIdx > 23 && activeBms->buffer[0] == 0xDD && activeBms->buffer[1] == 0x03) {
      activeBms->voltage = ((activeBms->buffer[4] << 8) | activeBms->buffer[5]) * 0.01;
      int16_t rawCurrent = (activeBms->buffer[6] << 8) | activeBms->buffer[7];
      activeBms->current = rawCurrent * 0.01;
      activeBms->soc = activeBms->buffer[23];
      
      Serial.printf(">>> [BMS %d] V: %.2f, I: %.2f, SoC: %d%% <<<\n", activeBms->id, activeBms->voltage, activeBms->current, activeBms->soc);
      
      activeBms->isConnected = true;
      activeBms->dataReady = true; 
    } else {
      Serial.printf("[DEBUG %lu] Payload failed header/length validation.\n", millis());
    }
    activeBms->bufferIdx = 0;
    memset(activeBms->buffer, 0, 64);
  }
}

void setup() {
  Serial.begin(115200);
  NimBLEDevice::init("");
  
  // Power up the BLE radio
  NimBLEDevice::setPower(ESP_PWR_LVL_P9); 
  
  pClient = NimBLEDevice::createClient();
  Serial.println("\n--- System Setup Complete. Waiting for initial interval... ---");
}

void loop() {
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
            pClient->disconnect();
            stateTimer = millis();
            currentState = STATE_COOLDOWN;
         }
      }
      break;

    case STATE_WAIT_BMS1_DATA:
      if (activeBms->dataReady) {
        Serial.printf("[DEBUG %lu] BMS 1 Data complete. Disconnecting...\n", millis());
        pClient->disconnect();
        stateTimer = millis();
        currentState = STATE_COOLDOWN;
      } else if (millis() - stateTimer >= TIMEOUT_MS) {
        Serial.printf("[DEBUG %lu] BMS 1 Request Timed Out!\n", millis());
        activeBms->isConnected = false;
        pClient->disconnect();
        stateTimer = millis();
        currentState = STATE_COOLDOWN;
      }
      break;

    // --- NEW COOLDOWN STATE ---
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
            pClient->disconnect();
            currentState = STATE_PROCESS_LOGIC;
         }
      }
      break;

    case STATE_WAIT_BMS2_DATA:
      if (activeBms->dataReady) {
        Serial.printf("[DEBUG %lu] BMS 2 Data complete. Disconnecting...\n", millis());
        pClient->disconnect();
        currentState = STATE_PROCESS_LOGIC;
      } else if (millis() - stateTimer >= TIMEOUT_MS) {
        Serial.printf("[DEBUG %lu] BMS 2 Request Timed Out!\n", millis());
        activeBms->isConnected = false;
        pClient->disconnect();
        currentState = STATE_PROCESS_LOGIC;
      }
      break;

    case STATE_PROCESS_LOGIC:
      evaluateContactorLogic();
      activeBms = nullptr;
      pActiveWriteChar = nullptr; 
      stateTimer = millis(); 
      Serial.printf("[DEBUG %lu] Cycle complete. Waiting %dms for next interval.\n", millis(), READ_INTERVAL_MS);
      currentState = STATE_WAIT_INTERVAL;
      break;
  }
}

// --- Helper Functions ---

bool connectAndSubscribe(const std::string& macAddress) {
  Serial.printf("[DEBUG %lu] === Attempting connection to %s ===\n", millis(), macAddress.c_str());
  
  activeBms->dataReady = false; 
  activeBms->bufferIdx = 0;
  memset(activeBms->buffer, 0, 64);
  pActiveWriteChar = nullptr; 
  
  NimBLEAddress address(macAddress, BLE_ADDR_PUBLIC); 
  
  if (!pClient->connect(address)) {
    Serial.printf("[ERROR %lu] pClient->connect() failed entirely.\n", millis());
    activeBms->isConnected = false;
    return false;
  }
  Serial.printf("[DEBUG %lu] Connected! Fetching service %s...\n", millis(), BMS_SERVICE_UUID);

  NimBLERemoteService* pService = pClient->getService(BMS_SERVICE_UUID);
  if (pService != nullptr) {
    Serial.printf("[DEBUG %lu] Service found. Getting characteristics...\n", millis());
    
    NimBLERemoteCharacteristic* pNotifyChar = pService->getCharacteristic(BMS_CHAR_NOTIFY_UUID);
    pActiveWriteChar = pService->getCharacteristic(BMS_CHAR_WRITE_UUID); 
    
    if (pNotifyChar != nullptr && pNotifyChar->canNotify()) {
      Serial.printf("[DEBUG %lu] Subscribing to notifications on %s...\n", millis(), BMS_CHAR_NOTIFY_UUID);
      pNotifyChar->subscribe(true, notifyCB);
      return true; 
    } else {
      Serial.printf("[ERROR %lu] Notify Characteristic %s missing or cannot notify.\n", millis(), BMS_CHAR_NOTIFY_UUID);
    }
  } else {
    Serial.printf("[ERROR %lu] Service %s not found on this device.\n", millis(), BMS_SERVICE_UUID);
  }
  
  activeBms->isConnected = false;
  Serial.printf("[DEBUG %lu] Disconnecting due to service/char failure.\n", millis());
  pClient->disconnect();
  return false;
}

bool triggerBmsRead() {
  if (pActiveWriteChar != nullptr && (pActiveWriteChar->canWrite() || pActiveWriteChar->canWriteNoResponse())) {
    Serial.printf("[DEBUG %lu] Writing 0xDD trigger command to %s...\n", millis(), BMS_CHAR_WRITE_UUID);
    pActiveWriteChar->writeValue(basicInfoCmd, sizeof(basicInfoCmd), true); 
    return true;
  }
  Serial.printf("[ERROR %lu] Cannot write to characteristic %s!\n", millis(), BMS_CHAR_WRITE_UUID);
  return false;
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