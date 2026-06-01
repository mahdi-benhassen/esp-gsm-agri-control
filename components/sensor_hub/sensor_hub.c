#include "sensor_hub.h"
#include "config_store.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "onewire_sensor.h"
#include <string.h>

static const char *TAG = "SENSOR_HUB";

ESP_EVENT_DEFINE_BASE(SENSOR_EVENTS);

static sensor_data_t s_latest_data = {0};
static SemaphoreHandle_t s_data_mutex = NULL;
static TaskHandle_t s_task_handle = NULL;

static void sensor_read_task(void *pvParameters) {
  ESP_LOGI(TAG, "Sensor Hub task started.");

  while (1) {
    float temp1 = -999.0f;
    float temp2 = -999.0f;
    bool valid = true;

    if (onewire_read_temperature(CONFIG_ONEWIRE_TMP1_GPIO, &temp1) != ESP_OK) {
      ESP_LOGW(TAG, "TMP1 (GPIO %d) read failed", CONFIG_ONEWIRE_TMP1_GPIO);
      valid = false;
    }

    if (onewire_read_temperature(CONFIG_ONEWIRE_TMP2_GPIO, &temp2) != ESP_OK) {
      ESP_LOGW(TAG, "TMP2 (GPIO %d) read failed", CONFIG_ONEWIRE_TMP2_GPIO);
      valid = false;
    }

    xSemaphoreTake(s_data_mutex, portMAX_DELAY);
    s_latest_data.timestamp_ms = esp_timer_get_time() / 1000;
    s_latest_data.temperature_c = temp1;
    s_latest_data.soil_moisture_pct = temp2;  // repurposed as temp2
    s_latest_data.humidity_pct = 0.0f;
    s_latest_data.sensors_valid = valid;

    sensor_data_t copy_data;
    memcpy(&copy_data, &s_latest_data, sizeof(sensor_data_t));
    xSemaphoreGive(s_data_mutex);

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
  if (s_data_mutex == NULL) return ESP_ERR_NO_MEM;

  onewire_init(CONFIG_ONEWIRE_TMP1_GPIO);
  onewire_init(CONFIG_ONEWIRE_TMP2_GPIO);

  BaseType_t ok = xTaskCreatePinnedToCore(sensor_read_task, "sensor_read",
                                          4096, NULL, 6, &s_task_handle, 1);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Failed to create sensor task");
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG, "Sensor Hub initialized (DS18B20 on GPIO %d, %d)",
           CONFIG_ONEWIRE_TMP1_GPIO, CONFIG_ONEWIRE_TMP2_GPIO);
  return ESP_OK;
}

esp_err_t sensor_hub_read(sensor_data_t *data) {
  if (s_data_mutex == NULL || data == NULL) return ESP_ERR_INVALID_STATE;
  xSemaphoreTake(s_data_mutex, portMAX_DELAY);
  memcpy(data, &s_latest_data, sizeof(sensor_data_t));
  xSemaphoreGive(s_data_mutex);
  return ESP_OK;
}

const sensor_data_t *sensor_hub_get_latest(void) {
  static sensor_data_t snapshot;
  if (sensor_hub_read(&snapshot) != ESP_OK) return NULL;
  return &snapshot;
}
