#include "safe_state.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "SAFE_STATE";

static uint32_t s_timeout_ms = CONFIG_SAFE_STATE_DEFAULT_TIMEOUT_SEC * 1000U;
static uint32_t s_last_feed_ms = 0;
static bool s_triggered = false;
static SemaphoreHandle_t s_mutex = NULL;
static void (*s_trigger_cb)(void) = NULL;
static TaskHandle_t s_task = NULL;

static void safe_state_task(void *pvParameters) {
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (s_mutex == NULL) continue;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    uint32_t elapsed = now_ms - s_last_feed_ms;
    xSemaphoreGive(s_mutex);

    if (s_timeout_ms > 0 && elapsed > s_timeout_ms && !s_triggered) {
      ESP_LOGW(TAG, "Safe-state triggered after %lu ms without heartbeat", (unsigned long)elapsed);
      s_triggered = true;
      if (s_trigger_cb) {
        s_trigger_cb();
      }
    }
  }
}

esp_err_t safe_state_init(void) {
  s_mutex = xSemaphoreCreateMutex();
  if (s_mutex == NULL) return ESP_ERR_NO_MEM;
  s_last_feed_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
  s_triggered = false;

  BaseType_t ok = xTaskCreate(safe_state_task, "safe_state", 2048, NULL, 4, &s_task);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Failed to create safe_state task");
    return ESP_ERR_NO_MEM;
  }
  ESP_LOGI(TAG, "Safe-state initialized (timeout=%lu ms)", (unsigned long)s_timeout_ms);
  return ESP_OK;
}

void safe_state_feed(void) {
  if (s_mutex == NULL) return;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_last_feed_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
  if (s_triggered) {
    ESP_LOGI(TAG, "Safe-state recovered (heartbeat restored)");
    s_triggered = false;
  }
  xSemaphoreGive(s_mutex);
}

void safe_state_set_timeout_sec(uint32_t timeout_sec) {
  if (s_mutex == NULL) return;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_timeout_ms = timeout_sec * 1000U;
  xSemaphoreGive(s_mutex);
}

bool safe_state_is_triggered(void) {
  bool triggered = false;
  if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
  triggered = s_triggered;
  if (s_mutex) xSemaphoreGive(s_mutex);
  return triggered;
}

void safe_state_register_trigger_cb(void (*cb)(void)) {
  s_trigger_cb = cb;
}
