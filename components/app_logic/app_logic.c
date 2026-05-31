#include "app_logic.h"
#include "config_store.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client_wrapper.h"
#include "relay_control.h"
#include "sensor_hub.h"
#include "system_monitor.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "APP_LOGIC";
static TaskHandle_t s_task = NULL;
static bool s_auto_irrigation_armed = true;

static void on_new_sensor_data(void *arg, esp_event_base_t base,
                               int32_t event_id, void *data) {
  if (event_id == SENSOR_EVENT_NEW_DATA) {
    sensor_data_t *sensor_data = (sensor_data_t *)data;
    app_config_t cfg;
    if (config_store_get_snapshot(&cfg) != ESP_OK)
      return;

    ESP_LOGI(TAG,
             "New sensor data received. Soil: %.1f%%, Temp: %.1f C, Valid: %d",
             sensor_data->soil_moisture_pct, sensor_data->temperature_c,
             sensor_data->sensors_valid);

    // Auto irrigation check. Use a re-arm band so dry readings after a timed
    // pump run do not immediately trigger another cycle until moisture
    // recovers.
    if (sensor_data->sensors_valid && cfg.auto_irrigation_enabled) {
      float rearm_threshold = cfg.soil_moisture_threshold +
                              (float)CONFIG_APP_IRRIGATION_HYSTERESIS_PCT;
      if (rearm_threshold > 100.0f) {
        rearm_threshold = 100.0f;
      }

      if (!s_auto_irrigation_armed &&
          sensor_data->soil_moisture_pct >= rearm_threshold) {
        s_auto_irrigation_armed = true;
        ESP_LOGI(TAG,
                 "Auto-irrigation re-armed at %.1f%% moisture (re-arm %.1f%%)",
                 sensor_data->soil_moisture_pct, (double)rearm_threshold);
      }

      bool pump_active = relay_get(RELAY_CH_PUMP_1);
      if (s_auto_irrigation_armed && !pump_active &&
          (sensor_data->soil_moisture_pct < cfg.soil_moisture_threshold)) {
        ESP_LOGW(TAG,
                 "Soil moisture (%.1f%%) below threshold (%.1f%%)! Starting "
                 "auto-irrigation for %lu seconds.",
                 sensor_data->soil_moisture_pct, cfg.soil_moisture_threshold,
                 (unsigned long)cfg.irrigation_duration_sec);

        esp_err_t relay_err =
            relay_set_timed(RELAY_CH_PUMP_1, cfg.irrigation_duration_sec);
        if (relay_err == ESP_OK) {
          s_auto_irrigation_armed = false;
        } else {
          ESP_LOGE(TAG, "Failed to start auto-irrigation: %s",
                   esp_err_to_name(relay_err));
        }
      }
    } else if (!cfg.auto_irrigation_enabled) {
      s_auto_irrigation_armed = true;
    }
  }
}

static void on_relay_changed(void *arg, esp_event_base_t base, int32_t event_id,
                             void *data) {
  if (event_id == RELAY_EVENT_STATE_CHANGED) {
    relay_event_data_t *relay_data = (relay_event_data_t *)data;
    ESP_LOGI(TAG, "Relay %d state changed to %s. Publishing status...",
             relay_data->channel, relay_data->state ? "ON" : "OFF");

    esp_err_t err = mqtt_wrapper_publish_relay_state(relay_data->channel,
                                                     relay_data->state);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "Failed to publish relay %d state: %s", relay_data->channel,
               esp_err_to_name(err));
    }
  }
}

static void on_mqtt_connected(void *arg, esp_event_base_t base,
                              int32_t event_id, void *data) {
  ESP_LOGI(TAG, "MQTT connected. Performing initial status updates.");

  // Publish current relay states
  for (int i = 0; i < RELAY_CH_MAX; i++) {
    mqtt_wrapper_publish_relay_state(i, relay_get(i));
  }

  // Publish initial system status
  system_status_t status;
  if (system_monitor_get_status(&status) == ESP_OK) {
    char *json = system_monitor_status_to_json(&status);
    if (json) {
      mqtt_wrapper_publish_status(json);
      free(json);
    }
  }
}

static void app_logic_task(void *pvParameters) {
  ESP_LOGI(TAG, "Main App Logic loop task started.");

  uint32_t last_sensor_pub = 0;
  uint32_t last_status_pub = 0;

  while (1) {
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    app_config_t cfg;
    if (config_store_get_snapshot(&cfg) == ESP_OK) {
      // Check if time to publish sensors
      if ((now - last_sensor_pub) >= cfg.mqtt_publish_interval_sec) {
        sensor_data_t sensor_data;
        if (sensor_hub_read(&sensor_data) == ESP_OK) {
          if (mqtt_wrapper_is_connected()) {
            ESP_LOGI(TAG, "Publishing periodic sensor telemetry...");
            mqtt_wrapper_publish_sensor_data(&sensor_data);
            last_sensor_pub = now;
          }
        }
      }

      // Check if time to publish system health status
      if ((now - last_status_pub) >= CONFIG_APP_PUBLISH_STATUS_INTERVAL_SEC) {
        system_status_t status;
        if (system_monitor_get_status(&status) == ESP_OK) {
          if (mqtt_wrapper_is_connected()) {
            ESP_LOGI(TAG, "Publishing periodic system diagnostics...");
            char *json = system_monitor_status_to_json(&status);
            if (json) {
              mqtt_wrapper_publish_status(json);
              free(json);
              last_status_pub = now;
            }
          }
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

esp_err_t app_logic_start(void) {
  // Register events on default event loop
  esp_err_t err = esp_event_handler_register(
      SENSOR_EVENTS, SENSOR_EVENT_NEW_DATA, &on_new_sensor_data, NULL);
  if (err != ESP_OK)
    return err;

  err = esp_event_handler_register(RELAY_EVENTS, RELAY_EVENT_STATE_CHANGED,
                                   &on_relay_changed, NULL);
  if (err != ESP_OK)
    return err;

  err = esp_event_handler_register(MQTT_APP_EVENTS, MQTT_APP_EVENT_CONNECTED,
                                   &on_mqtt_connected, NULL);
  if (err != ESP_OK)
    return err;

  BaseType_t task_ok = xTaskCreatePinnedToCore(
      app_logic_task, "app_logic", CONFIG_APP_LOGIC_TASK_STACK_SIZE, NULL,
      CONFIG_APP_LOGIC_TASK_PRIORITY, &s_task, 1);
  if (task_ok != pdPASS) {
    ESP_LOGE(TAG, "Failed to create application logic task");
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG, "Application Logic started successfully.");
  return ESP_OK;
}
