#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

esp_err_t rs485_init(void);
esp_err_t rs485_send(const uint8_t *data, size_t len);
esp_err_t rs485_send_modbus_rtu(uint8_t slave_addr, uint8_t func_code,
                                 uint16_t reg, uint16_t value);
esp_err_t rs485_set_baudrate(int baudrate);
