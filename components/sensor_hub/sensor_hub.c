#include "sensor_hub.h"
#include "config_store.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "SENSOR_HUB";

ESP_EVENT_DEFINE_BASE(SENSOR_EVENTS);

// Internal driver declarations (implemented in other files of same component)
esp_err_t soil_moisture_init(void);
esp_err_t soil_moisture_read(float *moisture_pct);
esp_err_t dht22_init(gpio_num_t pin);
esp_err_t dht22_read(float *temperature, float *humidity);

static sensor_data_t s_latest_data = {0};
static SemaphoreHandle_t s_data_mutex = NULL;
static TaskHandle_t s_task_handle = NULL;

static void sensor_read_task(void *pvParameters) {
  ESP_LOGI(TAG, "Sensor Hub Periodic reading task started.");

  while (1) {
    float moisture = 0.0f;
    float temp = 0.0f;
    float hum = 0.0f;

    esp_err_t err_soil = soil_moisture_read(&moisture);
    esp_err_t err_dht = dht22_read(&temp, &hum);

    xSemaphoreTake(s_data_mutex, portMAX_DELAY);

    s_latest_data.timestamp_ms = esp_timer_get_time() / 1000;

    if (err_soil == ESP_OK && err_dht == ESP_OK) {
      s_latest_data.soil_moisture_pct = moisture;
      s_latest_data.temperature_c = temp;
      s_latest_data.humidity_pct = hum;
      s_latest_data.sensors_valid = true;
      ESP_LOGI(TAG, "Readings: Soil=%.1f%%, Temp=%.1f C, Hum=%.1f%%",
               (double)moisture, (double)temp, (double)hum);
    } else {
      s_latest_data.sensors_valid = false;
      ESP_LOGE(TAG, "Sensor read errors. Soil err: %s, DHT err: %s",
               esp_err_to_name(err_soil), esp_err_to_name(err_dht));
    }

    sensor_data_t copy_data;
    memcpy(&copy_data, &s_latest_data, sizeof(sensor_data_t));

    xSemaphoreGive(s_data_mutex);

    // Post event
    esp_event_post(SENSOR_EVENTS, SENSOR_EVENT_NEW_DATA, &copy_data,
                   sizeof(copy_data), pdMS_TO_TICKS(100));

    app_config_t cfg;
    uint32_t delay_ms = CONFIG_SENSOR_READ_INTERVAL_MS;
    if (config_store_get_snapshot(&cfg) == ESP_OK &&
        cfg.sensor_read_interval_sec > 0) {
      delay_ms = cfg.sensor_read_interval_sec * 1000U;
    }
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
  }
}

esp_err_t sensor_hub_init(void) {
  s_data_mutex = xSemaphoreCreateMutex();
  if (s_data_mutex == NULL) {
    return ESP_ERR_NO_MEM;
  }

  esp_err_t err = soil_moisture_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Soil moisture init failed!");
    return err;
  }

  err = dht22_init((gpio_num_t)CONFIG_DHT22_GPIO);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "DHT22 init failed!");
    return err;
  }

  BaseType_t task_ok = xTaskCreatePinnedToCore(
      sensor_read_task, "sensor_read", 4096, NULL, 6, &s_task_handle, 1);
  if (task_ok != pdPASS) {
    ESP_LOGE(TAG, "Failed to create sensor read task");
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG, "Sensor Hub successfully initialized.");
  return ESP_OK;
}

esp_err_t sensor_hub_read(sensor_data_t *data) {
  if (s_data_mutex == NULL)
    return ESP_ERR_INVALID_STATE;

  xSemaphoreTake(s_data_mutex, portMAX_DELAY);
  memcpy(data, &s_latest_data, sizeof(sensor_data_t));
  xSemaphoreGive(s_data_mutex);

  return ESP_OK;
}

const sensor_data_t *sensor_hub_get_latest(void) {
  static sensor_data_t snapshot;
  if (sensor_hub_read(&snapshot) != ESP_OK) {
    return NULL;
  }
  return &snapshot;
}
