#include "DisplayDriver.h"

// U8G2_SSD1306_128X64_NONAME_F_HW_I2C(rotation, clock, data, reset)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, 22, 21);

void setupDisplay() {
  u8g2.begin();
  u8g2.setContrast(150); // Set brightness
}

void updateDisplay(const SystemMetrics& metrics) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_profont12_tf); // Small, crisp font
  
  // Header
  u8g2.drawStr(0, 10, "BMS MONITOR");
  
  // Power & Status
  char line1[32];
  sprintf(line1, "P: %.0f W | %s", metrics.netPower, (metrics.status == 2 ? "DISCH" : "CHG"));
  u8g2.drawStr(0, 25, line1);
  
  // SoC
  char line2[32];
  sprintf(line2, "SoC: %d%% | Delta: %d%%", metrics.avgSoc, metrics.socDelta);
  u8g2.drawStr(0, 38, line2);
  
  // Voltages
  char line3[32];
  sprintf(line3, "Min V: %.2fV", metrics.minVoltage);
  u8g2.drawStr(0, 51, line3);
  
  // Temp
  char line4[32];
  sprintf(line4, "Peak Temp: %.1fC", metrics.peakTemp);
  u8g2.drawStr(0, 64, line4);
  
  u8g2.sendBuffer();
}