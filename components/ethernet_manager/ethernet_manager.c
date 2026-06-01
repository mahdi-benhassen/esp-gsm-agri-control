#include "ethernet_manager.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ETHERNET";

esp_err_t ethernet_manager_init(void) {
  ESP_LOGW(TAG, "W5500 requires ESP-IDF >= v5.3 or espressif/esp_eth_w5500 component");
  return ESP_ERR_NOT_SUPPORTED;
}

bool ethernet_manager_is_connected(void) {
  return false;
}

esp_err_t ethernet_manager_get_ip(char *buf, size_t len) {
  if (buf == NULL) return ESP_ERR_INVALID_ARG;
  snprintf(buf, len, "not supported");
  return ESP_OK;
}
