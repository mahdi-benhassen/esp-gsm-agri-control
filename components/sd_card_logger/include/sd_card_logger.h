#pragma once

#include "esp_err.h"

esp_err_t sd_card_logger_init(void);
esp_err_t sd_card_logger_write(const char *line);
esp_err_t sd_card_logger_get_info(char *buf, size_t len);
bool sd_card_is_mounted(void);
