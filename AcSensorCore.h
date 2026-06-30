#ifndef AC_SENSOR_CORE_H
#define AC_SENSOR_CORE_H

#include <Adafruit_ADS1X15.h>
#include "Config.h"

void setupAcSensors();
void readAcSensors();  // Call this in the main loop

extern float acVoltage;
extern float acCurrent;

#endif