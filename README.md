# KinCony KC868-A2v3 — ESP32-S3 Smart Relay Controller (ESP-IDF)

A modular, event-driven firmware architecture built natively on **ESP-IDF v5.x** for the KinCony KC868-A2v3 board. Designed for smart home automation with cellular (4G) connectivity, MQTT remote control, sensor monitoring, and local display/logging.

## Hardware — KinCony KC868-A2v3

| Feature | Detail |
|---|---|
| MCU | ESP32-S3-WROOM-1 (N16R8 — 16MB Flash, 8MB PSRAM) |
| Relays | 2CH (250V/10A COM, NO, NC) |
| Digital Inputs | 2CH dry contact (optocoupler isolated, 500m cable) |
| RTC | DS3231 high-precision (CR1220 battery backup) |
| Display | SSD1306 128×64 I2C OLED |
| Storage | microSD card (SPI bus) |
| Communications | 4G (SIM7600E), RS485, Ethernet (W5500), Wi-Fi, BLE |
| Free GPIOs | 4 pins for custom expansion (4, 5, 6, 38) |
| Power | 12/24V DC, DIN rail mount |

### GPIO Pin Mapping

| GPIO | Function | Status |
|---|---|---|
| **40** | Relay 1 | Active |
| **39** | Relay 2 | Active |
| **16** | Digital Input 1 (dry contact, active-low) | Active |
| **17** | Digital Input 2 (dry contact, active-low) | Active |
| **18** | 1-Wire TMP1 (DS18B20, pull-up on PCB) | Active |
| **8** | 1-Wire TMP2 (DS18B20, pull-up on PCB) | Active |
| **48** / **47** | I2C SDA / SCL | Active |
| **10** / **9** | 4G Modem UART TX / RX | Active |
| **7** / **15** | RS485 TXD / RXD | Active |
| **42** / **43** / **44** / **41** | Ethernet W5500 SPI (SCK/MOSI/MISO/CS) | Stub (needs IDF ≥5.3) |
| **2** / **1** | Ethernet W5500 INT / RST | Stub |
| **12** / **13** / **14** / **11** / **21** | SD Card SPI (MOSI/SCK/MISO/CS/CD) | Active |
| **4** / **5** / **6** / **38** | Free GPIOs (inputs with pull-up) | Active |

### I2C Bus Devices

| Address | Device | Status |
|---|---|---|
| **0x68** | DS3231 RTC | Active — time read/write, on-chip temperature |
| **0x3C** | SSD1306 OLED 128×64 | Active — 3-line status display |
| **0x50** | 24C02 EEPROM (2Kbit) | Not implemented |

---

## Features

- **Event-Driven Architecture** — 14 components communicate via `esp_event` buses (`RELAY_EVENTS`, `SENSOR_EVENTS`, `DIGITAL_INPUT_EVENTS`, `MQTT_APP_EVENTS`), fully decoupled.
- **Cellular 4G Connectivity (PPPoS)** — SIM7600E module via `esp_modem` (component ^1.1.0) with automatic PPP connection on boot and RSSI monitoring.
- **Remote MQTT API** — structured JSON commands for relay control, sensor queries, config changes, time sync, and reboot.
- **Fail-Safe Auto-Off Relays** — `esp_timer` hardware timers prevent relays from staying stuck ON (max configurable duration).
- **Persistent Configuration** — NVS flash storage with runtime validation; configurable over MQTT without reflash.
- **RTC-Backed Timestamps** — DS3231 for accurate time in logs, LCD display, and MQTT payloads.
- **Local LCD Display** — SSD1306 OLED shows relay states, 4G signal strength (dBm), and MQTT connection status.
- **Web GUI Dashboard** — Responsive single-page web app served from the ESP32. Tabs for relays, inputs, sensors, WiFi config, system configuration, automations, and event log.
- **WiFi Manager** — Dual mode (AP + STA). Access point for initial setup, connects to your router with saved credentials.
- **REST API** — JSON endpoints at `/api/status`, `/api/relays`, `/api/inputs`, `/api/sensors`, `/api/wifi`, `/api/config`.
- **SD Card Logging** — timestamped log entries written to microSD (FATFS).
- **RS485 Modbus** — half-duplex UART with Modbus RTU CRC16 frame builder (9600 baud default).
- **Digital Inputs** — 2 isolated dry-contact inputs with configurable debounce and per-channel inversion.
- **1-Wire Sensors** — 2× DS18B20 temperature sensors with bit-banged protocol.
- **Thread-Safe** — all shared state protected by FreeRTOS mutexes.

---

## Project Structure

```
main/                        # App entry point (app_main) — 16-step init sequence
components/
  app_logic/                 # Event handlers + periodic publish loop + LCD refresh
  command_handler/           # MQTT command dispatch (JSON parsing + validation)
  config_store/              # NVS persistent config (read/write/validate/sanitize)
  digital_input/             # 2× dry contact inputs with debounce + event posting
  ethernet_manager/          # W5500 Ethernet (stub — IDF ≥5.3 required)
  lcd_display/               # SSD1306 I2C OLED (128×64 framebuffer, 6×8 font)
  modem_manager/             # SIM7600 PPPoS — init, RSSI, IP events, reconnect
  mqtt_client_wrapper/       # MQTT client abstraction, topic construction, LWT
  onewire_sensor/            # Bit-banged 1-Wire protocol + DS18B20 read sequence
  relay_control/             # 2-channel relay GPIO driver + timed auto-off timers
  rs485_manager/             # RS485 half-duplex UART + Modbus RTU frame builder
  rtc_manager/               # DS3231 I2C RTC — BCD conversion, time read/write, temp
  sd_card_logger/            # SPI SD card — FATFS mount, timestamped log writes
  sensor_hub/                # Periodic DS18B20 reading task + event posting
  system_monitor/            # Health diagnostics (heap, uptime, RSSI, SD status)
  web_server/                # HTTP server + WiFi manager + embedded web GUI
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
- `SD Card Logger Configuration` — SPI pins, card detect pin
- `Sensor Hub Configuration` — read interval
- `System Monitor Configuration` — report interval, low-heap threshold
- `Application Logic Configuration` — task stack, priority, status publish interval
- `KC868-A2v3 Smart Controller Configuration` — firmware version, free GPIO enable

---

## Web GUI

The ESP32 serves a responsive single-page web application accessible via WiFi. Open a browser to the device IP address (shown on the LCD display or serial console).

### WiFi Setup

1. Power on the board — it creates an access point: **`KC868-A2v3-Setup`** (password: `admin1234`)
2. Connect your phone/laptop to this AP
3. Open `http://192.168.4.1` in a browser
4. Go to the **WiFi** tab, enter your router SSID and password, click **Connect**
5. The board joins your network; find its new IP on the LCD or serial log

### Web API Endpoints

| Endpoint | Method | Description |
|---|---|---|
| `/` | GET | Web dashboard (HTML) |
| `/api/status` | GET | System status JSON (uptime, heap, RSSI, WiFi, SD, RTC) |
| `/api/relays` | GET | Relay states `[true, false]` |
| `/api/relays` | POST | Set relay: `{"channel":0,"state":true}` / timed / all_off |
| `/api/inputs` | GET | Digital input states |
| `/api/sensors` | GET | Temperature readings (T1, T2) |
| `/api/wifi` | GET | WiFi mode, IP, SSID |
| `/api/wifi` | POST | Connect: `{"ssid":"MyRouter","password":"mypass"}` or `{"disconnect":true}` |
| `/api/config` | GET | Device configuration |
| `/api/config` | POST | Update config (auto-saves to NVS flash) |
| `/api/reboot` | POST | Reboot device |

### Dashboard Tabs

- **Dashboard** — system overview, relay states, inputs, sensor readings
- **Relays** — toggle relays ON/OFF, timed activation
- **Inputs** — live digital input states
- **Sensors** — DS18B20 temperature readings
- **WiFi** — connect to router, view connection status
- **Config** — device name, MQTT broker, intervals, inversion, LCD/SD toggles, reboot
- **Automations** — simple IF-THEN rules (e.g., Input 1 ON → Relay 1 ON for 10s)
- **Log** — event log (relay changes, config saves, WiFi events)

---

## MQTT API

Topic prefix: `kca2v3/{DEVICE_ID}` (DEVICE_ID = 12-char hex MAC address from ESP base MAC)

| Topic Suffix | Direction | Payload | Purpose |
|---|---|---|---|
| `sensors` | Publish | `{"temp_1": 23.5, "temp_2": 18.2, ...}` | Temperature readings (T1, T2) |
| `relays` | Publish | `{"channel": 0, "state": true}` | Relay state changes |
| `inputs` | Publish | `{"channel": 1, "state": false}` | Digital input state changes |
| `status` | Publish / LWT | See below | System health + LWT online/offline |
| `cmd` | Subscribe | See below | Incoming JSON commands |
| `cmd/response` | Publish | `{"cmd": "...", "result": "ok", "message": "..."}` | Command responses |

### Status Payload

```json
{
  "uptime": 3600,
  "free_heap": 245000,
  "min_free_heap": 200000,
  "modem_rssi": -65,
  "mqtt_connected": true,
  "modem_connected": true,
  "eth_connected": false,
  "sd_mounted": true,
  "rtc_temp": 31.5
}
```

### MQTT Commands

#### Relay Control
```json
{"cmd": "relay_set",    "channel": 0, "state": true}
{"cmd": "relay_timed",  "channel": 1, "duration": 300}
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
{"cmd": "set_time",    "timestamp": 1748744400}
{"cmd": "reboot"}
```

#### Configuration (persisted to NVS)
```json
{"cmd": "set_config", "key": "mqtt_broker", "value": "mqtt://mybroker:1883"}
```

Supported config keys: `sensor_interval`, `mqtt_interval`, `debounce_ms`, `input_1_inverted`, `input_2_inverted`, `interlock`, `lcd_enabled`, `sd_log_enabled`, `mqtt_broker`, `device_name`

Changing `mqtt_broker` triggers an automatic MQTT disconnect, reconfiguration with the new URI, and reconnect.

---

## Default MQTT Broker

`mqtt://broker.hivemq.com:1883` (public test broker — change via `set_config` or `menuconfig`)
