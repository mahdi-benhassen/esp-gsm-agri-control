#pragma once

#include "esp_err.h"
#include <stdbool.h>

esp_err_t modem_manager_init(void);
esp_err_t modem_manager_deinit(void);
bool modem_manager_is_connected(void);
int modem_manager_get_rssi(void);
esp_err_t modem_manager_reconnect(void);
