# KC868-A2v3 — Architecture Guide

## Overview

The firmware is structured as independent ESP-IDF components communicating via the `esp_event` event loop. Each component owns a specific responsibility and exposes a public API through its header. Components do not call each other directly — they post and subscribe to events.

## Boot Sequence

```
app_main()
├── 0.  heap_caps_register_failed_alloc_callback() — Safe reboot on OOM
├── 1.  nvs_flash_init()                           — Non-Volatile Storage
├── 2.  esp_netif_init()                           — TCP/IP stack
├── 3.  esp_event_loop_create_default()            — System event loop
├── 4.  config_store_init()                        — Load config from NVS
├── 5.  i2c_bus_manager_init()                     — Owns I2C_NUM_0 bus
├── 6.  rtc_manager_init()                         — DS3231 RTC
├── 7.  lcd_display_init()                         — SSD1306 OLED
├── 8.  relay_control_init()                       — 2× relay GPIO + auto-off timers
├── 9.  digital_input_init()                       — 4× dry contact inputs
├── 10. analog_input_init()                        — 2× ADC analog inputs
├── 11. eeprom_storage_init()                      — 24C02 EEPROM
├── 12. free GPIO config                           — GPIO6 as input
├── 13. sensor_hub_init()                          — DS18B20 1-Wire sensors
├── 14. modem_manager_init()                       — SIM7600/SIM800L PPPoS
├── 15. rs485_init()                               — RS485 half-duplex UART
├── 16. sd_card_logger_init()                      — SPI SD card FATFS
├── 17. system_monitor_init()                      — Health diagnostics task
├── 18. safe_state_init()                          — Comm-loss heartbeat watchdog
├── 19. wait for cellular (60s timeout)
├── 20. command_handler_init()                     — MQTT command dispatch
├── 21. mqtt_wrapper_init()                        — MQTT broker + TLS
├── 22. app_logic_start()                          — Main loop
├── 23. web_server_init()                          — HTTP + WiFi + Web GUI
└── 24. factory_reset_init()                       — Long-press reset monitor
```

## Event Bus Layout

```
MQTT_APP_EVENTS
├── MQTT_APP_EVENT_CONNECTED    → app_logic (publish initial state)
├── MQTT_APP_EVENT_DISCONNECTED → app_logic (safe-state monitoring)
└── MQTT_APP_EVENT_COMMAND_RECEIVED → command_handler (dispatch)

RELAY_EVENTS
└── RELAY_EVENT_STATE_CHANGED   → app_logic (publish to MQTT)

SENSOR_EVENTS
└── SENSOR_EVENT_NEW_DATA       → app_logic (log, auto-publish)

DIGITAL_INPUT_EVENTS
└── DIGITAL_INPUT_EVENT_CHANGED → app_logic (publish to MQTT)

IP_EVENT
├── IP_EVENT_PPP_GOT_IP         → modem_manager (set connected)
└── IP_EVENT_PPP_LOST_IP        → modem_manager (clear connected)
```

## Thread Model

| Task | Core | Priority | Stack | Purpose |
|---|---|---|---|---|
| `app_logic` | 1 | 5 | 4096 | Periodic sensor/status publish, LCD refresh, input scanning |
| `sensor_read` | 1 | 6 | 4096 | Read DS18B20 sensors, post SENSOR_EVENT_NEW_DATA |
| `sys_mon` | — | 3 | 3072 | Log heap/uptime stats |
| `safe_state` | — | 4 | 2048 | Watchdog heartbeat monitor; triggers safe state on comm loss |
| `factory_rst` | — | 3 | 2048 | Long-press factory reset button monitor |

All shared state (relay states, sensor cache, config, modem connection, MQTT client) is protected by FreeRTOS mutexes. The I2C bus is owned by `i2c_bus_manager`; `rtc_manager`, `lcd_display`, and `eeprom_storage` are consumers.

## Component Dependency Graph

```
main
├── config_store          (NVS)
├── i2c_bus_manager       (I2C bus owner)
├── rtc_manager           (I2C) ← i2c_bus_manager
├── lcd_display           (I2C) ← i2c_bus_manager
├── eeprom_storage        (I2C) ← i2c_bus_manager
├── relay_control         (GPIO)
├── digital_input         (GPIO) ← config_store
├── analog_input          (ADC)
├── sensor_hub            (1-Wire) ← onewire_sensor, config_store
│   └── onewire_sensor    (GPIO bit-bang)
├── modem_manager         (UART, esp_modem)
├── rs485_manager         (UART)
├── sd_card_logger        (SPI, FATFS) ← rtc_manager
├── system_monitor        (logging)
├── safe_state            (heartbeat watchdog)
├── factory_reset         (GPIO long-press)
├── web_server            (HTTP, WiFi) ← relay_control, sensor_hub, digital_input,
│                                      analog_input, modem_manager, mqtt_client_wrapper,
│                                      config_store, system_monitor
├── command_handler       (JSON) ← relay_control, config_store, sensor_hub,
│                                 mqtt_client_wrapper, digital_input, rtc_manager,
│                                 system_monitor
├── mqtt_client_wrapper   (esp_mqtt) ← config_store, sensor_hub
└── app_logic             (orchestration) ← all of the above + safe_state
```

## Safety & Security

- **Watchdogs:** Task WDT, Interrupt WDT, and brownout detector are enabled in `sdkconfig.defaults`.
- **Safe State:** `safe_state` component monitors MQTT connectivity. If no heartbeat is fed for the configured timeout, all relays are turned OFF.
- **Web Auth:** HTTP Basic Auth protects all `/api/*` endpoints. Default credentials are configurable via `menuconfig`.
- **MQTT TLS:** `mqtts://` broker URIs automatically enable TLS in `mqtt_client_wrapper`.
- **Rate Limiting:** Web API requests are rate-limited. MQTT command payloads larger than 1KB are rejected.
- **Heap Protection:** A failed-alloc callback reboots the device safely instead of continuing with corrupted heap.
- **Factory Reset:** Long-press the configured GPIO (default 38) for 5 seconds to erase NVS and reboot.

## Data Flow

### Incoming MQTT Command
```
MQTT Broker → mqtt_client_wrapper (MQTT_EVENT_DATA)
  → size sanity check + heap allocation
    → posts MQTT_APP_EVENT_COMMAND_RECEIVED
      → command_handler::on_mqtt_command() parses JSON
        → dispatch to relay_control / config_store / sensor_hub / rtc_manager
          → send_response() publishes to cmd/response topic
```

### Periodic Sensor Publish
```
sensor_hub task (sensor_read_interval_sec)
  → onewire_read_temperature() × 2
    → store in mutex-protected cache
      → post SENSOR_EVENT_NEW_DATA
        → app_logic task picks up on next loop iteration
          → mqtt_wrapper_publish_sensor_data() to sensors topic
            → mqtt_wrapper_publish_analog() for each channel to analog topic
```

### Analog Input Publish
```
app_logic task (same interval as sensors)
  → analog_input_read_mv() × 2
    → if MQTT connected: mqtt_wrapper_publish_analog() to analog topic
```

### Digital Input Change
```
app_logic task (20ms poll loop)
  → digital_input_get() × 4
    → deadline-based debounce
      → if state changed: post DIGITAL_INPUT_EVENT_CHANGED
        → app_logic handler: mqtt_wrapper_publish_input_state() to inputs topic
```
