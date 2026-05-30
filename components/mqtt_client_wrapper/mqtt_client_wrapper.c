#include "mqtt_client_wrapper.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "cJSON.h"
#include "config_store.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "MQTT_WRAPPER";

ESP_EVENT_DEFINE_BASE(MQTT_APP_EVENTS);

static esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;
static char s_device_id[32] = {0};

static void build_topic(const char *suffix, char *buf, size_t len)
{
    snprintf(buf, len, "%s/%s/%s", CONFIG_MQTT_DEVICE_ID_PREFIX, s_device_id, suffix);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    char topic_buf[128];

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT Connected");
            s_connected = true;

            // Subscribe to cmd topic
            build_topic("cmd", topic_buf, sizeof(topic_buf));
            esp_mqtt_client_subscribe(client, topic_buf, 1);
            ESP_LOGI(TAG, "Subscribed to: %s", topic_buf);

            // Publish Birth message
            build_topic("status", topic_buf, sizeof(topic_buf));
            esp_mqtt_client_publish(client, topic_buf, "{\"status\":\"online\"}", 0, 1, 1);

            esp_event_post(MQTT_APP_EVENTS, MQTT_APP_EVENT_CONNECTED, NULL, 0, pdMS_TO_TICKS(100));
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT Disconnected");
            s_connected = false;
            esp_event_post(MQTT_APP_EVENTS, MQTT_APP_EVENT_DISCONNECTED, NULL, 0, pdMS_TO_TICKS(100));
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT Data topic: %.*s", event->topic_len, event->topic);
            
            // Dispatch command payload to command_handler
            mqtt_command_event_t cmd_evt = {
                .data = malloc(event->data_len + 1),
                .data_len = event->data_len
            };
            if (cmd_evt.data) {
                memcpy(cmd_evt.data, event->data, event->data_len);
                cmd_evt.data[event->data_len] = '\0';
                
                esp_event_post(MQTT_APP_EVENTS, MQTT_APP_EVENT_COMMAND_RECEIVED, &cmd_evt, sizeof(cmd_evt), pdMS_TO_TICKS(100));
            } else {
                ESP_LOGE(TAG, "Failed to allocate memory for command payload");
            }
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT Error event");
            break;

        default:
            break;
    }
}

esp_err_t mqtt_wrapper_init(void)
{
    // Generate device id from MAC address
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_device_id, sizeof(s_device_id), "%02X%02X%02X%02X%02X%02X", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    ESP_LOGI(TAG, "Device ID generated: %s", s_device_id);

    const app_config_t *cfg = config_store_get();
    const char *broker_uri = (cfg && strlen(cfg->mqtt_broker_uri) > 0) ? cfg->mqtt_broker_uri : CONFIG_MQTT_BROKER_URI;
    
    ESP_LOGI(TAG, "Connecting to broker: %s", broker_uri);

    char lwt_topic[128];
    build_topic("status", lwt_topic, sizeof(lwt_topic));

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = broker_uri,
        .session.last_will = {
            .topic = lwt_topic,
            .msg = "{\"status\":\"offline\"}",
            .qos = 1,
            .retain = 1,
        }
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "Failed to init MQTT client");
        return ESP_FAIL;
    }

    esp_err_t err = esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

esp_err_t mqtt_wrapper_publish_sensor_data(const sensor_data_t *data)
{
    if (!s_connected || s_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "soil_moisture", data->soil_moisture_pct);
    cJSON_AddNumberToObject(root, "temperature", data->temperature_c);
    cJSON_AddNumberToObject(root, "humidity", data->humidity_pct);
    cJSON_AddBoolToObject(root, "sensors_valid", data->sensors_valid);
    cJSON_AddNumberToObject(root, "timestamp", (double)data->timestamp_ms);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    char topic[128];
    build_topic("sensors", topic, sizeof(topic));

    int msg_id = esp_mqtt_client_publish(s_client, topic, json_str, 0, 1, 0);
    free(json_str);

    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish sensor data");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t mqtt_wrapper_publish_relay_state(int channel, bool state)
{
    if (!s_connected || s_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "channel", channel);
    cJSON_AddBoolToObject(root, "state", state);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    char topic[128];
    build_topic("relays", topic, sizeof(topic));

    int msg_id = esp_mqtt_client_publish(s_client, topic, json_str, 0, 1, 0);
    free(json_str);

    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish relay state");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t mqtt_wrapper_publish_status(const char *status_json)
{
    return mqtt_wrapper_publish("status", status_json);
}

esp_err_t mqtt_wrapper_publish(const char *topic_suffix, const char *payload)
{
    if (!s_connected || s_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    char topic[128];
    build_topic(topic_suffix, topic, sizeof(topic));

    int msg_id = esp_mqtt_client_publish(s_client, topic, payload, 0, 1, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish to topic: %s", topic);
        return ESP_FAIL;
    }
    return ESP_OK;
}

bool mqtt_wrapper_is_connected(void)
{
    return s_connected;
}
