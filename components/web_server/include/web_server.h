#pragma once

#include "esp_err.h"
#include <stdbool.h>

esp_err_t web_server_init(void);
esp_err_t web_server_wifi_connect(const char *ssid, const char *password);
esp_err_t web_server_wifi_disconnect(void);
bool web_server_wifi_is_connected(void);
esp_err_t web_server_wifi_get_ip(char *buf, size_t len);
