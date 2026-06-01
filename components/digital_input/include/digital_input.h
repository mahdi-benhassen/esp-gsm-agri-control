#pragma once

#include "esp_err.h"
#include "esp_event.h"
#include <stdbool.h>
#include <stdint.h>

ESP_EVENT_DECLARE_BASE(DIGITAL_INPUT_EVENTS);
enum {
  DIGITAL_INPUT_EVENT_CHANGED,
};

typedef struct {
  int channel;
  bool state;
  uint32_t timestamp_ms;
} digital_input_event_t;

esp_err_t digital_input_init(void);
bool digital_input_get(int channel);
