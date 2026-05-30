#pragma once

#include "esp_err.h"
#include "esp_event.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float soil_moisture_pct;   // 0-100%
    float temperature_c;       // Celsius
    float humidity_pct;        // 0-100%
    bool  sensors_valid;       // All readings OK
    int64_t timestamp_ms;      // esp_timer_get_time() / 1000
} sensor_data_t;

// Custom event declarations
ESP_EVENT_DECLARE_BASE(SENSOR_EVENTS);
enum { SENSOR_EVENT_NEW_DATA };

esp_err_t sensor_hub_init(void);
esp_err_t sensor_hub_read(sensor_data_t *data);
const sensor_data_t *sensor_hub_get_latest(void);
