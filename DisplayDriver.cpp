#include "DisplayDriver.h"
#include "BleCore.h" // Gives us access to bms1Data and bms2Data

// Keep your existing Full Buffer setup
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, 22, 21);

// Short status helper to save screen space
const char* shortStatus(SystemStatus s) {
  if (s == STATUS_CHARGING) return "CHG";
  if (s == STATUS_DISCHARGING) return "DIS";
  if (s == STATUS_IDLE) return "IDL";
  return "ERR";
}

void setupDisplay() {
  u8g2.begin();
  u8g2.setContrast(150); 
}

void updateDisplay(const SystemMetrics& metrics) {
  u8g2.clearBuffer();
  // Using a slightly wider font that perfectly fits 5 lines on 64 pixels
  u8g2.setFont(u8g2_font_profont11_tf); 
  
  char buf[32]; // Text buffer
  
  // Line 1: Overall Status & AC Power (y = 10)
  sprintf(buf, "SYS: %s | AC: %.0fW", shortStatus(metrics.status), metrics.acPower);
  u8g2.drawStr(0, 10, buf);
  
  // Line 2: BMS 1 - Volt, Amps, SoC (y = 23)
  sprintf(buf, "B1: %.1fV %5.1fA %d%%", bms1Data.voltage, bms1Data.current, bms1Data.soc);
  u8g2.drawStr(0, 23, buf);
  
  // Line 3: BMS 2 - Volt, Amps, SoC (y = 36)
  sprintf(buf, "B2: %.1fV %5.1fA %d%%", bms2Data.voltage, bms2Data.current, bms2Data.soc);
  u8g2.drawStr(0, 36, buf);
  
  // Line 4: DC Net Total (y = 49)
  sprintf(buf, "NET: %.1fA (%.0fW)", metrics.netCurrent, metrics.netPower);
  u8g2.drawStr(0, 49, buf);
  
  // Line 5: System Health - Imbalance & Peak Temp (y = 62)
  sprintf(buf, "Imb: %d%% | MaxT: %.1fC", metrics.socDelta, metrics.peakTemp);
  u8g2.drawStr(0, 62, buf);
  
  u8g2.sendBuffer();
}