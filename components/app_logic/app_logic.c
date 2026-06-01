#include "app_logic.h"
#include "config_store.h"
#include "digital_input.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcd_display.h"
#include "modem_manager.h"
#include "mqtt_client_wrapper.h"
#include "relay_control.h"
#include "rtc_manager.h"
#include "sensor_hub.h"
#include "system_monitor.h"
#include "web_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "APP_LOGIC";
static TaskHandle_t s_task = NULL;

static void on_sensor_data(void *arg, esp_event_base_t base,
                           int32_t event_id, void *data) {
  if (event_id == SENSOR_EVENT_NEW_DATA) {
    sensor_data_t *sd = (sensor_data_t *)data;
    ESP_LOGI(TAG, "Sensor: T1=%.1fC T2=%.1fC Valid=%d",
             sd->temperature_c, sd->soil_moisture_pct, sd->sensors_valid);
  }
}

static void on_input_changed(void *arg, esp_event_base_t base,
                             int32_t event_id, void *data) {
  if (event_id == DIGITAL_INPUT_EVENT_CHANGED) {
    digital_input_event_t *evt = (digital_input_event_t *)data;
    ESP_LOGI(TAG, "Input %d changed: %s", evt->channel,
             evt->state ? "ON" : "OFF");
    mqtt_wrapper_publish_input_state(evt->channel, evt->state);
  }
}

static void on_relay_changed(void *arg, esp_event_base_t base,
                             int32_t event_id, void *data) {
  if (event_id == RELAY_EVENT_STATE_CHANGED) {
    relay_event_data_t *rd = (relay_event_data_t *)data;
    mqtt_wrapper_publish_relay_state(rd->channel, rd->state);
  }
}

static void on_mqtt_connected(void *arg, esp_event_base_t base,
                              int32_t event_id, void *data) {
  ESP_LOGI(TAG, "MQTT connected - publishing initial state");

  for (int i = 0; i < RELAY_CH_MAX; i++) {
    mqtt_wrapper_publish_relay_state(i, relay_get(i));
  }
  for (int i = 0; i < 2; i++) {
    mqtt_wrapper_publish_input_state(i, digital_input_get(i));
  }

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
  ESP_LOGI(TAG, "App Logic task started.");

  uint32_t last_sensor_pub = 0;
  uint32_t last_status_pub = 0;
  uint32_t last_lcd_update = 0;
  uint32_t last_input_scan = 0;

  while (1) {
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    app_config_t cfg;
    config_store_get_snapshot(&cfg);

    // Scan digital inputs
    if (cfg.input_debounce_ms > 0 &&
        (now - last_input_scan) >= (cfg.input_debounce_ms / 1000 + 1)) {
      for (int i = 0; i < 2; i++) {
        digital_input_get(i);
      }
      last_input_scan = now;
    }

    // Publish sensor data
    if ((now - last_sensor_pub) >= cfg.mqtt_publish_interval_sec) {
      sensor_data_t sd;
      if (sensor_hub_read(&sd) == ESP_OK && mqtt_wrapper_is_connected()) {
        mqtt_wrapper_publish_sensor_data(&sd);
        last_sensor_pub = now;
      }
    }

    // Publish system status
    if ((now - last_status_pub) >= CONFIG_APP_PUBLISH_STATUS_INTERVAL_SEC) {
      system_status_t status;
      if (system_monitor_get_status(&status) == ESP_OK &&
          mqtt_wrapper_is_connected()) {
        char *json = system_monitor_status_to_json(&status);
        if (json) {
          mqtt_wrapper_publish_status(json);
          free(json);
          last_status_pub = now;
        }
      }
    }

    // Update LCD
    if ((now - last_lcd_update) >= CONFIG_LCD_REFRESH_INTERVAL_SEC) {
      struct tm rtc_now;
      char line1[22] = "KC868-A2v3";
      char line2[22] = "";
      char line3[22] = "";

      if (rtc_manager_get_time(&rtc_now) == ESP_OK) {
        snprintf(line1, sizeof(line1), "%02d:%02d:%02d",
                 rtc_now.tm_hour, rtc_now.tm_min, rtc_now.tm_sec);
      }

      snprintf(line2, sizeof(line2), "R1:%s R2:%s",
               relay_get(RELAY_CH_1) ? "ON " : "OFF",
               relay_get(RELAY_CH_2) ? "ON " : "OFF");

      int rssi = modem_manager_get_rssi();

      char wifi_ip[16] = "";
      web_server_wifi_get_ip(wifi_ip, sizeof(wifi_ip));
      if (wifi_ip[0] && strcmp(wifi_ip, "0.0.0.0") != 0) {
        snprintf(line2, sizeof(line2), "WiFi: %s", wifi_ip);
        snprintf(line3, sizeof(line3), "R1:%s R2:%s 4G:%ddBm",
                 relay_get(RELAY_CH_1) ? "ON " : "OFF",
                 relay_get(RELAY_CH_2) ? "ON " : "OFF", rssi);
      } else {
        snprintf(line2, sizeof(line2), "R1:%s R2:%s",
                 relay_get(RELAY_CH_1) ? "ON " : "OFF",
                 relay_get(RELAY_CH_2) ? "ON " : "OFF");
        snprintf(line3, sizeof(line3), "4G:%ddBm MQTT:%s",
                 rssi, mqtt_wrapper_is_connected() ? "OK" : "NO");
      }

      lcd_display_show(line1, line2, line3);
      last_lcd_update = now;
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

esp_err_t app_logic_start(void) {
  esp_event_handler_register(SENSOR_EVENTS, SENSOR_EVENT_NEW_DATA,
                             &on_sensor_data, NULL);
  esp_event_handler_register(DIGITAL_INPUT_EVENTS, DIGITAL_INPUT_EVENT_CHANGED,
                             &on_input_changed, NULL);
  esp_event_handler_register(RELAY_EVENTS, RELAY_EVENT_STATE_CHANGED,
                             &on_relay_changed, NULL);
  esp_event_handler_register(MQTT_APP_EVENTS, MQTT_APP_EVENT_CONNECTED,
                             &on_mqtt_connected, NULL);

  BaseType_t ok = xTaskCreatePinnedToCore(
      app_logic_task, "app_logic", CONFIG_APP_LOGIC_TASK_STACK_SIZE, NULL,
      CONFIG_APP_LOGIC_TASK_PRIORITY, &s_task, 1);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Failed to create app logic task");
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG, "Application Logic started");
  return ESP_OK;
}
