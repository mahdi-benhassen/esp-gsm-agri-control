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

#define CONFIG_MIN_INTERVAL_SEC 5U
#define CONFIG_MAX_INTERVAL_SEC 86400U
#define CONFIG_MIN_IRRIGATION_SEC 1U
#define CONFIG_MAX_IRRIGATION_SEC CONFIG_RELAY_MAX_ON_TIME_SEC

static const app_config_t s_defaults = {.sensor_read_interval_sec = 30,
                                        .mqtt_publish_interval_sec = 60,
                                        .soil_moisture_threshold = 30.0f,
                                        .auto_irrigation_enabled = true,
                                        .irrigation_duration_sec = 300,
                                        .mqtt_broker_uri =
                                            "mqtt://broker.hivemq.com",
                                        .device_name = "agri-node-01"};

static void sanitize_config(app_config_t *cfg) {
  if (cfg->sensor_read_interval_sec < CONFIG_MIN_INTERVAL_SEC ||
      cfg->sensor_read_interval_sec > CONFIG_MAX_INTERVAL_SEC) {
    cfg->sensor_read_interval_sec = s_defaults.sensor_read_interval_sec;
  }
  if (cfg->mqtt_publish_interval_sec < CONFIG_MIN_INTERVAL_SEC ||
      cfg->mqtt_publish_interval_sec > CONFIG_MAX_INTERVAL_SEC) {
    cfg->mqtt_publish_interval_sec = s_defaults.mqtt_publish_interval_sec;
  }
  if (cfg->soil_moisture_threshold < 0.0f ||
      cfg->soil_moisture_threshold > 100.0f) {
    cfg->soil_moisture_threshold = s_defaults.soil_moisture_threshold;
  }
  if (cfg->irrigation_duration_sec < CONFIG_MIN_IRRIGATION_SEC ||
      cfg->irrigation_duration_sec > CONFIG_MAX_IRRIGATION_SEC) {
    cfg->irrigation_duration_sec = s_defaults.irrigation_duration_sec;
  }
  cfg->mqtt_broker_uri[sizeof(cfg->mqtt_broker_uri) - 1] = '\0';
  cfg->device_name[sizeof(cfg->device_name) - 1] = '\0';
}

static esp_err_t parse_u32_range(const char *value, uint32_t min, uint32_t max,
                                 uint32_t *out) {
  if (value == NULL || out == NULL || value[0] == '\0') {
    return ESP_ERR_INVALID_ARG;
  }

  char *end = NULL;
  errno = 0;
  unsigned long parsed = strtoul(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' || parsed > UINT32_MAX) {
    return ESP_ERR_INVALID_ARG;
  }

  if (parsed < min || parsed > max) {
    ESP_LOGW(TAG, "Value %lu is outside allowed range [%lu, %lu]", parsed,
             (unsigned long)min, (unsigned long)max);
    return ESP_ERR_INVALID_ARG;
  }

  *out = (uint32_t)parsed;
  return ESP_OK;
}

static esp_err_t parse_float_range(const char *value, float min, float max,
                                   float *out) {
  if (value == NULL || out == NULL || value[0] == '\0') {
    return ESP_ERR_INVALID_ARG;
  }

  char *end = NULL;
  errno = 0;
  float parsed = strtof(value, &end);
  if (errno != 0 || end == value || *end != '\0') {
    return ESP_ERR_INVALID_ARG;
  }

  if (parsed < min || parsed > max) {
    ESP_LOGW(TAG, "Value %.2f is outside allowed range [%.2f, %.2f]",
             (double)parsed, (double)min, (double)max);
    return ESP_ERR_INVALID_ARG;
  }

  *out = parsed;
  return ESP_OK;
}

static esp_err_t parse_bool_value(const char *value, bool *out) {
  if (value == NULL || out == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

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
    ESP_LOGW(TAG, "Using default configuration after load failure: %s",
             esp_err_to_name(err));
    return ESP_OK;
  }
  return ESP_OK;
}

esp_err_t config_store_load(app_config_t *cfg) {
  if (cfg == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  nvs_handle_t handle;
  esp_err_t err;

  if (s_mutex)
    xSemaphoreTake(s_mutex, portMAX_DELAY);

  memcpy(cfg, &s_defaults, sizeof(app_config_t));

  err = nvs_open("app_config", NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Error opening NVS handle: %s. Using defaults.",
             esp_err_to_name(err));
    if (s_mutex)
      xSemaphoreGive(s_mutex);
    return err;
  }

  nvs_get_u32(handle, "sens_intvl", &cfg->sensor_read_interval_sec);
  nvs_get_u32(handle, "mqtt_intvl", &cfg->mqtt_publish_interval_sec);

  int32_t val_moist_th = 0;
  if (nvs_get_i32(handle, "moist_th", &val_moist_th) == ESP_OK) {
    cfg->soil_moisture_threshold = (float)val_moist_th / 100.0f;
  }

  uint8_t val_auto_irr = 0;
  if (nvs_get_u8(handle, "auto_irr", &val_auto_irr) == ESP_OK) {
    cfg->auto_irrigation_enabled = (val_auto_irr != 0);
  }

  nvs_get_u32(handle, "irr_dur", &cfg->irrigation_duration_sec);

  size_t required_size = sizeof(cfg->mqtt_broker_uri);
  err = nvs_get_str(handle, "mqtt_uri", cfg->mqtt_broker_uri, &required_size);
  if (err != ESP_OK) {
    strcpy(cfg->mqtt_broker_uri, s_defaults.mqtt_broker_uri);
  }

  required_size = sizeof(cfg->device_name);
  err = nvs_get_str(handle, "dev_name", cfg->device_name, &required_size);
  if (err != ESP_OK) {
    strcpy(cfg->device_name, s_defaults.device_name);
  }

  sanitize_config(cfg);

  nvs_close(handle);
  if (s_mutex)
    xSemaphoreGive(s_mutex);

  ESP_LOGI(TAG, "Config loaded successfully. Name: %s, Threshold: %.1f",
           cfg->device_name, (double)cfg->soil_moisture_threshold);
  return ESP_OK;
}

esp_err_t config_store_save(const app_config_t *cfg) {
  if (cfg == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  nvs_handle_t handle;
  esp_err_t err;

  if (s_mutex)
    xSemaphoreTake(s_mutex, portMAX_DELAY);

  err = nvs_open("app_config", NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error opening NVS for save: %s", esp_err_to_name(err));
    if (s_mutex)
      xSemaphoreGive(s_mutex);
    return err;
  }

  if ((err = nvs_set_u32(handle, "sens_intvl",
                         cfg->sensor_read_interval_sec)) != ESP_OK ||
      (err = nvs_set_u32(handle, "mqtt_intvl",
                         cfg->mqtt_publish_interval_sec)) != ESP_OK ||
      (err = nvs_set_i32(handle, "moist_th",
                         (int32_t)(cfg->soil_moisture_threshold * 100.0f))) !=
          ESP_OK ||
      (err = nvs_set_u8(handle, "auto_irr",
                        cfg->auto_irrigation_enabled ? 1 : 0)) != ESP_OK ||
      (err = nvs_set_u32(handle, "irr_dur", cfg->irrigation_duration_sec)) !=
          ESP_OK ||
      (err = nvs_set_str(handle, "mqtt_uri", cfg->mqtt_broker_uri)) != ESP_OK ||
      (err = nvs_set_str(handle, "dev_name", cfg->device_name)) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to write config to NVS: %s", esp_err_to_name(err));
    nvs_close(handle);
    if (s_mutex)
      xSemaphoreGive(s_mutex);
    return err;
  }

  err = nvs_commit(handle);
  nvs_close(handle);
  if (err == ESP_OK) {
    memcpy(&s_config, cfg, sizeof(app_config_t));
    ESP_LOGI(TAG, "Config saved to NVS successfully");
  } else {
    ESP_LOGE(TAG, "NVS Commit failed: %s", esp_err_to_name(err));
  }

  if (s_mutex)
    xSemaphoreGive(s_mutex);
  return err;
}

const app_config_t *config_store_get(void) {
  static app_config_t snapshot;
  if (config_store_get_snapshot(&snapshot) != ESP_OK) {
    return NULL;
  }
  return &snapshot;
}

esp_err_t config_store_get_snapshot(app_config_t *cfg) {
  if (cfg == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (s_mutex)
    xSemaphoreTake(s_mutex, portMAX_DELAY);
  memcpy(cfg, &s_config, sizeof(app_config_t));
  if (s_mutex)
    xSemaphoreGive(s_mutex);
  return ESP_OK;
}

esp_err_t config_store_set_field(const char *key, const char *value) {
  if (key == NULL || value == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  app_config_t temp_config;
  esp_err_t err = config_store_get_snapshot(&temp_config);
  if (err != ESP_OK) {
    return err;
  }

  if (strcmp(key, "sensor_interval") == 0) {
    err =
        parse_u32_range(value, CONFIG_MIN_INTERVAL_SEC, CONFIG_MAX_INTERVAL_SEC,
                        &temp_config.sensor_read_interval_sec);
  } else if (strcmp(key, "mqtt_interval") == 0) {
    err =
        parse_u32_range(value, CONFIG_MIN_INTERVAL_SEC, CONFIG_MAX_INTERVAL_SEC,
                        &temp_config.mqtt_publish_interval_sec);
  } else if (strcmp(key, "moisture_threshold") == 0) {
    err = parse_float_range(value, 0.0f, 100.0f,
                            &temp_config.soil_moisture_threshold);
  } else if (strcmp(key, "auto_irrigation") == 0) {
    err = parse_bool_value(value, &temp_config.auto_irrigation_enabled);
  } else if (strcmp(key, "irrigation_duration") == 0) {
    err = parse_u32_range(value, CONFIG_MIN_IRRIGATION_SEC,
                          CONFIG_MAX_IRRIGATION_SEC,
                          &temp_config.irrigation_duration_sec);
  } else if (strcmp(key, "mqtt_broker") == 0) {
    if (value[0] == '\0' ||
        strlen(value) >= sizeof(temp_config.mqtt_broker_uri)) {
      err = ESP_ERR_INVALID_ARG;
    } else {
      strncpy(temp_config.mqtt_broker_uri, value,
              sizeof(temp_config.mqtt_broker_uri) - 1);
      temp_config.mqtt_broker_uri[sizeof(temp_config.mqtt_broker_uri) - 1] =
          '\0';
      err = ESP_OK;
    }
  } else if (strcmp(key, "device_name") == 0) {
    if (value[0] == '\0' || strlen(value) >= sizeof(temp_config.device_name)) {
      err = ESP_ERR_INVALID_ARG;
    } else {
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
    ESP_LOGW(TAG, "Invalid value for config key '%s': %s", key, value);
    return err;
  }

  return config_store_save(&temp_config);
}
