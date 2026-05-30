#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t sensor_read_interval_sec;
    uint32_t mqtt_publish_interval_sec;
    float soil_moisture_threshold;
    bool auto_irrigation_enabled;
    uint32_t irrigation_duration_sec;
    char mqtt_broker_uri[128];
    char device_name[32];
} app_config_t;

esp_err_t config_store_init(void);
esp_err_t config_store_load(app_config_t *cfg);
esp_err_t config_store_save(const app_config_t *cfg);
const app_config_t *config_store_get(void);
esp_err_t config_store_set_field(const char *key, const char *value);
