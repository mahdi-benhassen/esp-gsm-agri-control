#include "config_store.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "CONFIG_STORE";
static app_config_t s_config;
static SemaphoreHandle_t s_mutex = NULL;

#define CONFIG_MIN_INTERVAL_SEC  5U
#define CONFIG_MAX_INTERVAL_SEC  86400U
#define CONFIG_MIN_DEBOUNCE_MS   10U
#define CONFIG_MAX_DEBOUNCE_MS   5000U

static const app_config_t s_defaults = {
    .sensor_read_interval_sec = 30,
    .mqtt_publish_interval_sec = 60,
    .mqtt_broker_uri = "mqtt://broker.hivemq.com",
    .device_name = "KC868-A2v3",
    .input_1_inverted = true,
    .input_2_inverted = true,
    .input_debounce_ms = 50,
    .relay_interlock_enabled = false,
    .lcd_enabled = true,
    .sd_log_enabled = true,
};

static void sanitize_config(app_config_t *cfg) {
  if (cfg->sensor_read_interval_sec < CONFIG_MIN_INTERVAL_SEC ||
      cfg->sensor_read_interval_sec > CONFIG_MAX_INTERVAL_SEC) {
    cfg->sensor_read_interval_sec = s_defaults.sensor_read_interval_sec;
  }
  if (cfg->mqtt_publish_interval_sec < CONFIG_MIN_INTERVAL_SEC ||
      cfg->mqtt_publish_interval_sec > CONFIG_MAX_INTERVAL_SEC) {
    cfg->mqtt_publish_interval_sec = s_defaults.mqtt_publish_interval_sec;
  }
  if (cfg->input_debounce_ms < CONFIG_MIN_DEBOUNCE_MS ||
      cfg->input_debounce_ms > CONFIG_MAX_DEBOUNCE_MS) {
    cfg->input_debounce_ms = s_defaults.input_debounce_ms;
  }
  cfg->mqtt_broker_uri[sizeof(cfg->mqtt_broker_uri) - 1] = '\0';
  cfg->device_name[sizeof(cfg->device_name) - 1] = '\0';
}

static esp_err_t parse_u32_range(const char *value, uint32_t min, uint32_t max,
                                 uint32_t *out) {
  if (value == NULL || out == NULL || value[0] == '\0')
    return ESP_ERR_INVALID_ARG;
  char *end = NULL;
  errno = 0;
  unsigned long parsed = strtoul(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' || parsed > UINT32_MAX)
    return ESP_ERR_INVALID_ARG;
  if (parsed < min || parsed > max) {
    ESP_LOGW(TAG, "Value %lu outside range [%lu, %lu]",
             (unsigned long)parsed, (unsigned long)min, (unsigned long)max);
    return ESP_ERR_INVALID_ARG;
  }
  *out = (uint32_t)parsed;
  return ESP_OK;
}

static esp_err_t parse_bool_value(const char *value, bool *out) {
  if (value == NULL || out == NULL)
    return ESP_ERR_INVALID_ARG;
  if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) {
    *out = true;
    return ESP_OK;
  }
  if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0) {
    *out = false;
    return ESP_OK;
  }
  return ESP_ERR_INVALID_ARG;
}

esp_err_t config_store_init(void) {
  s_mutex = xSemaphoreCreateMutex();
  if (s_mutex == NULL) {
    return ESP_ERR_NO_MEM;
  }
  memcpy(&s_config, &s_defaults, sizeof(app_config_t));
  esp_err_t err = config_store_load(&s_config);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Using defaults after load failure: %s",
             esp_err_to_name(err));
    return ESP_OK;
  }
  return ESP_OK;
}

esp_err_t config_store_load(app_config_t *cfg) {
  if (cfg == NULL) return ESP_ERR_INVALID_ARG;
  nvs_handle_t handle;
  if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);

  memcpy(cfg, &s_defaults, sizeof(app_config_t));

  esp_err_t err = nvs_open("app_config", NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "NVS open failed: %s", esp_err_to_name(err));
    if (s_mutex) xSemaphoreGive(s_mutex);
    return err;
  }

  nvs_get_u32(handle, "sens_intvl", &cfg->sensor_read_interval_sec);
  nvs_get_u32(handle, "mqtt_intvl", &cfg->mqtt_publish_interval_sec);
  nvs_get_u32(handle, "dbnc_ms", &cfg->input_debounce_ms);

  uint8_t val8 = 0;
  if (nvs_get_u8(handle, "inv_in1", &val8) == ESP_OK)
    cfg->input_1_inverted = (val8 != 0);
  if (nvs_get_u8(handle, "inv_in2", &val8) == ESP_OK)
    cfg->input_2_inverted = (val8 != 0);
  if (nvs_get_u8(handle, "interlock", &val8) == ESP_OK)
    cfg->relay_interlock_enabled = (val8 != 0);
  if (nvs_get_u8(handle, "lcd_en", &val8) == ESP_OK)
    cfg->lcd_enabled = (val8 != 0);
  if (nvs_get_u8(handle, "sd_log", &val8) == ESP_OK)
    cfg->sd_log_enabled = (val8 != 0);

  size_t sz = sizeof(cfg->mqtt_broker_uri);
  if (nvs_get_str(handle, "mqtt_uri", cfg->mqtt_broker_uri, &sz) != ESP_OK)
    strcpy(cfg->mqtt_broker_uri, s_defaults.mqtt_broker_uri);
  sz = sizeof(cfg->device_name);
  if (nvs_get_str(handle, "dev_name", cfg->device_name, &sz) != ESP_OK)
    strcpy(cfg->device_name, s_defaults.device_name);

  sanitize_config(cfg);
  nvs_close(handle);
  if (s_mutex) xSemaphoreGive(s_mutex);
  ESP_LOGI(TAG, "Config loaded. Name: %s", cfg->device_name);
  return ESP_OK;
}

esp_err_t config_store_save(const app_config_t *cfg) {
  if (cfg == NULL) return ESP_ERR_INVALID_ARG;
  nvs_handle_t handle;
  if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);

  esp_err_t err = nvs_open("app_config", NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "NVS open for save failed: %s", esp_err_to_name(err));
    if (s_mutex) xSemaphoreGive(s_mutex);
    return err;
  }

  if ((err = nvs_set_u32(handle, "sens_intvl", cfg->sensor_read_interval_sec)) != ESP_OK ||
      (err = nvs_set_u32(handle, "mqtt_intvl", cfg->mqtt_publish_interval_sec)) != ESP_OK ||
      (err = nvs_set_u32(handle, "dbnc_ms", cfg->input_debounce_ms)) != ESP_OK ||
      (err = nvs_set_u8(handle, "inv_in1", cfg->input_1_inverted ? 1 : 0)) != ESP_OK ||
      (err = nvs_set_u8(handle, "inv_in2", cfg->input_2_inverted ? 1 : 0)) != ESP_OK ||
      (err = nvs_set_u8(handle, "interlock", cfg->relay_interlock_enabled ? 1 : 0)) != ESP_OK ||
      (err = nvs_set_u8(handle, "lcd_en", cfg->lcd_enabled ? 1 : 0)) != ESP_OK ||
      (err = nvs_set_u8(handle, "sd_log", cfg->sd_log_enabled ? 1 : 0)) != ESP_OK ||
      (err = nvs_set_str(handle, "mqtt_uri", cfg->mqtt_broker_uri)) != ESP_OK ||
      (err = nvs_set_str(handle, "dev_name", cfg->device_name)) != ESP_OK) {
    ESP_LOGE(TAG, "NVS write failed: %s", esp_err_to_name(err));
    nvs_close(handle);
    if (s_mutex) xSemaphoreGive(s_mutex);
    return err;
  }

  err = nvs_commit(handle);
  nvs_close(handle);
  if (err == ESP_OK) {
    memcpy(&s_config, cfg, sizeof(app_config_t));
    ESP_LOGI(TAG, "Config saved to NVS");
  } else {
    ESP_LOGE(TAG, "NVS commit failed: %s", esp_err_to_name(err));
  }
  if (s_mutex) xSemaphoreGive(s_mutex);
  return err;
}

const app_config_t *config_store_get(void) {
  ESP_LOGW(TAG, "config_store_get() is deprecated and unsafe; use config_store_get_snapshot()");
  return NULL;
}

esp_err_t config_store_get_snapshot(app_config_t *cfg) {
  if (cfg == NULL) return ESP_ERR_INVALID_ARG;
  if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
  memcpy(cfg, &s_config, sizeof(app_config_t));
  if (s_mutex) xSemaphoreGive(s_mutex);
  return ESP_OK;
}

esp_err_t config_store_set_field(const char *key, const char *value) {
  if (key == NULL || value == NULL) return ESP_ERR_INVALID_ARG;

  app_config_t temp_config;
  esp_err_t err = config_store_get_snapshot(&temp_config);
  if (err != ESP_OK) return err;

  if (strcmp(key, "sensor_interval") == 0) {
    err = parse_u32_range(value, CONFIG_MIN_INTERVAL_SEC,
                          CONFIG_MAX_INTERVAL_SEC,
                          &temp_config.sensor_read_interval_sec);
  } else if (strcmp(key, "mqtt_interval") == 0) {
    err = parse_u32_range(value, CONFIG_MIN_INTERVAL_SEC,
                          CONFIG_MAX_INTERVAL_SEC,
                          &temp_config.mqtt_publish_interval_sec);
  } else if (strcmp(key, "debounce_ms") == 0) {
    err = parse_u32_range(value, CONFIG_MIN_DEBOUNCE_MS,
                          CONFIG_MAX_DEBOUNCE_MS,
                          &temp_config.input_debounce_ms);
  } else if (strcmp(key, "input_1_inverted") == 0) {
    err = parse_bool_value(value, &temp_config.input_1_inverted);
  } else if (strcmp(key, "input_2_inverted") == 0) {
    err = parse_bool_value(value, &temp_config.input_2_inverted);
  } else if (strcmp(key, "interlock") == 0) {
    err = parse_bool_value(value, &temp_config.relay_interlock_enabled);
  } else if (strcmp(key, "lcd_enabled") == 0) {
    err = parse_bool_value(value, &temp_config.lcd_enabled);
  } else if (strcmp(key, "sd_log_enabled") == 0) {
    err = parse_bool_value(value, &temp_config.sd_log_enabled);
  } else if (strcmp(key, "mqtt_broker") == 0) {
    if (value[0] == '\0' || strlen(value) >= sizeof(temp_config.mqtt_broker_uri))
      err = ESP_ERR_INVALID_ARG;
    else {
      strncpy(temp_config.mqtt_broker_uri, value,
              sizeof(temp_config.mqtt_broker_uri) - 1);
      temp_config.mqtt_broker_uri[sizeof(temp_config.mqtt_broker_uri) - 1] = '\0';
      err = ESP_OK;
    }
  } else if (strcmp(key, "device_name") == 0) {
    if (value[0] == '\0' || strlen(value) >= sizeof(temp_config.device_name))
      err = ESP_ERR_INVALID_ARG;
    else {
      strncpy(temp_config.device_name, value,
              sizeof(temp_config.device_name) - 1);
      temp_config.device_name[sizeof(temp_config.device_name) - 1] = '\0';
      err = ESP_OK;
    }
  } else {
    ESP_LOGW(TAG, "Unknown config key: %s", key);
    return ESP_ERR_INVALID_ARG;
  }

  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Invalid value for key '%s': %s", key, value);
    return err;
  }
  return config_store_save(&temp_config);
}
