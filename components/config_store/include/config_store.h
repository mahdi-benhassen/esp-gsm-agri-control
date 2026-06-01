#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint32_t sensor_read_interval_sec;
  uint32_t mqtt_publish_interval_sec;
  char mqtt_broker_uri[128];
  char device_name[32];
  bool input_1_inverted;
  bool input_2_inverted;
  uint32_t input_debounce_ms;
  bool relay_interlock_enabled;
  bool lcd_enabled;
  bool sd_log_enabled;
} app_config_t;

esp_err_t config_store_init(void);
esp_err_t config_store_load(app_config_t *cfg);
esp_err_t config_store_save(const app_config_t *cfg);
const app_config_t *config_store_get(void);
esp_err_t config_store_get_snapshot(app_config_t *cfg);
esp_err_t config_store_set_field(const char *key, const char *value);
