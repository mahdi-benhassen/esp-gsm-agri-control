#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

esp_err_t rtc_manager_init(void);
esp_err_t rtc_manager_get_time(struct tm *timeinfo);
esp_err_t rtc_manager_set_time(const struct tm *timeinfo);
esp_err_t rtc_manager_get_unixtime(uint32_t *timestamp);
float rtc_manager_get_temperature(void);
