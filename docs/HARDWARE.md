# KC868-A2v3 — Hardware Reference

## Board Specifications

| Parameter | Value |
|---|---|
| Model | KinCony KC868-A2v3 |
| MCU | ESP32-S3-WROOM-1 (N16R8) |
| CPU | Dual-core Xtensa LX7 @ 240 MHz |
| Flash | 16 MB |
| PSRAM | 8 MB (Octal SPI) |
| Wi-Fi | 2.4 GHz 802.11 b/g/n |
| BLE | Bluetooth LE 5.0 |
| Power Input | 12–24V DC |
| PCB Size | 87mm × 93mm |
| Mount | DIN rail (plastic shell included) |

## Pin Reference

### I2C Bus (GPIO48 SDA, GPIO47 SCL)

| Address | Device | Notes |
|---|---|---|
| 0x3C | SSD1306 OLED 128×64 | Status display |
| 0x50 | 24C02 EEPROM 2Kbit | Config storage backup, page-write safe driver |
| 0x68 | DS3231 RTC | Battery-backed (CR1220) |

### Relays (250V / 10A COM, NO, NC)

| Channel | GPIO | Active Level |
|---|---|---|
| Relay 1 | GPIO40 | Active-LOW |
| Relay 2 | GPIO39 | Active-LOW |

### Digital Inputs (Optocoupler Isolated, Dry Contact)

| Channel | GPIO | Pull-up | Logic |
|---|---|---|---|
| Input 1 | GPIO16 | Internal | LOW = active (inverted by firmware) |
| Input 2 | GPIO17 | Internal | LOW = active (inverted by firmware) |
| Input 3 | GPIO35 | Internal | LOW = active (inverted by firmware) |
| Input 4 | GPIO36 | Internal | LOW = active (inverted by firmware) |

### 1-Wire Sensors (PCB Pull-up Resistors)

| Channel | GPIO | Default Device |
|---|---|---|
| TMP1 | GPIO18 | DS18B20 |
| TMP2 | GPIO8 | DS18B20 |

### Cellular Modem UART

| Signal | GPIO | Baud |
|---|---|---|
| ESP TX → Modem RX | GPIO10 | 115200 |
| ESP RX ← Modem TX | GPIO9 | 115200 |
| RTS/CTS | Disabled | — |

Supported modules: SIM7600E (4G), SIM7600G (4G+GPS), SIM800L (2G)
Firmware DCE type is selectable via `menuconfig` → `Modem Manager Configuration`.

### RS485 UART

| Signal | GPIO | Default Baud |
|---|---|---|
| TXD | GPIO7 | 9600 |
| RXD | GPIO15 | 9600 |
| Mode | Half-duplex | — |

### Ethernet W5500 (SPI)

| Signal | GPIO |
|---|---|
| SCK | GPIO42 |
| MOSI | GPIO43 |
| MISO | GPIO44 |
| CS | GPIO41 |
| INT | GPIO2 |
| RST | GPIO1 |

**Note:** W5500 driver requires ESP-IDF ≥5.3 or `espressif/esp_eth_w5500` component.

### SD Card (SPI)

| Signal | GPIO | SPI Host |
|---|---|---|
| MOSI | GPIO12 | SPI2_HOST |
| SCK | GPIO13 | SPI2_HOST |
| MISO | GPIO14 | SPI2_HOST |
| CS | GPIO11 | SPI2_HOST |
| CD (Card Detect) | GPIO21 | — |

### Analog Inputs (0–10V via Voltage Divider)

| Channel | GPIO | ADC Channel | Range |
|---|---|---|---|
| Analog 1 | GPIO4 | ADC1_CH3 | 0–10V (divider 1:11) |
| Analog 2 | GPIO5 | ADC1_CH4 | 0–10V (divider 1:11) |

### Free GPIOs (Expansion)

| GPIO | Default Config | Notes |
|---|---|---|
| GPIO6 | Input, pull-up | No PCB pull-up |
| GPIO38 | Input, pull-up | No PCB pull-up |

### System Buttons

| Button | GPIO | Function |
|---|---|---|
| S1 (Reset) | EN | ESP32 hardware reset |
| S2 (Download) | GPIO0 | Boot mode select (hold + press RST) |
