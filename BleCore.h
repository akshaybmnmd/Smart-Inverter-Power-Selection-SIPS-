#ifndef BLE_CORE_H
#define BLE_CORE_H

#include "Config.h"

// Expose the struct instances to the main program
extern BmsData bms1Data;
extern BmsData bms2Data;
extern BmsData* activeBms;

// Expose the core BLE functions
void setupBLE();
void disconnectBLE();
bool connectAndSubscribe(const std::string& macAddress);
bool triggerBmsRead();

#endif