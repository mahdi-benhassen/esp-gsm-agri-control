#pragma once

#include "esp_err.h"
#include <stdbool.h>

esp_err_t onewire_init(int pin);
esp_err_t onewire_deinit(int pin);
esp_err_t onewire_read_temperature(int pin, float *temp_c);
bool onewire_is_present(int pin);
