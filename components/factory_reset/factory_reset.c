#include "factory_reset.h"
#include "config_store.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "FACTORY_RESET";

#ifndef CONFIG_FACTORY_RESET_GPIO
#define CONFIG_FACTORY_RESET_GPIO 38
#endif

#ifndef CONFIG_FACTORY_RESET_HOLD_MS
#define CONFIG_FACTORY_RESET_HOLD_MS 5000
#endif

static void factory_reset_task(void *pvParameters) {
  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << CONFIG_FACTORY_RESET_GPIO),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&io_conf);

  ESP_LOGI(TAG, "Factory reset button on GPIO %d (hold %d ms)",
           CONFIG_FACTORY_RESET_GPIO, CONFIG_FACTORY_RESET_HOLD_MS);

  while (1) {
    if (gpio_get_level(CONFIG_FACTORY_RESET_GPIO) == 0) {
      int held_ms = 0;
      while (gpio_get_level(CONFIG_FACTORY_RESET_GPIO) == 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
        held_ms += 100;
        if (held_ms >= CONFIG_FACTORY_RESET_HOLD_MS) {
          ESP_LOGW(TAG, "Factory reset triggered! Erasing NVS...");
          nvs_flash_erase();
          ESP_LOGW(TAG, "Rebooting...");
          esp_restart();
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

esp_err_t factory_reset_init(void) {
  BaseType_t ok = xTaskCreate(factory_reset_task, "factory_rst", 2048, NULL, 3, NULL);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Failed to create factory reset task");
    return ESP_ERR_NO_MEM;
  }
  ESP_LOGI(TAG, "Factory reset monitor started");
  return ESP_OK;
}
