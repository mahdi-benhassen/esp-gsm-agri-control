# Agent Guide — KC868-A2v3 Firmware

This file contains agent-focused guidance for working on the KinCony KC868-A2v3 ESP-IDF firmware.

## Project Overview

- **Target:** ESP32-S3-WROOM-1 (N16R8 — 16MB Flash, 8MB PSRAM)
- **Framework:** ESP-IDF v5.2+
- **Build system:** CMake / `idf.py`
- **Architecture:** Event-driven components on `esp_event` loop
- **License:** MIT (see `LICENSE`)

## Build & Test

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

CI uses `.github/workflows/build.yml` with `espressif/esp-idf-ci-action@v1`.

Before committing, ensure:

1. `idf.py build` succeeds.
2. `idf.py size` shows sensible flash/heap budgets.
3. No new compiler warnings introduced (treat `-Wall` warnings as errors in review).

## Component Rules

1. **One responsibility per component.** Each component lives under `components/<name>/` with `include/<name>.h` and `<name>.c`.
2. **Public API headers must be C-compatible.** Use `<stdbool.h>`, `<stdint.h>`, and `extern "C"` guards.
3. **Components must not call each other directly.** Use `esp_event` posts and subscriptions.
4. **Hardware ownership is centralized.**
   - `i2c_bus_manager` owns `I2C_NUM_0`.
   - `relay_control` owns relay GPIOs.
   - `digital_input` / `analog_input` own input peripherals.
   - `modem_manager` owns the modem UART.
5. **Return `esp_err_t` from init functions.** Propagate errors to `app_main()`; do not silently ignore failures.
6. **No dynamic allocation without a fallback.** Use static buffers where possible; check `malloc`/`calloc` results and handle OOM gracefully.

## Coding Conventions

- C11. Avoid GNU extensions not supported by the ESP-IDF RISC-V/xtensa toolchain.
- Use fixed-width types (`uint32_t`, `int16_t`) for protocol, hardware, and serialized data.
- Prefer `snprintf` and `strncpy` over `strcpy`; always null-terminate.
- Wrap shared state in FreeRTOS mutexes. Hold mutexes for the shortest time possible.
- Never call blocking APIs from ISR context.
- Prefer `ESP_LOG*` macros over raw `printf`.
- Keep functions short and single-purpose. Avoid deeply nested conditionals.

## Security & Safety

- **No default secrets in source.** Web auth and MQTT defaults must be configurable via `menuconfig` only.
- **Sanitize all external input.** JSON command payloads, HTTP query/body data, and MQTT topics must be bounds-checked.
- **Limit memory exhaustion.** Reject oversized MQTT payloads (current limit: 1KB). Cap dynamic allocations.
- **Use TLS for remote MQTT.** Prefer `mqtts://` URIs in production.
- **Watchdogs must stay enabled.** Task WDT, interrupt WDT, and brownout detection are required in `sdkconfig.defaults`.
- **Safe-state on communication loss.** `safe_state` monitors MQTT heartbeat; if it expires, all relays turn OFF.
- **Factory reset must erase NVS.** `factory_reset` long-press handler must call `nvs_flash_erase()` and reboot.

## Memory & Performance

- Default stack sizes are calibrated for the tasks. If you increase a stack, document why.
- Avoid large stack arrays. Use static or heap buffers for frames > 256 bytes.
- Register the heap allocation failure callback in `app_main()` before other initialization.
- `sdkconfig.defaults` enables heap poisoning and stack protection for debug builds.

## Documentation

When adding or changing components, update:

- `docs/HARDWARE.md` — pin map, I2C addresses, peripheral assignments.
- `docs/ARCHITECTURE.md` — boot sequence, event bus, dependency graph.
- `README.md` — user-facing features, API endpoints, MQTT topics.
- `AGENTS.md` — this file, if conventions or build steps change.

## Commit Style

Use concise, descriptive messages in present tense:

```
feat: add analog input component with ADC calibration
fix: resolve I2C bus reentrancy in lcd_display
security: add HTTP Basic Auth and rate limiting to web_server
docs: update pin map for 4 digital inputs and 2 analog inputs
```

Keep commits focused. Do not mix unrelated refactoring with feature work in the same commit.

## Common Pitfalls

- **I2C ordering:** always call `i2c_bus_manager_init()` before `rtc_manager_init()`, `lcd_display_init()`, or `eeprom_storage_init()`.
- **Modem model:** select the correct modem in `menuconfig` (`SIM7600` vs `SIM800L`) before building for your hardware.
- **ADC calibration:** `adc_cali_create_scheme_curve_fitting()` is used for ESP32-S3. Verify the selected attenuation covers your input voltage range.
- **Partition table:** after changing `partitions.csv`, run `idf.py fullclean` then rebuild.
- **Factory reset GPIO:** GPIO38 is reserved for factory reset; do not use it for user I/O.
