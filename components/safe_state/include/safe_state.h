#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

esp_err_t safe_state_init(void);
void safe_state_feed(void);
void safe_state_set_timeout_sec(uint32_t timeout_sec);
bool safe_state_is_triggered(void);
void safe_state_register_trigger_cb(void (*cb)(void));
