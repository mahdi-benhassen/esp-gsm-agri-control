#include "system_monitor.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "modem_manager.h"
#include "mqtt_client_wrapper.h"
#include "rtc_manager.h"

static const char *TAG = "SYSTEM_MONITOR";
static int64_t s_boot_time = 0;
static TaskHandle_t s_task = NULL;

static void monitor_task(void *pvParameters) {
  ESP_LOGI(TAG, "System Monitor task started.");

  while (1) {
    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t min_free = esp_get_minimum_free_heap_size();
    uint32_t uptime = (uint32_t)(esp_timer_get_time() / 1000000ULL);

    ESP_LOGI(TAG, "Uptime: %lu s | Free Heap: %lu B | Min: %lu B",
             (unsigned long)uptime, (unsigned long)free_heap,
             (unsigned long)min_free);

    if (free_heap < CONFIG_SYSMON_LOW_HEAP_THRESHOLD) {
      ESP_LOGW(TAG, "Low heap warning: %lu B", (unsigned long)free_heap);
    }

    vTaskDelay(pdMS_TO_TICKS(CONFIG_SYSMON_REPORT_INTERVAL_SEC * 1000));
  }
}

esp_err_t system_monitor_init(void) {
  s_boot_time = esp_timer_get_time();

  BaseType_t ok = xTaskCreate(monitor_task, "sys_mon", 3072, NULL, 3, &s_task);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Failed to create monitor task");
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG, "System Monitor initialized");
  return ESP_OK;
}

esp_err_t system_monitor_get_status(system_status_t *status) {
  if (status == NULL) return ESP_ERR_INVALID_ARG;

  status->uptime_sec =
      (uint32_t)((esp_timer_get_time() - s_boot_time) / 1000000ULL);
  status->free_heap = esp_get_free_heap_size();
  status->min_free_heap = esp_get_minimum_free_heap_size();
  status->modem_rssi = modem_manager_get_rssi();
  status->mqtt_connected = mqtt_wrapper_is_connected();
  status->modem_connected = modem_manager_is_connected();
  status->eth_connected = false;
  status->sd_mounted = sd_card_is_mounted();
  status->rtc_temp = rtc_manager_get_temperature();
  return ESP_OK;
}

char *system_monitor_status_to_json(const system_status_t *status) {
  if (status == NULL) return NULL;

  cJSON *root = cJSON_CreateObject();
  if (root == NULL) return NULL;

  cJSON_AddNumberToObject(root, "uptime", status->uptime_sec);
  cJSON_AddNumberToObject(root, "free_heap", status->free_heap);
  cJSON_AddNumberToObject(root, "min_free_heap", status->min_free_heap);
  cJSON_AddNumberToObject(root, "modem_rssi", status->modem_rssi);
  cJSON_AddBoolToObject(root, "mqtt_connected", status->mqtt_connected);
  cJSON_AddBoolToObject(root, "modem_connected", status->modem_connected);
  cJSON_AddBoolToObject(root, "eth_connected", status->eth_connected);
  cJSON_AddBoolToObject(root, "sd_mounted", status->sd_mounted);
  cJSON_AddNumberToObject(root, "rtc_temp", status->rtc_temp);

  char *json_str = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return json_str;
}
