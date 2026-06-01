#pragma once

#include "esp_err.h"
#include "esp_event.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    RELAY_CH_1 = 0,
    RELAY_CH_2,
    RELAY_CH_MAX
} relay_channel_t;

ESP_EVENT_DECLARE_BASE(RELAY_EVENTS);
enum { RELAY_EVENT_STATE_CHANGED };

typedef struct {
    relay_channel_t channel;
    bool state;
} relay_event_data_t;

esp_err_t relay_control_init(void);
esp_err_t relay_set(relay_channel_t ch, bool on);
bool relay_get(relay_channel_t ch);
esp_err_t relay_set_timed(relay_channel_t ch, uint32_t duration_sec);
esp_err_t relay_all_off(void);
