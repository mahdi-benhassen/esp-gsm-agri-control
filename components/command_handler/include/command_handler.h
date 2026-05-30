#pragma once

#include "esp_err.h"

esp_err_t command_handler_init(void);
esp_err_t command_handler_process(const char *json_cmd, int len);
