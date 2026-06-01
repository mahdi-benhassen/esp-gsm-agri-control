#pragma once

#include "esp_err.h"
#include "esp_event.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  float temperature_c;
  float humidity_pct;
  float soil_moisture_pct;
  bool sensors_valid;
  int64_t timestamp_ms;
} sensor_data_t;

ESP_EVENT_DECLARE_BASE(SENSOR_EVENTS);
enum { SENSOR_EVENT_NEW_DATA };

esp_err_t sensor_hub_init(void);
esp_err_t sensor_hub_read(sensor_data_t *data);
const sensor_data_t *sensor_hub_get_latest(void);
