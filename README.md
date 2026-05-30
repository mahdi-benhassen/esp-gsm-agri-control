# ESP32 + GSM/4G Cellular Smart Agriculture Control System

A clean, modular, and extensible firmware architecture built natively on Espressif's ESP-IDF v5.x SDK. Designed for monitoring environmental parameters and controlling irrigation systems in agricultural fields using cellular connectivity.

## Features

- **Modular Architecture**: Complete separation of concerns. Easily replace or add components (e.g., adding a new sensor type to `sensor_hub` or changing modems in `modem_manager`).
- **Cellular Connectivity (PPPoS)**: Leverages official `esp_modem` to bridge a SIM7600/A7670 4G module to ESP32's `esp_netif` stack. Transparent TCP/IP networking (standard sockets and protocol clients).
- **Automated Irrigation**: Intelligent hysteresis-based watering. Triggers the irrigation relay automatically when the soil moisture falls below the configurable threshold.
- **Remote MQTT API**: Fully controllable via structured JSON messages for toggling relays, querying real-time telemetry, updating system parameters, or triggering reboots.
- **Fail-safe Auto-off Relays**: Hardware-timer backed relays to prevent flooding if communication drops while the water pump is ON.
- **Persistent Configuration**: Stores thresholds and configuration parameters persistently in Non-Volatile Storage (NVS).

---

## Hardware Configuration (Default)

| ESP32 GPIO | Connected Device Peripheral |
|---|---|
| **GPIO 17** | Modem UART RX (ESP32 TX) |
| **GPIO 16** | Modem UART TX (ESP32 RX) |
| **GPIO 4** | Modem Power Control (PWRKEY) |
| **GPIO 34** | Capacitive Soil Moisture Sensor (Analog ADC1 CH6) |
| **GPIO 27** | DHT22 Temperature & Humidity Sensor Data Line |
| **GPIO 25** | Relay 1 Control (Pump 1) |
| **GPIO 26** | Relay 2 Control (Pump 2) |
| **GPIO 32** | Relay 3 Control (Valve 1) |
| **GPIO 33** | Relay 4 Control (Valve 2) |

---

## Building and Flashing

### Prerequisites

Ensure you have ESP-IDF v5.0 or later installed and configured in your shell environment.

### Compile & Flash

1. Configure the project:
   ```bash
   idf_py menuconfig
   ```
   Navigate to the component configuration menus under `Component config -> Modem Manager Configuration`, `Component config -> Sensor Hub Configuration`, etc. to adjust UART pins, sensor parameters, and MQTT settings.
   
2. Build the project:
   ```bash
   idf.py build
   ```
   *Note: On first build, the ESP Component Manager will automatically download the `espressif/esp_modem` component dependency from the registry.*

3. Flash and monitor the console output:
   ```bash
   idf.py -p <PORT> flash monitor
   ```

---

## MQTT API Reference

Topic Paths:
- **Telemetry Publication**: `agri/{MAC_ADDRESS}/sensors`
- **Relay State Publication**: `agri/{MAC_ADDRESS}/relays`
- **Status/Health Publication**: `agri/{MAC_ADDRESS}/status`
- **Command Subscription**: `agri/{MAC_ADDRESS}/cmd`
- **Response Publication**: `agri/{MAC_ADDRESS}/cmd/response`

### Commands (Send to `agri/{MAC_ADDRESS}/cmd`)

#### Toggle Relay State
```json
{
  "cmd": "relay_set",
  "channel": 0,
  "state": true
}
```

#### Timed Relay Activation (Auto-shutoff after duration)
```json
{
  "cmd": "relay_timed",
  "channel": 0,
  "duration": 300
}
```

#### Query Sensor Data Immediately
```json
{
  "cmd": "get_sensors"
}
```

#### Modify Configuration Parameters (Saves to NVS)
```json
{
  "cmd": "set_config",
  "key": "moisture_threshold",
  "value": "45.5"
}
```
*Config keys supported: `sensor_interval`, `mqtt_interval`, `moisture_threshold`, `auto_irrigation`, `irrigation_duration`, `mqtt_broker`, `device_name`.*

#### Emergency Stop (Shut down all relays)
```json
{
  "cmd": "all_off"
}
```

#### Soft System Reboot
```json
{
  "cmd": "reboot"
}
```
