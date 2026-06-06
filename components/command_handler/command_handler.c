#include "command_handler.h"
#include "cJSON.h"
#include "config_store.h"
#include "digital_input.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client_wrapper.h"
#include "relay_control.h"
#include "rtc_manager.h"
#include "sensor_hub.h"
#include "system_monitor.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "COMMAND_HANDLER";

static bool json_get_valid_channel(cJSON *root, relay_channel_t *channel) {
  cJSON *ch_item = cJSON_GetObjectItem(root, "channel");
  if (ch_item == NULL || !cJSON_IsNumber(ch_item)) return false;
  int ch = ch_item->valueint;
  if ((double)ch != ch_item->valuedouble || ch < 0 || ch >= RELAY_CH_MAX)
    return false;
  *channel = (relay_channel_t)ch;
  return true;
}

static esp_err_t send_response(const char *cmd, const char *result,
                                const char *msg) {
  cJSON *root = cJSON_CreateObject();
  if (root == NULL) return ESP_ERR_NO_MEM;
  cJSON_AddStringToObject(root, "cmd", cmd ? cmd : "unknown");
  cJSON_AddStringToObject(root, "result", result ? result : "error");
  cJSON_AddStringToObject(root, "message", msg ? msg : "");
  char *json_str = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json_str == NULL) return ESP_ERR_NO_MEM;
  esp_err_t err = mqtt_wrapper_publish("cmd/response", json_str);
  free(json_str);
  return err;
}

esp_err_t command_handler_process(const char *json_cmd, int len) {
  if (json_cmd == NULL || len <= 0) {
    send_response("unknown", "error", "Empty command");
    return ESP_ERR_INVALID_ARG;
  }

  cJSON *root = cJSON_ParseWithLength(json_cmd, len);
  if (root == NULL) {
    send_response("unknown", "error", "Invalid JSON");
    return ESP_FAIL;
  }

  cJSON *cmd_item = cJSON_GetObjectItem(root, "cmd");
  if (cmd_item == NULL || !cJSON_IsString(cmd_item)) {
    send_response("unknown", "error", "Missing cmd field");
    cJSON_Delete(root);
    return ESP_FAIL;
  }

  const char *cmd = cmd_item->valuestring;
  ESP_LOGI(TAG, "Command: %s", cmd);

  if (strcmp(cmd, "relay_set") == 0) {
    relay_channel_t ch;
    cJSON *st_item = cJSON_GetObjectItem(root, "state");
    if (json_get_valid_channel(root, &ch) && st_item && cJSON_IsBool(st_item)) {
      bool state = cJSON_IsTrue(st_item);
      relay_set(ch, state);
      send_response(cmd, "ok", "Relay updated");
    } else {
      send_response(cmd, "error", "Invalid parameters");
    }
  } else if (strcmp(cmd, "relay_timed") == 0) {
    relay_channel_t ch;
    cJSON *dur_item = cJSON_GetObjectItem(root, "duration");
    if (json_get_valid_channel(root, &ch) && dur_item &&
        cJSON_IsNumber(dur_item) && dur_item->valuedouble >= 0) {
      uint32_t dur = (uint32_t)dur_item->valuedouble;
      relay_set_timed(ch, dur);
      send_response(cmd, "ok", "Timed relay started");
    } else {
      send_response(cmd, "error", "Invalid parameters");
    }
  } else if (strcmp(cmd, "get_sensors") == 0) {
    sensor_data_t data;
    if (sensor_hub_read(&data) == ESP_OK) {
      mqtt_wrapper_publish_sensor_data(&data);
      send_response(cmd, "ok", "Sensors published");
    } else {
      send_response(cmd, "error", "Sensor read failed");
    }
  } else if (strcmp(cmd, "get_inputs") == 0) {
    for (int i = 0; i < 4; i++) {
      mqtt_wrapper_publish_input_state(i, digital_input_get(i));
    }
    send_response(cmd, "ok", "Inputs published");
  } else if (strcmp(cmd, "get_status") == 0) {
    system_status_t status;
    if (system_monitor_get_status(&status) == ESP_OK) {
      char *json = system_monitor_status_to_json(&status);
      if (json) {
        mqtt_wrapper_publish_status(json);
        free(json);
        send_response(cmd, "ok", "Status published");
      } else {
        send_response(cmd, "error", "JSON build failed");
      }
    } else {
      send_response(cmd, "error", "Status unavailable");
    }
  } else if (strcmp(cmd, "get_time") == 0) {
    struct tm now;
    char timestr[64];
    if (rtc_manager_get_time(&now) == ESP_OK) {
      snprintf(timestr, sizeof(timestr),
               "%04d-%02d-%02d %02d:%02d:%02d",
               now.tm_year + 1900, now.tm_mon + 1, now.tm_mday,
               now.tm_hour, now.tm_min, now.tm_sec);
      send_response(cmd, "ok", timestr);
    } else {
      send_response(cmd, "error", "RTC read failed");
    }
  } else if (strcmp(cmd, "set_time") == 0) {
    cJSON *ts = cJSON_GetObjectItem(root, "timestamp");
    if (ts && cJSON_IsNumber(ts) && ts->valuedouble > 0) {
      time_t t = (time_t)ts->valuedouble;
      struct tm now = {0};
      if (gmtime_r(&t, &now) != NULL && rtc_manager_set_time(&now) == ESP_OK) {
        send_response(cmd, "ok", "RTC time updated");
      } else {
        send_response(cmd, "error", "RTC set failed");
      }
    } else {
      send_response(cmd, "error", "Missing timestamp");
    }
  } else if (strcmp(cmd, "set_config") == 0) {
    cJSON *key_item = cJSON_GetObjectItem(root, "key");
    cJSON *val_item = cJSON_GetObjectItem(root, "value");
    if (key_item && val_item && cJSON_IsString(key_item) &&
        cJSON_IsString(val_item)) {
      bool broker_changed = strcmp(key_item->valuestring, "mqtt_broker") == 0;
      esp_err_t err = config_store_set_field(key_item->valuestring,
                                              val_item->valuestring);
      if (err == ESP_OK) {
        if (broker_changed) {
          send_response(cmd, "ok", "Config updated; reconnecting MQTT");
          mqtt_wrapper_reconfigure();
        } else {
          send_response(cmd, "ok", "Config updated and saved");
        }
      } else {
        send_response(cmd, "error", "Config update failed");
      }
    } else {
      send_response(cmd, "error", "Invalid parameters");
    }
  } else if (strcmp(cmd, "all_off") == 0) {
    relay_all_off();
    send_response(cmd, "ok", "All relays turned off");
  } else if (strcmp(cmd, "reboot") == 0) {
    ESP_LOGW(TAG, "Rebooting...");
    send_response(cmd, "ok", "Rebooting");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
  } else {
    ESP_LOGE(TAG, "Unsupported command: %s", cmd);
    send_response(cmd, "error", "Unsupported command");
  }

  cJSON_Delete(root);
  return ESP_OK;
}

static void on_mqtt_command(void *arg, esp_event_base_t base, int32_t event_id,
                            void *data) {
  if (event_id == MQTT_APP_EVENT_COMMAND_RECEIVED) {
    mqtt_command_event_t *evt = (mqtt_command_event_t *)data;
    if (evt != NULL && evt->data != NULL) {
      command_handler_process(evt->data, evt->data_len);
      free(evt->data);
    }
  }
}

esp_err_t command_handler_init(void) {
  esp_err_t err = esp_event_handler_register(
      MQTT_APP_EVENTS, MQTT_APP_EVENT_COMMAND_RECEIVED, &on_mqtt_command, NULL);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register handler: %s", esp_err_to_name(err));
    return err;
  }
  ESP_LOGI(TAG, "Command Handler initialized");
  return ESP_OK;
}
