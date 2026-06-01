#pragma once

#include "esp_err.h"
#include <stdbool.h>

esp_err_t ethernet_manager_init(void);
bool ethernet_manager_is_connected(void);
esp_err_t ethernet_manager_get_ip(char *buf, size_t len);
