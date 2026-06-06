#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

#define EEPROM_STORAGE_SIZE 256

esp_err_t eeprom_storage_init(void);
esp_err_t eeprom_storage_read(uint8_t addr, uint8_t *data, size_t len);
esp_err_t eeprom_storage_write(uint8_t addr, const uint8_t *data, size_t len);
bool eeprom_storage_detected(void);
