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

// --- State Machine Enums ---
enum AppState {
  STATE_WAIT_INTERVAL,
  STATE_CONNECT_BMS1,
  STATE_DELAY_BMS1,  // NEW: Non-blocking wait state
  STATE_WAIT_BMS1_DATA,
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

// Pointer to tell the callback which BMS is currently communicating
BmsData* activeBms = nullptr;

// --- Notification Callback (Contains your parsing logic) ---
static void notifyCB(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
  if (activeBms == nullptr) return;

  // Add incoming chunks to the buffer
  for (size_t i = 0; i < length; i++) {
    if (activeBms->bufferIdx < 64) {
      activeBms->buffer[activeBms->bufferIdx++] = pData[i];
    }
  }

  // Process when footer (0x77) arrives
  if (pData[length - 1] == 0x77) {
    if (activeBms->bufferIdx > 23 && activeBms->buffer[0] == 0xDD && activeBms->buffer[1] == 0x03) {

      // Extract Live Data
      activeBms->voltage = ((activeBms->buffer[4] << 8) | activeBms->buffer[5]) * 0.01;
      int16_t rawCurrent = (activeBms->buffer[6] << 8) | activeBms->buffer[7];
      activeBms->current = rawCurrent * 0.01;
      activeBms->soc = activeBms->buffer[23];

      // Your delta checks
      bool stateChanged = false;
      if (activeBms->soc != activeBms->lastSoC) {
        stateChanged = true;
        activeBms->lastSoC = activeBms->soc;
      }
      if (abs(activeBms->current - activeBms->lastCurrent) > 0.2) {
        stateChanged = true;
        activeBms->lastCurrent = activeBms->current;
      }

      if (stateChanged) {
        Serial.printf("[BMS %d] Updated -> V: %.2f, I: %.2f, SoC: %d%%\n",
                      activeBms->id, activeBms->voltage, activeBms->current, activeBms->soc);
      }

      activeBms->isConnected = true;
      activeBms->dataReady = true;  // Signal the state machine to move on
    }

    // Clear buffer for the next read
    activeBms->bufferIdx = 0;
    memset(activeBms->buffer, 0, 64);
  }
}

void setup() {
  Serial.begin(115200);
  NimBLEDevice::init("");
  pClient = NimBLEDevice::createClient();
  Serial.println("Setup complete. Waiting for interval...");
}

void loop() {
  // readAcSensors(); // Non-blocking calls go here

  // 2. State Machine for BLE Management
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
        currentState = STATE_DELAY_BMS1;  // Move to delay state
      } else {
        currentState = STATE_CONNECT_BMS2;
      }
      break;

    case STATE_DELAY_BMS1:
      if (millis() - stateTimer >= 500) {  // 500ms non-blocking delay
        if (triggerBmsRead()) {
          stateTimer = millis();
          currentState = STATE_WAIT_BMS1_DATA;
        } else {
          pClient->disconnect();
          currentState = STATE_CONNECT_BMS2;
        }
      }
      break;

    case STATE_WAIT_BMS1_DATA:
      if (activeBms->dataReady) {
        pClient->disconnect();
        currentState = STATE_CONNECT_BMS2;
      } else if (millis() - stateTimer >= TIMEOUT_MS) {
        activeBms->isConnected = false;
        pClient->disconnect();
        currentState = STATE_CONNECT_BMS2;
      }
      break;

    case STATE_CONNECT_BMS2:
      activeBms = &bms2Data;
      if (connectAndSubscribe(BMS2_MAC)) {
        stateTimer = millis();
        currentState = STATE_DELAY_BMS2;
      } else {
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
        pClient->disconnect();
        currentState = STATE_PROCESS_LOGIC;
      } else if (millis() - stateTimer >= TIMEOUT_MS) {
        activeBms->isConnected = false;
        pClient->disconnect();
        currentState = STATE_PROCESS_LOGIC;
      }
      break;

    case STATE_PROCESS_LOGIC:
      evaluateContactorLogic();
      activeBms = nullptr;
      pActiveWriteChar = nullptr;  // Clear pointer safely
      stateTimer = millis();
      currentState = STATE_WAIT_INTERVAL;
      break;
  }
}

// --- Helper Functions ---

bool connectAndSubscribe(const std::string& macAddress) {
  activeBms->dataReady = false; 
  activeBms->bufferIdx = 0;
  memset(activeBms->buffer, 0, 64);
  pActiveWriteChar = nullptr; 
  
  // Reverted to PUBLIC based on your old code's success
  NimBLEAddress address(macAddress, BLE_ADDR_PUBLIC); 
  
  if (!pClient->connect(address)) {
    Serial.printf("Failed to connect to %s\n", macAddress.c_str());
    activeBms->isConnected = false;
    return false;
  }

  NimBLERemoteService* pService = pClient->getService(BMS_SERVICE_UUID);
  if (pService) {
    NimBLERemoteCharacteristic* pNotifyChar = pService->getCharacteristic(BMS_CHAR_NOTIFY_UUID);
    pActiveWriteChar = pService->getCharacteristic(BMS_CHAR_WRITE_UUID); // Store for the delay state
    
    if (pNotifyChar && pNotifyChar->canNotify()) {
      pNotifyChar->subscribe(true, notifyCB);
      return true; // Success so far, let the loop move to the Delay state
    } else {
      Serial.println("Notify Characteristic missing or cannot notify.");
    }
  } else {
    Serial.println("Service missing.");
  }
  
  activeBms->isConnected = false;
  pClient->disconnect();
  return false;
}

bool triggerBmsRead() {
  if (pActiveWriteChar && (pActiveWriteChar->canWrite() || pActiveWriteChar->canWriteNoResponse())) {
    // Notice the 'true' at the end - this matches your old code!
    pActiveWriteChar->writeValue(basicInfoCmd, sizeof(basicInfoCmd), true); 
    return true;
  }
  Serial.println("Write Characteristic missing or cannot write.");
  return false;
}

void evaluateContactorLogic() {
  Serial.println("--- Evaluating System State ---");

  if (bms1Data.isConnected && bms2Data.isConnected) {
    int avgSoc = (bms1Data.soc + bms2Data.soc) / 2;
    Serial.printf("Average SoC: %d%%\n", avgSoc);

    // Contactor logic goes here
    // if (avgSoc > 80) { disengageMains(); }
    // else if (avgSoc < 30) { engageMains(); }

  } else {
    Serial.println("Warning: One or both BMS unreachable. Defaulting to safe state.");
    // engageMains(); // Failsafe
  }
  Serial.println("-------------------------------");
}