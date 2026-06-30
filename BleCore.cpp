#include "BleCore.h"
#include <NimBLEDevice.h>

// Instantiate the exposed variables
BmsData bms1Data = {1, 0, 0, 0, 0, 0, false, false, {0}, 0};
BmsData bms2Data = {2, 0, 0, 0, 0, 0, false, false, {0}, 0};
BmsData* activeBms = nullptr;

// Private variables (only accessible within this file)
NimBLEClient* pClient = nullptr;
NimBLERemoteCharacteristic* pActiveWriteChar = nullptr; 
uint8_t basicInfoCmd[] = { 0xDD, 0xA5, 0x03, 0x00, 0xFF, 0xFD, 0x77 };

// --- Notification Callback ---
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

// --- Public Functions ---
void setupBLE() {
  NimBLEDevice::init("");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9); 
  pClient = NimBLEDevice::createClient();
}

void disconnectBLE() {
  if (pClient != nullptr) {
    pClient->disconnect();
  }
}

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
  disconnectBLE();
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