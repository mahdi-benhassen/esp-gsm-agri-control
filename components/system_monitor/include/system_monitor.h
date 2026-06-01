#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint32_t uptime_sec;
  uint32_t free_heap;
  uint32_t min_free_heap;
  int modem_rssi;
  bool mqtt_connected;
  bool modem_connected;
  bool eth_connected;
  bool sd_mounted;
  float rtc_temp;
} system_status_t;

esp_err_t system_monitor_init(void);
esp_err_t system_monitor_get_status(system_status_t *status);
char *system_monitor_status_to_json(const system_status_t *status);
