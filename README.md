# KinCony KC868-A2v3 — ESP32-S3 Smart Relay Controller (ESP-IDF)

A modular, event-driven firmware architecture built natively on **ESP-IDF v5.x** for the KinCony KC868-A2v3 board. Designed for smart home automation with cellular (4G) connectivity, MQTT remote control, sensor monitoring, and local display/logging.

## Hardware — KinCony KC868-A2v3

| Feature | Detail |
|---|---|
| MCU | ESP32-S3-WROOM-1 (N16R8 — 16MB Flash, 8MB PSRAM) |
| Relays | 2CH (250V/10A COM,NO,NC) |
| Digital Inputs | 2CH dry contact (optocoupler isolated, 500m cable) |
| RTC | DS3231 high-precision (CR1220 battery backup) |
| Display | SSD1306 128x64 I2C OLED |
| Storage | microSD card (SPI bus) |
| Communications | 4G (SIM7600E / SIM800L), RS485, Ethernet (W5500), WiFi, BLE |
| Power | 12/24V DC, DIN rail mount |

### GPIO Pin Mapping

| GPIO | Function |
|---|---|
| **40** | Relay 1 |
| **39** | Relay 2 |
| **16** | Digital Input 1 (dry contact, active-low) |
| **17** | Digital Input 2 (dry contact, active-low) |
| **18** | 1-Wire TMP1 (DS18B20, pull-up on PCB) |
| **8** | 1-Wire TMP2 (DS18B20, pull-up on PCB) |
| **48** / **47** | I2C SDA / SCL (DS3231, SSD1306, 24C02) |
| **10** / **9** | 4G Modem UART TX / RX |
| **7** / **15** | RS485 TXD / RXD |
| **42** / **43** / **44** / **41** | Ethernet W5500 SPI (SCK/MOSI/MISO/CS) |
| **2** / **1** | Ethernet W5500 INT / RST |
| **12** / **13** / **14** / **11** / **21** | SD Card SPI (MOSI/SCK/MISO/CS/CD) |
| **4** / **5** / **6** / **38** | Free GPIOs (available on PCB) |

---

## Features

- **Event-Driven Architecture** — 14 components communicate via `esp_event` buses (RELAY_EVENTS, SENSOR_EVENTS, DIGITAL_INPUT_EVENTS, MQTT_APP_EVENTS), fully decoupled.
- **Cellular 4G Connectivity (PPPoS)** — SIM7600E module via `esp_modem` (component ^1.1.0) with automatic PPP connection on boot.
- **Remote MQTT API** — structured JSON commands for relay control, sensor queries, config changes, time sync, and reboot.
- **Fail-Safe Auto-Off Relays** — `esp_timer` hardware timers prevent relays from staying stuck ON.
- **Persistent Configuration** — NVS flash storage with runtime validation; configurable over MQTT without reflash.
- **RTC-Backed Timestamps** — DS3231 for accurate time in logs and MQTT payloads.
- **Local LCD Display** — SSD1306 shows relay states, signal strength, and MQTT status.
- **SD Card Logging** — timestamped logs to microSD.
- **RS485 Modbus Support** — basic Modbus RTU frame construction.
- **Thread-Safe** — all shared state protected by FreeRTOS mutexes.

---

## Project Structure

```
main/                        # App entry point (app_main)
components/
  app_logic/                 # Event handlers + periodic publish loop + LCD updates
  command_handler/           # MQTT command dispatch (JSON parsing)
  config_store/              # NVS persistent config (read/write/validate)
  digital_input/             # 2x dry contact inputs with debounce + event posting
  ethernet_manager/          # W5500 Ethernet (stub — needs IDF >=5.3)
  lcd_display/               # SSD1306 I2C OLED driver (6x8 font)
  modem_manager/             # SIM7600 PPPoS modem init, RSSI, connection state
  mqtt_client_wrapper/       # MQTT client abstraction, topic construction, LWT
  onewire_sensor/            # 1-Wire DS18B20 temperature sensor driver
  relay_control/             # 2-channel relay GPIO driver + timed auto-off
  rs485_manager/             # RS485 half-duplex + Modbus RTU frame builder
  rtc_manager/               # DS3231 I2C RTC driver (time read/set, temp)
  sd_card_logger/            # SPI SD card log writer with RTC timestamps
  sensor_hub/                # Periodic DS18B20 sensor reading task + event posting
  system_monitor/            # Health diagnostics (heap, uptime, RSSI, MQTT status)
```

---

## Building and Flashing

### Prerequisites

- ESP-IDF **v5.0** or later
- On first build, the ESP Component Manager auto-downloads `espressif/esp_modem ^1.1.0`

### Build

```bash
idf.py set-target esp32s3
idf.py build
```

### Flash & Monitor

```bash
idf.py -p <PORT> flash monitor
```

### Configuration

```bash
idf.py menuconfig
```

Navigate to `Component config` to adjust:
- `Relay Control Configuration` — GPIO pins, active-level, max-on-time
- `Digital Input Configuration` — GPIO pins
- `1-Wire Sensor Configuration` — GPIO pins for TMP1/TMP2
- `Modem Manager Configuration` — UART pins, baud rate, APN, PWRKEY
- `MQTT Client Configuration` — default broker URI, topic prefix
- `RTC Manager Configuration` — I2C SDA/SCL pins
- `LCD Display Configuration` — I2C pins, refresh interval
- `RS485 Configuration` — TXD/RXD pins, baud rate
- `Ethernet W5500 Configuration` — SPI pins
- `SD Card Logger Configuration` — SPI pins
- `Sensor Hub Configuration` — read interval
- `System Monitor Configuration` — report interval, heap threshold
- `Application Logic Configuration` — task stack, priority, status publish interval

---

## MQTT API

Topic prefix: `kca2v3/{DEVICE_ID}` (DEVICE_ID = 12-char hex MAC address)

| Topic Suffix | Direction | Purpose |
|---|---|---|
| `sensors` | Publish | Temperature readings (T1, T2) |
| `relays` | Publish | Relay state changes |
| `inputs` | Publish | Digital input state changes |
| `status` | Publish / LWT | System health (heap, uptime, RSSI, MQTT state, RTC temp) |
| `cmd` | Subscribe | Incoming JSON commands |
| `cmd/response` | Publish | Command responses |

### MQTT Commands

#### Relay Control
```json
{"cmd": "relay_set", "channel": 0, "state": true}
{"cmd": "relay_timed", "channel": 1, "duration": 300}
{"cmd": "all_off"}
```

#### Sensor & Input Queries
```json
{"cmd": "get_sensors"}
{"cmd": "get_inputs"}
```

#### System
```json
{"cmd": "get_status"}
{"cmd": "get_time"}
{"cmd": "set_time", "timestamp": 1748744400}
{"cmd": "reboot"}
```

#### Configuration (persisted to NVS)
```json
{"cmd": "set_config", "key": "mqtt_broker", "value": "mqtt://mybroker:1883"}
```
Supported keys: `sensor_interval`, `mqtt_interval`, `debounce_ms`, `input_1_inverted`, `input_2_inverted`, `interlock`, `lcd_enabled`, `sd_log_enabled`, `mqtt_broker`, `device_name`

---

## Default MQTT Broker

`mqtt://broker.hivemq.com:1883` (public test broker — change via `set_config` or `menuconfig`)
