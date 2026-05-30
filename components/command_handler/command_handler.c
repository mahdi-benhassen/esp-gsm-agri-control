#include "command_handler.h"
#include "cJSON.h"
#include "config_store.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client_wrapper.h"
#include "relay_control.h"
#include "sensor_hub.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "COMMAND_HANDLER";

static bool json_get_valid_channel(cJSON *root, relay_channel_t *channel) {
  cJSON *ch_item = cJSON_GetObjectItem(root, "channel");
  if (ch_item == NULL || !cJSON_IsNumber(ch_item)) {
    return false;
  }

  int ch = ch_item->valueint;
  if ((double)ch != ch_item->valuedouble || ch < 0 || ch >= RELAY_CH_MAX) {
    return false;
  }

  *channel = (relay_channel_t)ch;
  return true;
}

static esp_err_t send_response(const char *cmd, const char *result,
                               const char *msg) {
  cJSON *root = cJSON_CreateObject();
  if (root == NULL) {
    return ESP_ERR_NO_MEM;
  }

  if (cJSON_AddStringToObject(root, "cmd", cmd ? cmd : "unknown") == NULL ||
      cJSON_AddStringToObject(root, "result", result ? result : "error") ==
          NULL ||
      cJSON_AddStringToObject(root, "message", msg ? msg : "") == NULL) {
    cJSON_Delete(root);
    return ESP_ERR_NO_MEM;
  }

  char *json_str = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json_str == NULL) {
    return ESP_ERR_NO_MEM;
  }

  esp_err_t err = mqtt_wrapper_publish("cmd/response", json_str);
  free(json_str);
  return err;
}

esp_err_t command_handler_process(const char *json_cmd, int len) {
  if (json_cmd == NULL || len <= 0) {
    send_response("unknown", "error", "Empty command payload");
    return ESP_ERR_INVALID_ARG;
  }

  cJSON *root = cJSON_ParseWithLength(json_cmd, len);
  if (root == NULL) {
    ESP_LOGE(TAG, "Failed to parse command JSON");
    send_response("unknown", "error", "Invalid JSON format");
    return ESP_FAIL;
  }

  cJSON *cmd_item = cJSON_GetObjectItem(root, "cmd");
  if (cmd_item == NULL || !cJSON_IsString(cmd_item)) {
    ESP_LOGE(TAG, "Command field 'cmd' is missing or not a string");
    send_response("unknown", "error", "Missing 'cmd' field");
    cJSON_Delete(root);
    return ESP_FAIL;
  }

  const char *cmd = cmd_item->valuestring;
  ESP_LOGI(TAG, "Processing command: %s", cmd);

  if (strcmp(cmd, "relay_set") == 0) {
    relay_channel_t ch;
    cJSON *st_item = cJSON_GetObjectItem(root, "state");
    if (json_get_valid_channel(root, &ch) && st_item && cJSON_IsBool(st_item)) {
      bool state = cJSON_IsTrue(st_item);
      esp_err_t err = relay_set(ch, state);
      if (err == ESP_OK) {
        send_response(cmd, "ok", "Relay state updated");
      } else {
        send_response(cmd, "error", "Failed to set relay state");
      }
    } else {
      send_response(cmd, "error", "Invalid parameters for relay_set");
    }
  } else if (strcmp(cmd, "relay_timed") == 0) {
    relay_channel_t ch;
    cJSON *dur_item = cJSON_GetObjectItem(root, "duration");
    if (json_get_valid_channel(root, &ch) && dur_item &&
        cJSON_IsNumber(dur_item) && dur_item->valuedouble >= 0 &&
        dur_item->valuedouble <= UINT32_MAX &&
        (double)(uint32_t)dur_item->valuedouble == dur_item->valuedouble) {
      uint32_t dur = (uint32_t)dur_item->valuedouble;
      esp_err_t err = relay_set_timed(ch, dur);
      if (err == ESP_OK) {
        send_response(cmd, "ok", "Relay timed execution started");
      } else {
        send_response(cmd, "error", "Failed to set timed relay");
      }
    } else {
      send_response(cmd, "error", "Invalid parameters for relay_timed");
    }
  } else if (strcmp(cmd, "get_sensors") == 0) {
    sensor_data_t data;
    esp_err_t err = sensor_hub_read(&data);
    if (err == ESP_OK) {
      err = mqtt_wrapper_publish_sensor_data(&data);
      if (err == ESP_OK) {
        send_response(cmd, "ok", "Sensor data published");
      } else {
        send_response(cmd, "error", "Failed to publish sensors");
      }
    } else {
      send_response(cmd, "error", "Failed to read sensors");
    }
  } else if (strcmp(cmd, "set_config") == 0) {
    cJSON *key_item = cJSON_GetObjectItem(root, "key");
    cJSON *val_item = cJSON_GetObjectItem(root, "value");
    if (key_item && val_item && cJSON_IsString(key_item) &&
        cJSON_IsString(val_item)) {
      bool broker_changed = strcmp(key_item->valuestring, "mqtt_broker") == 0;
      esp_err_t err =
          config_store_set_field(key_item->valuestring, val_item->valuestring);
      if (err == ESP_OK) {
        if (broker_changed) {
          send_response(cmd, "ok", "Configuration updated; MQTT reconnecting");
          err = mqtt_wrapper_reconfigure();
          if (err != ESP_OK) {
            ESP_LOGE(TAG, "MQTT reconnect failed after broker update: %s",
                     esp_err_to_name(err));
          }
        } else {
          send_response(cmd, "ok", "Configuration updated and saved");
        }
      } else {
        send_response(cmd, "error", "Failed to update configuration");
      }
    } else {
      send_response(cmd, "error", "Invalid parameters for set_config");
    }
  } else if (strcmp(cmd, "all_off") == 0) {
    relay_all_off();
    send_response(cmd, "ok", "All relays turned off");
  } else if (strcmp(cmd, "reboot") == 0) {
    ESP_LOGW(TAG, "Rebooting system in 1 second...");
    send_response(cmd, "ok", "Rebooting...");
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
    ESP_LOGE(TAG, "Failed to register MQTT command handler event: %s",
             esp_err_to_name(err));
    return err;
  }
  ESP_LOGI(TAG, "Command Handler initialized.");
  return ESP_OK;
}
