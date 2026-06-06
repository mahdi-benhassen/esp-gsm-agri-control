#pragma once

#include "esp_err.h"
#include <stdint.h>

#define ANALOG_INPUT_COUNT 2

esp_err_t analog_input_init(void);
int analog_input_read_mv(int channel);  // millivolts at terminal (0-10000)
int analog_input_read_raw(int channel); // raw ADC 0-4095
