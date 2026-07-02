# Smart Inverter Power Selection (SIPS)

A sophisticated embedded system for intelligent power management and inverter control based on real-time battery management system (BMS) data. This project monitors dual BMS units via Bluetooth Low Energy (BLE), analyzes system metrics, and provides an intuitive display interface for power system status.

## 🎯 Project Overview

SIPS is designed to:
- **Monitor dual BMS units** via BLE connectivity
- **Analyze battery health metrics** including voltage, current, temperature, and state of charge (SoC)
- **Detect power flow direction** (charging/discharging/idle states)
- **Manage contactors** for safe power distribution
- **Display real-time metrics** on an integrated display with multi-view navigation

### Key Features
- ✅ Dual BMS monitoring with independent state machines
- ✅ Real-time AC voltage/current sensing
- ✅ Multi-view display system with button-based navigation
- ✅ Graceful degradation with data staleness detection (5-minute timeout)
- ✅ Configurable read intervals and timeout mechanisms
- ✅ Comprehensive system metrics tracking

## 🏗️ Architecture

### Core Components

| Component | File | Purpose |
|-----------|------|---------|
| **Main Application** | `SIPS.ino` | State machine orchestration and core loop |
| **BLE Manager** | `BleCore.cpp/h` | Bluetooth connectivity and data subscription |
| **AC Sensor Driver** | `AcSensorCore.cpp/h` | AC voltage and current measurement |
| **Display Driver** | `DisplayDriver.cpp/h` | UI rendering and display management |
| **Configuration** | `config.h` | Global constants and data structures |

### State Machine

The application uses a finite state machine with 9 states for sequential BMS polling:

```
STATE_WAIT_INTERVAL
    ↓
STATE_CONNECT_BMS1 → STATE_DELAY_BMS1 → STATE_WAIT_BMS1_DATA
    ↓
STATE_COOLDOWN
    ↓
STATE_CONNECT_BMS2 → STATE_DELAY_BMS2 → STATE_WAIT_BMS2_DATA
    ↓
STATE_PROCESS_LOGIC → (repeat)
```

### Data Structures

**BmsData** - Individual battery module metrics:
- Voltage, Current, Power
- State of Charge (SoC)
- Maximum Temperature
- Connection status and update timestamp

**SystemMetrics** - Aggregated system-level data:
- Net current and voltage (from both BMSs)
- Average SoC and temperature
- AC sensing data
- Current system status (IDLE/CHARGING/DISCHARGING/ERROR)

## 🔌 Hardware Requirements

- **Microcontroller**: ESP32 (or compatible with BLE support)
- **BMS Units**: 2x devices with BLE capability (MAC addresses configured in `config.h`)
- **Display**: U8G2-compatible OLED/LCD
- **AC Sensors**: ADS1X15 ADC module for voltage/current measurement
- **Button**: Digital input pin 15 for display navigation
- **Power Components**: Contactors for power switching (managed by application logic)

## 📦 Dependencies

Install the following Arduino libraries:

```
NimBLEDevice - Bluetooth Low Energy communication
U8g2          - Graphics display interface
Adafruit_ADS1X15 - ADC analog sensor reading
```

### Installation Steps

1. Open Arduino IDE
2. Go to **Sketch → Include Library → Manage Libraries**
3. Search for and install:
   - `NimBLEDevice`
   - `U8g2`
   - `Adafruit ADS1X15`

## ⚙️ Configuration

Edit `config.h` to customize:

```cpp
// BMS Device MAC Addresses
const std::string BMS1_MAC = "a5:c2:39:1d:e6:2e";
const std::string BMS2_MAC = "a5:c2:39:1d:e5:9b";

// Timing Parameters (milliseconds)
const unsigned long READ_INTERVAL_MS = 10000;  // Read every 10 seconds
const unsigned long TIMEOUT_MS = 2000;         // Wait 2 seconds for BMS response
const unsigned long COOLDOWN_MS = 200;         // Cool-down between BMS connections
```

## 🚀 Operation

### Boot Sequence
1. Serial communication initialized (115200 baud)
2. Display splash screen shown
3. AC sensor calibration
4. BLE initialization
5. Main loop begins with BMS1 connection

### Display Navigation
- **Auto-rotate**: Display views change every 10 seconds
- **Manual control**: Press button on GPIO 15 to advance to next view
- **Views**: Up to 4 configurable metric displays

### Power Logic
The system determines operational status based on net current:
- **Charging**: Net current > 1.0A (DC flowing into batteries)
- **Discharging**: Net current < -1.0A (DC flowing from batteries)
- **Idle**: Current between -1.0A and 1.0A

### Safety Features
- **Data Staleness Detection**: If BMS data is older than 5 minutes, system enters ERROR state
- **Connection Monitoring**: Tracks active BLE connections for both BMS units
- **Graceful Degradation**: Uses cached data during temporary connection loss with warnings

## 📊 Serial Output

The system provides detailed debugging information via serial console:

```
================ SYSTEM METRICS ================
STATUS   : CHARGING
------------------------------------------------
BMS 1    : 48.25V |   15.50A |  746W | 85% | 35.2C
BMS 2    : 48.30V |   15.45A |  745W | 85% | 34.8C
------------------------------------------------
DELTAS   : Volt:0.050V | Cur:0.05A | Pwr:1W
DC TOTAL : Net:   31.00A |  1490W
AC SENSE : 230.5V | 6.50A | 1499 VA
HEALTH   : Avg SoC 85% (Imb 0%) | Peak Temp 35.2C
================================================
```

## 🔍 Monitoring & Metrics

Key metrics available in real-time:
- **Balance Monitoring**: SoC and voltage delta between BMS units
- **Thermal Management**: Peak temperature across all cells
- **Power Flow**: Net current and power calculations
- **AC Coupling**: Grid voltage and current sensing for inverter feedback

## 🛠️ Development

### Adding New Functionality
1. Extend `SystemMetrics` in `config.h` for new data fields
2. Implement calculation logic in `evaluateContactorLogic()` function
3. Update `DisplayDriver` with new display formats
4. Add state handlers if new sequential steps are needed

### Debug Mode
Serial output provides extensive logging:
- `[DEBUG]` - State transitions and timing info
- `[WARNING]` - Connection issues with grace period notifications
- `[CRITICAL ERROR]` - Data timeout or system failures

## 📝 License

No license specified - contact repository owner for terms of use.

## 👤 Author

**Akshay B (akshaybmnmd)**

---

For issues, feature requests, or technical questions, please open an issue on the GitHub repository.
