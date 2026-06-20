#pragma once

#include "esp_err.h"
#include "driver/i2c.h"

esp_err_t i2c_bus_manager_init(void);
bool i2c_bus_manager_is_initialized(void);

i2c_port_t i2c_bus_manager_get_port(void);
