# KinCony KC868-A2v3 — ESP32-S3 Smart Relay Controller (ESP-IDF)

A modular, event-driven firmware architecture built natively on **ESP-IDF v5.x** for the KinCony KC868-A2v3 board. Designed for smart home automation with cellular (4G) connectivity, MQTT remote control, sensor monitoring, and local display/logging.

## Hardware — KinCony KC868-A2v3

| Feature | Detail |
|---|---|
| MCU | ESP32-S3-WROOM-1 (N16R8 — 16MB Flash, 8MB PSRAM) |
| Relays | 2CH (250V/10A COM, NO, NC) |
| Digital Inputs | 4CH dry contact (optocoupler isolated, 500m cable) |
| Analog Inputs | 2CH 0-10V (GPIO4/5, ADC1_CH3/4) |
| RTC | DS3231 high-precision (CR1220 battery backup) |
| Display | SSD1306 128×64 I2C OLED |
| Storage | microSD card (SPI bus), 24C02 EEPROM (I2C) |
| Communications | 4G (SIM7600E/SIM800L), RS485, Ethernet (W5500), Wi-Fi, BLE |
| Free GPIOs | 1 pin for custom expansion (GPIO6) |
| Power | 12/24V DC, DIN rail mount |

### GPIO Pin Mapping

| GPIO | Function | Status |
|---|---|---|
| **40** | Relay 1 | Active |
| **39** | Relay 2 | Active |
| **16** | Digital Input 1 (dry contact, active-low) | Active |
| **17** | Digital Input 2 (dry contact, active-low) | Active |
| **35** | Digital Input 3 (dry contact, active-low) | Active |
| **36** | Digital Input 4 (dry contact, active-low) | Active |
| **18** | 1-Wire TMP1 (DS18B20, pull-up on PCB) | Active |
| **8** | 1-Wire TMP2 (DS18B20, pull-up on PCB) | Active |
| **48** / **47** | I2C SDA / SCL | Active |
| **10** / **9** | 4G Modem UART TX / RX | Active |
| **7** / **15** | RS485 TXD / RXD | Active |
| **42** / **43** / **44** / **41** | Ethernet W5500 SPI (SCK/MOSI/MISO/CS) | Stub (needs IDF ≥5.3) |
| **2** / **1** | Ethernet W5500 INT / RST | Stub |
| **12** / **13** / **14** / **11** / **21** | SD Card SPI (MOSI/SCK/MISO/CS/CD) | Active |
| **4** | Analog Input 1 (0-10V, ADC1_CH3) | Active |
| **5** | Analog Input 2 (0-10V, ADC1_CH4) | Active |
| **38** | Factory Reset button (long-press) | Active |
| **6** | Free GPIO (input with pull-up) | Active |

### I2C Bus Devices

| Address | Device | Status |
|---|---|---|
| **0x68** | DS3231 RTC | Active — time read/write, on-chip temperature |
| **0x3C** | SSD1306 OLED 128×64 | Active — 3-line status display |
| **0x50** | 24C02 EEPROM (2Kbit) | Active — page-safe writes |

---

## Features

- **Event-Driven Architecture** — components communicate via `esp_event` buses, fully decoupled.
- **Cellular 4G Connectivity (PPPoS)** — SIM7600 or SIM800L module via `esp_modem` with automatic PPP connection on boot and RSSI monitoring.
- **Remote MQTT API** — structured JSON commands for relay control, sensor queries, config changes, time sync, and reboot.
- **MQTT TLS** — `mqtts://` broker URIs enable TLS for encrypted command/telemetry traffic.
- **Fail-Safe Auto-Off Relays** — `esp_timer` hardware timers prevent relays from staying stuck ON (max configurable duration).
- **Communication-Loss Safe State** — heartbeat watchdog turns all relays OFF if MQTT/remote connectivity is lost for a configured timeout.
- **Persistent Configuration** — NVS flash storage with runtime validation; configurable over MQTT without reflash.
- **RTC-Backed Timestamps** — DS3231 for accurate time in logs, LCD display, and MQTT payloads.
- **Local LCD Display** — SSD1306 OLED shows relay states, 4G signal strength (dBm), and MQTT connection status.
- **Web GUI Dashboard** — responsive single-page web app served from the ESP32, protected by HTTP Basic Auth and rate limiting.
- **WiFi Manager** — dual mode (AP + STA). Access point for initial setup, connects to your router with saved credentials.
- **REST API** — JSON endpoints at `/api/status`, `/api/relays`, `/api/inputs`, `/api/sensors`, `/api/analog`, `/api/wifi`, `/api/config`.
- **SD Card Logging** — timestamped log entries written to microSD (FATFS).
- **RS485 Modbus** — half-duplex UART with Modbus RTU CRC16 frame builder (9600 baud default).
- **Digital Inputs** — 4 isolated dry-contact inputs with configurable debounce and per-channel inversion.
- **Analog Inputs** — 2× 0-10V analog inputs with curve-fitting ADC calibration.
- **1-Wire Sensors** — 2× DS18B20 temperature sensors with bit-banged protocol.
- **EEPROM Storage** — 24C02 I2C EEPROM with page-safe writes for persistent counters or calibration data.
- **Factory Reset** — long-press GPIO38 to erase NVS and reboot to defaults.
- **Watchdog Hardening** — task and interrupt watchdogs, brownout detection, stack protection, and heap poisoning enabled.
- **Thread-Safe** — all shared state protected by FreeRTOS mutexes.

---

## Project Structure

```
main/                        # App entry point (app_main)
components/
  analog_input/              # 2× 0-10V ADC inputs with calibration
  app_logic/                 # Event handlers + periodic publish loop + LCD refresh
  command_handler/           # MQTT command dispatch (JSON parsing + validation)
  config_store/              # NVS persistent config (read/write/validate/sanitize)
  digital_input/             # 4× dry contact inputs with debounce + event posting
  eeprom_storage/            # 24C02 I2C EEPROM page-safe writes
  ethernet_manager/          # W5500 Ethernet (stub — IDF ≥5.3 required)
  factory_reset/             # Long-press factory reset monitor
  i2c_bus_manager/           # Central I2C_NUM_0 bus owner
  lcd_display/               # SSD1306 I2C OLED (128×64 framebuffer, 6×8 font)
  modem_manager/             # SIM7600/SIM800L PPPoS — init, RSSI, IP events, reconnect
  mqtt_client_wrapper/       # MQTT client abstraction, topic construction, LWT, TLS
  onewire_sensor/            # Bit-banged 1-Wire protocol + DS18B20 read sequence
  relay_control/             # 2-channel relay GPIO driver + timed auto-off timers
  rs485_manager/             # RS485 half-duplex UART + Modbus RTU frame builder
  rtc_manager/               # DS3231 I2C RTC — BCD conversion, time read/write, temp
  safe_state/                # Heartbeat watchdog + relay-OFF safe state
  sd_card_logger/            # SPI SD card — FATFS mount, timestamped log writes
  sensor_hub/                # Periodic DS18B20 reading task + event posting
  system_monitor/            # Health diagnostics (heap, uptime, RSSI, SD status)
  web_server/                # HTTP server + WiFi manager + embedded web GUI + auth
```

---

## Building and Flashing

### Prerequisites

- ESP-IDF **v5.2** or later
- On first build, the ESP Component Manager auto-downloads `espressif/esp_modem`

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
- `Digital Input Configuration` — GPIO pins, debounce, inversion
- `Analog Input Configuration` — GPIO pins, attenuation, calibration, publish interval
- `1-Wire Sensor Configuration` — GPIO pins for TMP1/TMP2
- `Modem Manager Configuration` — modem model, UART pins, baud rate, APN, PWRKEY
- `MQTT Client Configuration` — default broker URI, topic prefix, TLS certs
- `Web Server Configuration` — auth username/password, rate-limit settings
- `Safe State Configuration` — heartbeat timeout, relay-OFF trigger
- `Factory Reset Configuration` — GPIO and long-press duration
- `RTC Manager Configuration` — I2C SDA/SCL pins
- `LCD Display Configuration` — I2C pins, refresh interval
- `RS485 Configuration` — TXD/RXD pins, baud rate
- `SD Card Logger Configuration` — SPI pins, card detect pin
- `Sensor Hub Configuration` — read interval
- `System Monitor Configuration` — report interval, low-heap threshold
- `Application Logic Configuration` — task stack, priority, status publish interval
- `EEPROM Storage Configuration` — I2C address, page size
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

All `/api/*` endpoints require HTTP Basic Auth and are rate-limited. The root `/` serves the dashboard HTML.

| Endpoint | Method | Description |
|---|---|---|
| `/` | GET | Web dashboard (HTML) |
| `/api/status` | GET | System status JSON (uptime, heap, RSSI, WiFi, SD, RTC) |
| `/api/relays` | GET | Relay states `[true, false]` |
| `/api/relays` | POST | Set relay: `{"channel":0,"state":true}` / timed / all_off |
| `/api/inputs` | GET | Digital input states |
| `/api/analog` | GET | Analog input readings in mV `[v1, v2]` |
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
| `analog` | Publish | `{"voltage_1": 5.0, "voltage_2": 7.2}` | Analog input voltage changes |
| `status` | Publish / LWT | See below | System health + LWT online/offline |
| `cmd` | Subscribe | See below | Incoming JSON commands (max 1KB) |
| `cmd/response` | Publish | `{"cmd": "...", "result": "ok", "message": "..."}` | Command responses |

### Security Notes

- Change the default web credentials and MQTT broker before deploying.
- Prefer `mqtts://` broker URIs for encrypted remote access.
- The device will reboot automatically on critical heap allocation failure to avoid undefined behaviour.
- Long-press GPIO38 erases all configuration and restores defaults.

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

Supported config keys: `sensor_interval`, `mqtt_interval`, `debounce_ms`, `safe_state_timeout`, `input_1_inverted`, `input_2_inverted`, `input_3_inverted`, `input_4_inverted`, `interlock`, `lcd_enabled`, `sd_log_enabled`, `mqtt_broker`, `device_name`

Changing `mqtt_broker` triggers an automatic MQTT disconnect, reconfiguration with the new URI, and reconnect.

---

## OTA

The partition table reserves `ota_0`, `ota_1`, and `otadata` partitions. OTA firmware updates are prepared for future implementation via HTTPS or MQTT binary streaming. The secure boot and flash encryption features remain configurable through standard ESP-IDF tools.

---

## Default MQTT Broker

`mqtt://broker.hivemq.com:1883` (public test broker — **change before production**, prefer `mqtts://`)
