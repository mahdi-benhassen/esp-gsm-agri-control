#include "web_server.h"
#include "web_assets.h"
#include "analog_input.h"
#include "config_store.h"
#include "digital_input.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "mbedtls/base64.h"
#include "modem_manager.h"
#include "mqtt_client_wrapper.h"
#include "nvs_flash.h"
#include "relay_control.h"
#include "rtc_manager.h"
#include "sensor_hub.h"
#include "system_monitor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static const char *TAG = "WEB_SERVER";

static httpd_handle_t s_server = NULL;
static bool s_wifi_connected = false;
static char s_wifi_ip[16] = "0.0.0.0";
static SemaphoreHandle_t s_wifi_mutex = NULL;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_count = 0;

// Rate limiting
static uint32_t s_last_req_time_ms = 0;
static SemaphoreHandle_t s_rate_mutex = NULL;

static bool rate_limit_check(void) {
  if (s_rate_mutex == NULL) return true;
  xSemaphoreTake(s_rate_mutex, portMAX_DELAY);
  uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);
  bool ok = (now - s_last_req_time_ms) >= CONFIG_WEB_SERVER_RATE_LIMIT_MS;
  if (ok) s_last_req_time_ms = now;
  xSemaphoreGive(s_rate_mutex);
  return ok;
}

static bool constant_time_equal(const char *a, const char *b, size_t len) {
  uint8_t diff = 0;
  for (size_t i = 0; i < len; i++) {
    diff |= (uint8_t)a[i] ^ (uint8_t)b[i];
  }
  return diff == 0;
}

static bool check_basic_auth(httpd_req_t *req) {
  char hdr[160] = {0};
  if (httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr)) != ESP_OK) {
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"KC868-A2v3\"");
    httpd_resp_sendstr(req, "{\"error\":\"Unauthorized\"}");
    return false;
  }
  const char *prefix = "Basic ";
  if (strncasecmp(hdr, prefix, strlen(prefix)) != 0) {
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_sendstr(req, "{\"error\":\"Invalid auth scheme\"}");
    return false;
  }
  char expected[96];
  int expected_len = snprintf(expected, sizeof(expected), "%s:%s",
                              CONFIG_WEB_SERVER_AUTH_USERNAME,
                              CONFIG_WEB_SERVER_AUTH_PASSWORD);
  if (expected_len < 0 || (size_t)expected_len >= sizeof(expected)) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_sendstr(req, "{\"error\":\"Auth misconfiguration\"}");
    return false;
  }

  unsigned char decoded[96] = {0};
  size_t decoded_len = 0;
  if (mbedtls_base64_decode(decoded, sizeof(decoded), &decoded_len,
                            (const unsigned char *)hdr + strlen(prefix),
                            strlen(hdr) - strlen(prefix)) != 0 ||
      decoded_len != (size_t)expected_len) {
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_sendstr(req, "{\"error\":\"Invalid credentials\"}");
    return false;
  }
  if (!constant_time_equal((const char *)decoded, expected, (size_t)expected_len)) {
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_sendstr(req, "{\"error\":\"Invalid credentials\"}");
    return false;
  }
  return true;
}

static void wifi_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *data) {
  if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    xSemaphoreTake(s_wifi_mutex, portMAX_DELAY);
    s_wifi_connected = false;
    snprintf(s_wifi_ip, sizeof(s_wifi_ip), "0.0.0.0");
    xSemaphoreGive(s_wifi_mutex);
    if (s_retry_count < 5) {
      esp_wifi_connect();
      s_retry_count++;
    }
  } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
    xSemaphoreTake(s_wifi_mutex, portMAX_DELAY);
    s_wifi_connected = true;
    snprintf(s_wifi_ip, sizeof(s_wifi_ip), IPSTR, IP2STR(&event->ip_info.ip));
    xSemaphoreGive(s_wifi_mutex);
    s_retry_count = 0;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    ESP_LOGI(TAG, "WiFi connected, IP: %s", s_wifi_ip);
  }
}

static esp_err_t wifi_init_apsta(void) {
  s_wifi_event_group = xEventGroupCreate();
  esp_netif_create_default_wifi_ap();
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);

  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);

  wifi_config_t ap_config = {
      .ap = {.ssid = CONFIG_WEB_SERVER_WIFI_AP_SSID,
             .password = CONFIG_WEB_SERVER_WIFI_AP_PASSWORD,
             .ssid_len = strlen(CONFIG_WEB_SERVER_WIFI_AP_SSID),
             .authmode = WIFI_AUTH_WPA_WPA2_PSK,
             .max_connection = 2},
  };

  esp_wifi_set_mode(WIFI_MODE_APSTA);
  esp_wifi_set_config(WIFI_IF_AP, &ap_config);
  esp_wifi_start();

  ESP_LOGI(TAG, "WiFi AP started: %s", CONFIG_WEB_SERVER_WIFI_AP_SSID);
  return ESP_OK;
}

esp_err_t web_server_wifi_connect(const char *ssid, const char *password) {
  if (ssid == NULL || password == NULL) return ESP_ERR_INVALID_ARG;

  wifi_config_t sta_cfg = {.sta = {.threshold.authmode = WIFI_AUTH_WPA2_PSK}};
  strncpy((char *)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid) - 1);
  strncpy((char *)sta_cfg.sta.password, password, sizeof(sta_cfg.sta.password) - 1);

  esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
  s_retry_count = 0;
  esp_wifi_connect();

  nvs_handle_t nvs;
  if (nvs_open("wifi_cfg", NVS_READWRITE, &nvs) == ESP_OK) {
    nvs_set_str(nvs, "ssid", ssid);
    nvs_set_str(nvs, "pass", password);
    nvs_commit(nvs);
    nvs_close(nvs);
  }

  ESP_LOGI(TAG, "Connecting to WiFi: %s", ssid);
  return ESP_OK;
}

esp_err_t web_server_wifi_disconnect(void) {
  esp_wifi_disconnect();
  return ESP_OK;
}

bool web_server_wifi_is_connected(void) {
  bool c = false;
  if (s_wifi_mutex) {
    xSemaphoreTake(s_wifi_mutex, portMAX_DELAY);
    c = s_wifi_connected;
    xSemaphoreGive(s_wifi_mutex);
  }
  return c;
}

esp_err_t web_server_wifi_get_ip(char *buf, size_t len) {
  if (buf == NULL || len == 0) return ESP_ERR_INVALID_ARG;
  xSemaphoreTake(s_wifi_mutex, portMAX_DELAY);
  snprintf(buf, len, "%s", s_wifi_ip);
  xSemaphoreGive(s_wifi_mutex);
  return ESP_OK;
}

static esp_err_t send_json(httpd_req_t *req, const char *json) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json, strlen(json));
  return ESP_OK;
}

static esp_err_t get_root(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, INDEX_HTML, strlen(INDEX_HTML));
  return ESP_OK;
}

static esp_err_t get_status(httpd_req_t *req) {
  if (!rate_limit_check()) {
    httpd_resp_set_status(req, "429 Too Many Requests");
    return httpd_resp_sendstr(req, "{\"error\":\"Rate limited\"}");
  }
  if (!check_basic_auth(req)) return ESP_OK;
  system_status_t status;
  system_monitor_get_status(&status);

  char buf[512];
  snprintf(buf, sizeof(buf),
           "{\"uptime\":%lu,\"free_heap\":%lu,\"min_free_heap\":%lu,"
           "\"modem_rssi\":%d,\"mqtt_connected\":%s,\"modem_connected\":%s,"
           "\"eth_connected\":%s,\"sd_mounted\":%s,\"rtc_temp\":%.1f,"
           "\"wifi_connected\":%s}",
           (unsigned long)status.uptime_sec,
           (unsigned long)status.free_heap,
           (unsigned long)status.min_free_heap,
           status.modem_rssi,
           status.mqtt_connected ? "true" : "false",
           status.modem_connected ? "true" : "false",
           status.eth_connected ? "true" : "false",
           status.sd_mounted ? "true" : "false",
           (double)status.rtc_temp,
           web_server_wifi_is_connected() ? "true" : "false");
  return send_json(req, buf);
}

static esp_err_t get_relays(httpd_req_t *req) {
  if (!rate_limit_check()) {
    httpd_resp_set_status(req, "429 Too Many Requests");
    return httpd_resp_sendstr(req, "{\"error\":\"Rate limited\"}");
  }
  if (!check_basic_auth(req)) return ESP_OK;
  char buf[64];
  snprintf(buf, sizeof(buf), "[%s,%s]",
           relay_get(RELAY_CH_1) ? "true" : "false",
           relay_get(RELAY_CH_2) ? "true" : "false");
  return send_json(req, buf);
}

static esp_err_t post_relays(httpd_req_t *req) {
  if (!rate_limit_check()) {
    httpd_resp_set_status(req, "429 Too Many Requests");
    return httpd_resp_sendstr(req, "{\"error\":\"Rate limited\"}");
  }
  if (!check_basic_auth(req)) return ESP_OK;
  char content[256] = {0};
  int recv = httpd_req_recv(req, content, sizeof(content) - 1);
  if (recv <= 0) { send_json(req, "{\"error\":\"empty body\"}"); return ESP_OK; }

  if (strstr(content, "\"all_off\"")) {
    relay_all_off();
    return send_json(req, "{\"ok\":true,\"message\":\"All relays OFF\"}");
  }

  int ch = -1, state = -1, duration = -1;
  char *p = strstr(content, "\"channel\"");
  if (p) {
      char *colon = strchr(p, ':');
      if (colon) ch = atoi(colon + 1);
  }
  p = strstr(content, "\"state\"");
  if (p) {
      char *colon = strchr(p, ':');
      if (colon) {
          colon++;
          while (*colon == ' ' || *colon == '\t') colon++;
          if (strncmp(colon, "true", 4) == 0) state = 1;
          else if (strncmp(colon, "false", 5) == 0) state = 0;
      }
  }
  p = strstr(content, "\"duration\"");
  if (p) {
      char *colon = strchr(p, ':');
      if (colon) duration = atoi(colon + 1);
  }

  if (ch >= 0 && ch < RELAY_CH_MAX) {
    if (duration > 0) {
      relay_set_timed((relay_channel_t)ch, (uint32_t)duration);
      char rsp[64];
      snprintf(rsp, sizeof(rsp), "{\"ok\":true,\"message\":\"Relay %d ON for %ds\"}", ch + 1, duration);
      return send_json(req, rsp);
    } else if (state >= 0) {
      relay_set((relay_channel_t)ch, (bool)state);
      char rsp[64];
      snprintf(rsp, sizeof(rsp), "{\"ok\":true,\"message\":\"Relay %d %s\"}", ch + 1, state ? "ON" : "OFF");
      return send_json(req, rsp);
    }
  }
  return send_json(req, "{\"ok\":false,\"message\":\"Invalid params\"}");
}

static esp_err_t get_inputs(httpd_req_t *req) {
  if (!rate_limit_check()) {
    httpd_resp_set_status(req, "429 Too Many Requests");
    return httpd_resp_sendstr(req, "{\"error\":\"Rate limited\"}");
  }
  if (!check_basic_auth(req)) return ESP_OK;
  char buf[128];
  snprintf(buf, sizeof(buf), "[%s,%s,%s,%s]",
           digital_input_get(0) ? "true" : "false",
           digital_input_get(1) ? "true" : "false",
           digital_input_get(2) ? "true" : "false",
           digital_input_get(3) ? "true" : "false");
  return send_json(req, buf);
}

static esp_err_t get_sensors(httpd_req_t *req) {
  if (!rate_limit_check()) {
    httpd_resp_set_status(req, "429 Too Many Requests");
    return httpd_resp_sendstr(req, "{\"error\":\"Rate limited\"}");
  }
  if (!check_basic_auth(req)) return ESP_OK;
  sensor_data_t data;
  sensor_hub_read(&data);
  char buf[128];
  snprintf(buf, sizeof(buf),
           "{\"temp_1\":%.1f,\"temp_2\":%.1f,\"valid\":%s,\"timestamp\":%lld}",
           (double)data.temperature_c,
           (double)data.soil_moisture_pct,
           data.sensors_valid ? "true" : "false",
           (long long)data.timestamp_ms);
  return send_json(req, buf);
}

static esp_err_t get_analog(httpd_req_t *req) {
  if (!rate_limit_check()) {
    httpd_resp_set_status(req, "429 Too Many Requests");
    return httpd_resp_sendstr(req, "{\"error\":\"Rate limited\"}");
  }
  if (!check_basic_auth(req)) return ESP_OK;
  int mv1 = analog_input_read_mv(0);
  int mv2 = analog_input_read_mv(1);
  char buf[64];
  snprintf(buf, sizeof(buf), "[%d,%d]", mv1, mv2);
  return send_json(req, buf);
}

static esp_err_t get_wifi(httpd_req_t *req) {
  if (!rate_limit_check()) {
    httpd_resp_set_status(req, "429 Too Many Requests");
    return httpd_resp_sendstr(req, "{\"error\":\"Rate limited\"}");
  }
  if (!check_basic_auth(req)) return ESP_OK;
  wifi_ap_record_t ap_info;
  char ssid[33] = "Not connected";
  if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
    memcpy(ssid, ap_info.ssid, sizeof(ap_info.ssid));
    ssid[32] = '\0';
  }

  char buf[256];
  xSemaphoreTake(s_wifi_mutex, portMAX_DELAY);
  snprintf(buf, sizeof(buf),
           "{\"mode\":\"%s\",\"ip\":\"%s\",\"ssid\":\"%s\",\"connected\":%s}",
           s_wifi_connected ? "STA" : "AP",
           s_wifi_ip, ssid,
           s_wifi_connected ? "true" : "false");
  xSemaphoreGive(s_wifi_mutex);
  return send_json(req, buf);
}

static esp_err_t post_wifi(httpd_req_t *req) {
  if (!rate_limit_check()) {
    httpd_resp_set_status(req, "429 Too Many Requests");
    return httpd_resp_sendstr(req, "{\"error\":\"Rate limited\"}");
  }
  if (!check_basic_auth(req)) return ESP_OK;
  char content[256] = {0};
  int recv = httpd_req_recv(req, content, sizeof(content) - 1);
  if (recv <= 0) { send_json(req, "{\"ok\":false,\"message\":\"empty body\"}"); return ESP_OK; }

  if (strstr(content, "\"disconnect\"")) {
    web_server_wifi_disconnect();
    return send_json(req, "{\"ok\":true,\"message\":\"Disconnected\"}");
  }

  char ssid[33] = {0}, pass[65] = {0};
  char *p = strstr(content, "\"ssid\"");
  if (p) {
      char *colon = strchr(p, ':');
      if (colon) {
          char *start = strchr(colon, '"');
          if (start) {
              start++;
              char *end = strchr(start, '"');
              if (end) {
                  size_t len = end - start;
                  if (len < sizeof(ssid)) memcpy(ssid, start, len);
              }
          }
      }
  }
  p = strstr(content, "\"password\"");
  if (p) {
      char *colon = strchr(p, ':');
      if (colon) {
          char *start = strchr(colon, '"');
          if (start) {
              start++;
              char *end = strchr(start, '"');
              if (end) {
                  size_t len = end - start;
                  if (len < sizeof(pass)) memcpy(pass, start, len);
              }
          }
      }
  }

  if (ssid[0]) {
    web_server_wifi_connect(ssid, pass);
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"ok\":true,\"message\":\"Connecting to %s...\"}", ssid);
    return send_json(req, buf);
  }
  return send_json(req, "{\"ok\":false,\"message\":\"Missing SSID\"}");
}

static esp_err_t get_config(httpd_req_t *req) {
  if (!rate_limit_check()) {
    httpd_resp_set_status(req, "429 Too Many Requests");
    return httpd_resp_sendstr(req, "{\"error\":\"Rate limited\"}");
  }
  if (!check_basic_auth(req)) return ESP_OK;
  app_config_t cfg;
  config_store_get_snapshot(&cfg);
  char buf[512];
  snprintf(buf, sizeof(buf),
           "{\"device_name\":\"%s\",\"mqtt_broker\":\"%s\","
           "\"sensor_read_interval_sec\":%lu,\"mqtt_publish_interval_sec\":%lu,"
           "\"input_debounce_ms\":%lu,\"input_1_inverted\":%s,"
           "\"input_2_inverted\":%s,\"input_3_inverted\":%s,"
           "\"input_4_inverted\":%s,\"relay_interlock_enabled\":%s,"
           "\"lcd_enabled\":%s,\"sd_log_enabled\":%s,"
           "\"safe_state_timeout_sec\":%lu}",
           cfg.device_name, cfg.mqtt_broker_uri,
           (unsigned long)cfg.sensor_read_interval_sec,
           (unsigned long)cfg.mqtt_publish_interval_sec,
           (unsigned long)cfg.input_debounce_ms,
           cfg.input_1_inverted ? "true" : "false",
           cfg.input_2_inverted ? "true" : "false",
           cfg.input_3_inverted ? "true" : "false",
            cfg.input_4_inverted ? "true" : "false",
            cfg.relay_interlock_enabled ? "true" : "false",
            cfg.lcd_enabled ? "true" : "false",
            cfg.sd_log_enabled ? "true" : "false",
            (unsigned long)cfg.safe_state_timeout_sec);
  return send_json(req, buf);
}

static esp_err_t post_config(httpd_req_t *req) {
  if (!rate_limit_check()) {
    httpd_resp_set_status(req, "429 Too Many Requests");
    return httpd_resp_sendstr(req, "{\"error\":\"Rate limited\"}");
  }
  if (!check_basic_auth(req)) return ESP_OK;
  char content[512] = {0};
  int recv = httpd_req_recv(req, content, sizeof(content) - 1);
  if (recv <= 0) { send_json(req, "{\"ok\":false,\"message\":\"empty body\"}"); return ESP_OK; }

  const char *keys[] = {"device_name", "mqtt_broker", "sensor_read_interval_sec",
                         "mqtt_publish_interval_sec", "input_debounce_ms",
                         "safe_state_timeout_sec",
                         "input_1_inverted", "input_2_inverted", "input_3_inverted",
                         "input_4_inverted", "relay_interlock_enabled", "lcd_enabled",
                         "sd_log_enabled", NULL};
  const char *nvs_map[] = {"device_name", "mqtt_broker", "sensor_interval",
                            "mqtt_interval", "debounce_ms", "safe_state_timeout",
                            "input_1_inverted", "input_2_inverted", "input_3_inverted",
                            "input_4_inverted", "interlock", "lcd_enabled",
                            "sd_log_enabled", NULL};

  for (int i = 0; keys[i] != NULL; i++) {
    char *p = strstr(content, keys[i]);
    if (p) {
      p = strchr(p, ':');
      if (p) {
        p++;
        while (*p == ' ' || *p == '"') p++;
        char val[64] = {0};
        int j = 0;
        while (*p && *p != '"' && *p != ',' && *p != '}' && j < 63) val[j++] = *p++;
        if (val[0]) {
           if (strcmp(val, "true") == 0) strncpy(val, "1", sizeof(val) - 1);
           if (strcmp(val, "false") == 0) strncpy(val, "0", sizeof(val) - 1);
          config_store_set_field(nvs_map[i], val);
        }
      }
    }
  }

  return send_json(req, "{\"ok\":true,\"message\":\"Config saved to flash\"}");
}

static esp_err_t post_reboot(httpd_req_t *req) {
  if (!rate_limit_check()) {
    httpd_resp_set_status(req, "429 Too Many Requests");
    return httpd_resp_sendstr(req, "{\"error\":\"Rate limited\"}");
  }
  if (!check_basic_auth(req)) return ESP_OK;
  send_json(req, "{\"ok\":true,\"message\":\"Rebooting...\"}");
  vTaskDelay(pdMS_TO_TICKS(500));
  esp_restart();
  return ESP_OK;
}

static const httpd_uri_t s_uris[] = {
    {.uri = "/",              .method = HTTP_GET,  .handler = get_root},
    {.uri = "/api/status",    .method = HTTP_GET,  .handler = get_status},
    {.uri = "/api/relays",    .method = HTTP_GET,  .handler = get_relays},
    {.uri = "/api/relays",    .method = HTTP_POST, .handler = post_relays},
    {.uri = "/api/inputs",    .method = HTTP_GET,  .handler = get_inputs},
    {.uri = "/api/sensors",   .method = HTTP_GET,  .handler = get_sensors},
    {.uri = "/api/analog",    .method = HTTP_GET,  .handler = get_analog},
    {.uri = "/api/wifi",      .method = HTTP_GET,  .handler = get_wifi},
    {.uri = "/api/wifi",      .method = HTTP_POST, .handler = post_wifi},
    {.uri = "/api/config",    .method = HTTP_GET,  .handler = get_config},
    {.uri = "/api/config",    .method = HTTP_POST, .handler = post_config},
    {.uri = "/api/reboot",    .method = HTTP_POST, .handler = post_reboot},
};

esp_err_t web_server_init(void) {
  s_wifi_mutex = xSemaphoreCreateMutex();
  if (s_wifi_mutex == NULL) return ESP_ERR_NO_MEM;
  s_rate_mutex = xSemaphoreCreateMutex();
  if (s_rate_mutex == NULL) return ESP_ERR_NO_MEM;

  wifi_init_apsta();

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = CONFIG_WEB_SERVER_PORT;
  config.max_uri_handlers = 16;

  esp_err_t err = httpd_start(&s_server, &config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "HTTP server start failed: %s", esp_err_to_name(err));
    return err;
  }

  for (int i = 0; i < sizeof(s_uris) / sizeof(s_uris[0]); i++) {
    httpd_register_uri_handler(s_server, &s_uris[i]);
  }

  ESP_LOGI(TAG, "Web server started on port %d", CONFIG_WEB_SERVER_PORT);
  ESP_LOGI(TAG, "AP: %s (password: %s)", CONFIG_WEB_SERVER_WIFI_AP_SSID,
           CONFIG_WEB_SERVER_WIFI_AP_PASSWORD);
  return ESP_OK;
}
