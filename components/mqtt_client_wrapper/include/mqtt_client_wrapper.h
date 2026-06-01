#pragma once

#include "esp_err.h"
#include "esp_event.h"
#include "sensor_hub.h"
#include <stdbool.h>

ESP_EVENT_DECLARE_BASE(MQTT_APP_EVENTS);
enum {
  MQTT_APP_EVENT_CONNECTED,
  MQTT_APP_EVENT_DISCONNECTED,
  MQTT_APP_EVENT_COMMAND_RECEIVED,
};

typedef struct {
  char *data;
  int data_len;
} mqtt_command_event_t;

esp_err_t mqtt_wrapper_init(void);
esp_err_t mqtt_wrapper_reconfigure(void);
esp_err_t mqtt_wrapper_publish_sensor_data(const sensor_data_t *data);
esp_err_t mqtt_wrapper_publish_relay_state(int channel, bool state);
esp_err_t mqtt_wrapper_publish_input_state(int channel, bool state);
esp_err_t mqtt_wrapper_publish_status(const char *status_json);
esp_err_t mqtt_wrapper_publish(const char *topic_suffix, const char *payload);
bool mqtt_wrapper_is_connected(void);
