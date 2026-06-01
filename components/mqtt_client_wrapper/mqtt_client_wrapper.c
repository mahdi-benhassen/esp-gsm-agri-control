#include "mqtt_client_wrapper.h"
#include "cJSON.h"
#include "config_store.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "MQTT_WRAPPER";

ESP_EVENT_DEFINE_BASE(MQTT_APP_EVENTS);

static esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;
static char s_device_id[32] = {0};
static SemaphoreHandle_t s_mutex = NULL;

static void build_topic(const char *suffix, char *buf, size_t len) {
  snprintf(buf, len, "%s/%s/%s", CONFIG_MQTT_DEVICE_ID_PREFIX, s_device_id,
           suffix);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
  esp_mqtt_event_handle_t event = event_data;
  esp_mqtt_client_handle_t client = event->client;
  char topic_buf[128];

  switch ((esp_mqtt_event_id_t)event_id) {
  case MQTT_EVENT_CONNECTED:
    ESP_LOGI(TAG, "MQTT Connected");
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_connected = true;
    if (s_mutex) xSemaphoreGive(s_mutex);

    build_topic("cmd", topic_buf, sizeof(topic_buf));
    esp_mqtt_client_subscribe(client, topic_buf, 1);
    ESP_LOGI(TAG, "Subscribed to: %s", topic_buf);

    build_topic("status", topic_buf, sizeof(topic_buf));
    esp_mqtt_client_publish(client, topic_buf,
                            "{\"status\":\"online\"}", 0, 1, 1);

    esp_event_post(MQTT_APP_EVENTS, MQTT_APP_EVENT_CONNECTED, NULL, 0,
                   pdMS_TO_TICKS(100));
    break;

  case MQTT_EVENT_DISCONNECTED:
    ESP_LOGW(TAG, "MQTT Disconnected");
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_connected = false;
    if (s_mutex) xSemaphoreGive(s_mutex);
    esp_event_post(MQTT_APP_EVENTS, MQTT_APP_EVENT_DISCONNECTED, NULL, 0,
                   pdMS_TO_TICKS(100));
    break;

  case MQTT_EVENT_DATA: {
    mqtt_command_event_t cmd_evt = {.data = malloc(event->data_len + 1),
                                    .data_len = event->data_len};
    if (cmd_evt.data == NULL) break;
    memcpy(cmd_evt.data, event->data, event->data_len);
    cmd_evt.data[event->data_len] = '\0';
    esp_err_t err = esp_event_post(MQTT_APP_EVENTS,
                                   MQTT_APP_EVENT_COMMAND_RECEIVED,
                                   &cmd_evt, sizeof(cmd_evt),
                                   pdMS_TO_TICKS(100));
    if (err != ESP_OK) free(cmd_evt.data);
    break;
  }

  case MQTT_EVENT_ERROR:
    ESP_LOGE(TAG, "MQTT Error event");
    break;

  default:
    break;
  }
}

static esp_err_t mqtt_wrapper_start_locked(void) {
  app_config_t cfg;
  const char *broker_uri = CONFIG_MQTT_BROKER_URI;
  if (config_store_get_snapshot(&cfg) == ESP_OK &&
      strlen(cfg.mqtt_broker_uri) > 0) {
    broker_uri = cfg.mqtt_broker_uri;
  }

  ESP_LOGI(TAG, "Connecting to broker: %s", broker_uri);

  char lwt_topic[128];
  build_topic("status", lwt_topic, sizeof(lwt_topic));

  esp_mqtt_client_config_t mqtt_cfg = {
      .broker.address.uri = broker_uri,
      .session.last_will = {.topic = lwt_topic,
                            .msg = "{\"status\":\"offline\"}",
                            .qos = 1,
                            .retain = 1}};

  s_client = esp_mqtt_client_init(&mqtt_cfg);
  if (s_client == NULL) {
    ESP_LOGE(TAG, "Failed to init MQTT client");
    return ESP_FAIL;
  }

  esp_err_t err = esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                                  mqtt_event_handler, NULL);
  if (err != ESP_OK) {
    esp_mqtt_client_destroy(s_client);
    s_client = NULL;
    return err;
  }

  err = esp_mqtt_client_start(s_client);
  if (err != ESP_OK) {
    esp_mqtt_client_destroy(s_client);
    s_client = NULL;
    return err;
  }
  return ESP_OK;
}

esp_err_t mqtt_wrapper_init(void) {
  if (s_mutex == NULL) {
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) return ESP_ERR_NO_MEM;
  }

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(s_device_id, sizeof(s_device_id),
           "%02X%02X%02X%02X%02X%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  ESP_LOGI(TAG, "Device ID: %s", s_device_id);

  return mqtt_wrapper_start_locked();
}

esp_err_t mqtt_wrapper_reconfigure(void) {
  if (s_mutex == NULL) return ESP_ERR_INVALID_STATE;

  ESP_LOGW(TAG, "Reconfiguring MQTT client");
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  esp_mqtt_client_handle_t old_client = s_client;
  s_client = NULL;
  s_connected = false;
  xSemaphoreGive(s_mutex);

  if (old_client != NULL) {
    esp_mqtt_client_stop(old_client);
    esp_mqtt_client_destroy(old_client);
  }
  return mqtt_wrapper_start_locked();
}

esp_err_t mqtt_wrapper_publish_sensor_data(const sensor_data_t *data) {
  if (data == NULL) return ESP_ERR_INVALID_ARG;
  if (!mqtt_wrapper_is_connected()) return ESP_ERR_INVALID_STATE;

  cJSON *root = cJSON_CreateObject();
  if (root == NULL) return ESP_ERR_NO_MEM;

  cJSON_AddNumberToObject(root, "temp_1", data->temperature_c);
  cJSON_AddNumberToObject(root, "humidity", data->humidity_pct);
  cJSON_AddNumberToObject(root, "temp_2", data->soil_moisture_pct);
  cJSON_AddBoolToObject(root, "sensors_valid", data->sensors_valid);
  cJSON_AddNumberToObject(root, "timestamp", (double)data->timestamp_ms);

  char *json_str = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json_str == NULL) return ESP_ERR_NO_MEM;

  char topic[128];
  build_topic("sensors", topic, sizeof(topic));

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  int msg_id = (s_client != NULL && s_connected)
                   ? esp_mqtt_client_publish(s_client, topic, json_str, 0, 1, 0)
                   : -1;
  xSemaphoreGive(s_mutex);
  free(json_str);

  return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t mqtt_wrapper_publish_relay_state(int channel, bool state) {
  if (!mqtt_wrapper_is_connected()) return ESP_ERR_INVALID_STATE;

  cJSON *root = cJSON_CreateObject();
  if (root == NULL) return ESP_ERR_NO_MEM;
  cJSON_AddNumberToObject(root, "channel", channel);
  cJSON_AddBoolToObject(root, "state", state);

  char *json_str = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json_str == NULL) return ESP_ERR_NO_MEM;

  char topic[128];
  build_topic("relays", topic, sizeof(topic));

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  int msg_id = (s_client != NULL && s_connected)
                   ? esp_mqtt_client_publish(s_client, topic, json_str, 0, 1, 0)
                   : -1;
  xSemaphoreGive(s_mutex);
  free(json_str);

  return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t mqtt_wrapper_publish_input_state(int channel, bool state) {
  if (!mqtt_wrapper_is_connected()) return ESP_ERR_INVALID_STATE;

  cJSON *root = cJSON_CreateObject();
  if (root == NULL) return ESP_ERR_NO_MEM;
  cJSON_AddNumberToObject(root, "channel", channel);
  cJSON_AddBoolToObject(root, "state", state);

  char *json_str = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json_str == NULL) return ESP_ERR_NO_MEM;

  char topic[128];
  build_topic("inputs", topic, sizeof(topic));

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  int msg_id = (s_client != NULL && s_connected)
                   ? esp_mqtt_client_publish(s_client, topic, json_str, 0, 1, 0)
                   : -1;
  xSemaphoreGive(s_mutex);
  free(json_str);

  return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t mqtt_wrapper_publish_status(const char *status_json) {
  return mqtt_wrapper_publish("status", status_json);
}

esp_err_t mqtt_wrapper_publish(const char *topic_suffix, const char *payload) {
  if (topic_suffix == NULL || payload == NULL) return ESP_ERR_INVALID_ARG;
  if (!mqtt_wrapper_is_connected()) return ESP_ERR_INVALID_STATE;

  char topic[128];
  build_topic(topic_suffix, topic, sizeof(topic));

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (s_client == NULL || !s_connected) {
    xSemaphoreGive(s_mutex);
    return ESP_ERR_INVALID_STATE;
  }
  int msg_id = esp_mqtt_client_publish(s_client, topic, payload, 0, 1, 0);
  xSemaphoreGive(s_mutex);
  return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

bool mqtt_wrapper_is_connected(void) {
  bool connected = false;
  if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
  connected = s_connected && s_client != NULL;
  if (s_mutex) xSemaphoreGive(s_mutex);
  return connected;
}
