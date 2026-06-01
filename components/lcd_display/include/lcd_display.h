#pragma once

#include "esp_err.h"
#include <stdbool.h>

esp_err_t lcd_display_init(void);
esp_err_t lcd_display_clear(void);
esp_err_t lcd_display_flush(void);
esp_err_t lcd_display_show_line(int line, const char *text);
esp_err_t lcd_display_show(const char *line1, const char *line2,
                           const char *line3);
void lcd_display_set_enabled(bool enabled);
