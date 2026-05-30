#include "config_store.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "CONFIG_STORE";
static app_config_t s_config;
static SemaphoreHandle_t s_mutex = NULL;

static const app_config_t s_defaults = {
    .sensor_read_interval_sec = 30,
    .mqtt_publish_interval_sec = 60,
    .soil_moisture_threshold = 30.0f,
    .auto_irrigation_enabled = true,
    .irrigation_duration_sec = 300,
    .mqtt_broker_uri = "mqtt://broker.hivemq.com",
    .device_name = "agri-node-01"
};

esp_err_t config_store_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    // Copy defaults initially
    memcpy(&s_config, &s_defaults, sizeof(app_config_t));
    
    return config_store_load(&s_config);
}

esp_err_t config_store_load(app_config_t *cfg)
{
    nvs_handle_t handle;
    esp_err_t err;

    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);

    err = nvs_open("app_config", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Error opening NVS handle: %s. Using defaults.", esp_err_to_name(err));
        if (s_mutex) xSemaphoreGive(s_mutex);
        return err;
    }

    nvs_get_u32(handle, "sens_intvl", &cfg->sensor_read_interval_sec);
    nvs_get_u32(handle, "mqtt_intvl", &cfg->mqtt_publish_interval_sec);
    
    int32_t val_moist_th = 0;
    if (nvs_get_i32(handle, "moist_th", &val_moist_th) == ESP_OK) {
        cfg->soil_moisture_threshold = (float)val_moist_th / 100.0f;
    } else {
        cfg->soil_moisture_threshold = s_defaults.soil_moisture_threshold;
    }

    uint8_t val_auto_irr = 0;
    if (nvs_get_u8(handle, "auto_irr", &val_auto_irr) == ESP_OK) {
        cfg->auto_irrigation_enabled = (val_auto_irr != 0);
    } else {
        cfg->auto_irrigation_enabled = s_defaults.auto_irrigation_enabled;
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

    nvs_close(handle);
    if (s_mutex) xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "Config loaded successfully. Name: %s, Threshold: %.1f", cfg->device_name, (double)cfg->soil_moisture_threshold);
    return ESP_OK;
}

esp_err_t config_store_save(const app_config_t *cfg)
{
    nvs_handle_t handle;
    esp_err_t err;

    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);

    err = nvs_open("app_config", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS for save: %s", esp_err_to_name(err));
        if (s_mutex) xSemaphoreGive(s_mutex);
        return err;
    }

    nvs_set_u32(handle, "sens_intvl", cfg->sensor_read_interval_sec);
    nvs_set_u32(handle, "mqtt_intvl", cfg->mqtt_publish_interval_sec);
    nvs_set_i32(handle, "moist_th", (int32_t)(cfg->soil_moisture_threshold * 100.0f));
    nvs_set_u8(handle, "auto_irr", cfg->auto_irrigation_enabled ? 1 : 0);
    nvs_set_u32(handle, "irr_dur", cfg->irrigation_duration_sec);
    nvs_set_str(handle, "mqtt_uri", cfg->mqtt_broker_uri);
    nvs_set_str(handle, "dev_name", cfg->device_name);

    err = nvs_commit(handle);
    nvs_close(handle);
    
    // Copy local cache
    memcpy(&s_config, cfg, sizeof(app_config_t));

    if (s_mutex) xSemaphoreGive(s_mutex);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Config saved to NVS successfully");
    } else {
        ESP_LOGE(TAG, "NVS Commit failed: %s", esp_err_to_name(err));
    }
    return err;
}

const app_config_t *config_store_get(void)
{
    return &s_config;
}

esp_err_t config_store_set_field(const char *key, const char *value)
{
    app_config_t temp_config;
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    memcpy(&temp_config, &s_config, sizeof(app_config_t));
    if (s_mutex) xSemaphoreGive(s_mutex);

    bool changed = false;
    
    if (strcmp(key, "sensor_interval") == 0) {
        temp_config.sensor_read_interval_sec = strtoul(value, NULL, 10);
        changed = true;
    } else if (strcmp(key, "mqtt_interval") == 0) {
        temp_config.mqtt_publish_interval_sec = strtoul(value, NULL, 10);
        changed = true;
    } else if (strcmp(key, "moisture_threshold") == 0) {
        temp_config.soil_moisture_threshold = strtof(value, NULL);
        changed = true;
    } else if (strcmp(key, "auto_irrigation") == 0) {
        temp_config.auto_irrigation_enabled = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
        changed = true;
    } else if (strcmp(key, "irrigation_duration") == 0) {
        temp_config.irrigation_duration_sec = strtoul(value, NULL, 10);
        changed = true;
    } else if (strcmp(key, "mqtt_broker") == 0) {
        strncpy(temp_config.mqtt_broker_uri, value, sizeof(temp_config.mqtt_broker_uri) - 1);
        temp_config.mqtt_broker_uri[sizeof(temp_config.mqtt_broker_uri) - 1] = '\0';
        changed = true;
    } else if (strcmp(key, "device_name") == 0) {
        strncpy(temp_config.device_name, value, sizeof(temp_config.device_name) - 1);
        temp_config.device_name[sizeof(temp_config.device_name) - 1] = '\0';
        changed = true;
    } else {
        ESP_LOGW(TAG, "Unknown config key: %s", key);
        return ESP_ERR_INVALID_ARG;
    }

    if (changed) {
        return config_store_save(&temp_config);
    }
    return ESP_OK;
}
