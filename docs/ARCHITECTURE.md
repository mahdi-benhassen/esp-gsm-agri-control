# KC868-A2v3 — Architecture Guide

## Overview

The firmware is structured as 14 independent ESP-IDF components communicating via the `esp_event` event loop. Each component owns its hardware peripheral and exposes a public API through its header. Components never call each other directly — they post and subscribe to events.

## Boot Sequence

```
app_main()
├── 1. nvs_flash_init()              — Non-Volatile Storage
├── 2. esp_netif_init()              — TCP/IP stack
├── 3. esp_event_loop_create_default() — Event loop
├── 4. config_store_init()           — Load config from NVS
├── 5. rtc_manager_init()            — DS3231 RTC (installs I2C bus)
├── 6. lcd_display_init()            — SSD1306 OLED (shares I2C)
├── 7. relay_control_init()          — 2× relay GPIO + auto-off timers
├── 8. digital_input_init()          — 2× dry contact inputs
├── 9. Free GPIO config              — GPIO 4/5/6/38 as inputs
├── 10. sensor_hub_init()            — DS18B20 1-Wire sensors + read task
├── 11. modem_manager_init()         — SIM7600 PPPoS + signal query
├── 12. rs485_init()                 — RS485 half-duplex UART
├── 13. sd_card_logger_init()        — SPI SD card FATFS mount
├── 14. system_monitor_init()        — Health logging task
├── 15. Wait for cellular (60s timeout)
├── 16. command_handler_init()       — Register MQTT command handler
├── 17. mqtt_wrapper_init()          — Connect to broker, subscribe cmd
└── 18. app_logic_start()            — Main loop (publish, LCD, inputs)
```

## Event Bus Layout

```
MQTT_APP_EVENTS
├── MQTT_APP_EVENT_CONNECTED    → app_logic (publish initial state)
├── MQTT_APP_EVENT_DISCONNECTED → (reserved)
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
| `app_logic` | 1 | 5 | 4096 | Periodic sensor publish, status publish, LCD refresh, input scanning |
| `sensor_read` | 1 | 6 | 4096 | Read DS18B20 sensors, post SENSOR_EVENT_NEW_DATA |
| `sys_mon` | — | 3 | 3072 | Log heap/uptime stats |

All shared state (relay states, sensor cache, config, modem connection, MQTT client) is protected by FreeRTOS mutexes. The I2C bus is shared between RTC and LCD — the RTC manager is the owner/installer of `I2C_NUM_0`.

## Component Dependency Graph

```
main
├── config_store          (NVS)
├── rtc_manager           (I2C) ──┐
├── lcd_display           (I2C) ──┤ shares I2C_NUM_0
├── relay_control         (GPIO)
├── digital_input         (GPIO) ── config_store
├── sensor_hub            (1-Wire) ── onewire_sensor, config_store
│   └── onewire_sensor    (GPIO bit-bang)
├── modem_manager         (UART, esp_modem)
├── rs485_manager         (UART)
├── sd_card_logger        (SPI, FATFS) ── rtc_manager
├── system_monitor        (logging) ── modem_manager, mqtt_client_wrapper, rtc_manager, sd_card_logger
├── command_handler       (JSON) ── relay_control, config_store, sensor_hub, mqtt_client_wrapper, digital_input, rtc_manager, system_monitor
├── mqtt_client_wrapper   (esp_mqtt) ── config_store, sensor_hub
└── app_logic             (orchestration) ── all of the above
```

## Data Flow

### Incoming MQTT Command
```
MQTT Broker → mqtt_client_wrapper (MQTT_EVENT_DATA)
  → posts MQTT_APP_EVENT_COMMAND_RECEIVED (heap-allocated payload)
    → command_handler::on_mqtt_command() parses JSON
      → dispatch to relay_control / config_store / sensor_hub / rtc_manager
        → send_response() publishes to cmd/response topic
```

### Periodic Sensor Publish
```
sensor_hub task (30s default)
  → onewire_read_temperature() × 2
    → store in mutex-protected cache
      → post SENSOR_EVENT_NEW_DATA
        → app_logic task picks up on next loop iteration
          → mqtt_wrapper_publish_sensor_data() to sensors topic
```

### Digital Input Change
```
app_logic task (1s loop, debounce_ms configurable)
  → digital_input_get() × 2
    → if state changed: post DIGITAL_INPUT_EVENT_CHANGED
      → app_logic handler: mqtt_wrapper_publish_input_state() to inputs topic
```
