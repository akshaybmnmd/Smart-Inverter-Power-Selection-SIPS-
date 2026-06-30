#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <string>

// --- Hardware Addresses ---
const std::string BMS1_MAC = "a5:c2:39:1d:e6:2e"; 
const std::string BMS2_MAC = "a5:c2:39:1d:e5:9b"; 

// --- UUIDs ---
const char* const BMS_SERVICE_UUID     = "FF00"; 
const char* const BMS_CHAR_NOTIFY_UUID = "FF01"; 
const char* const BMS_CHAR_WRITE_UUID  = "FF02"; 

// --- Timings ---
const unsigned long READ_INTERVAL_MS = 30000; 
const unsigned long TIMEOUT_MS = 2000; 
const unsigned long COOLDOWN_MS = 500; 

// --- Data Structures ---
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

#endif