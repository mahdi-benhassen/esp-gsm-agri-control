#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "app_logic.h"
#include "command_handler.h"
#include "config_store.h"
#include "digital_input.h"
#include "analog_input.h"
#include "eeprom_storage.h"
#include "factory_reset.h"
#include "i2c_bus_manager.h"
#include "lcd_display.h"
#include "modem_manager.h"
#include "mqtt_client_wrapper.h"
#include "relay_control.h"
#include "rs485_manager.h"
#include "rtc_manager.h"
#include "safe_state.h"
#include "sd_card_logger.h"
#include "sensor_hub.h"
#include "system_monitor.h"
#include "web_server.h"

static const char *TAG = "MAIN";

static void heap_alloc_failed_cb(size_t requested_size, uint32_t caps, const char *function_name) {
  ESP_LOGE(TAG, "HEAP ALLOC FAILED: %lu bytes (caps=0x%08lX) in %s",
           (unsigned long)requested_size, (unsigned long)caps,
           function_name ? function_name : "???");
  // In industrial systems, a safe reboot is better than running with corrupted heap
  esp_restart();
}

static void safe_state_trigger_cb(void) {
  ESP_LOGW(TAG, "SAFE STATE: Turning all relays OFF due to comm loss");
  relay_all_off();
}

void app_main(void) {
  ESP_LOGI(TAG, "================================================");
  ESP_LOGI(TAG, "  KC868-A2v3 Smart Controller - Version %s",
           CONFIG_APP_VERSION);
  ESP_LOGI(TAG, "================================================");

  // 0. Register heap failure callback
  heap_caps_register_failed_alloc_callback(heap_alloc_failed_cb);

  // 1. Initialize Non-Volatile Storage
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS flash partition error. Erasing and retrying...");
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  // 2. Initialize network interface stack
  ESP_ERROR_CHECK(esp_netif_init());

  // 3. Create default system event loop
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  // 4. Initialize config store
  ESP_ERROR_CHECK(config_store_init());

  // 5. Initialize I2C bus manager (must be before RTC, LCD, EEPROM)
  err = i2c_bus_manager_init();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "I2C bus init failed: %s", esp_err_to_name(err));
  }

  // 6. Initialize RTC (DS3231)
  err = rtc_manager_init();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "RTC init failed: %s", esp_err_to_name(err));
  }

  // 7. Initialize LCD display
  err = lcd_display_init();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "LCD init failed: %s", esp_err_to_name(err));
  }

  // 8. Initialize relay control
  ESP_ERROR_CHECK(relay_control_init());

  // 9. Initialize digital inputs
  err = digital_input_init();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Digital input init failed: %s", esp_err_to_name(err));
  }

  // 10. Initialize analog inputs (ADC)
  err = analog_input_init();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Analog input init failed: %s", esp_err_to_name(err));
  }

  // 11. Initialize EEPROM (24C02 on I2C bus)
  err = eeprom_storage_init();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "EEPROM init failed: %s", esp_err_to_name(err));
  }

  // 12. Configure remaining free GPIOs as inputs
#if CONFIG_FREEGPIO_ENABLE
  {
    gpio_config_t free_io = {
        .pin_bit_mask = (1ULL << 6),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&free_io);
    ESP_LOGI(TAG, "Free GPIO (6) configured as input");
  }
#endif

  // 13. Initialize sensor hub (1-wire DS18B20)
  err = sensor_hub_init();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Sensor hub init failed: %s", esp_err_to_name(err));
  }

  // 14. Initialize GSM/4G modem
  err = modem_manager_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Modem init failed: %s", esp_err_to_name(err));
    // Do not treat as fatal; WiFi may still provide connectivity
  }

  // 15. Initialize RS485
  err = rs485_init();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "RS485 init failed: %s", esp_err_to_name(err));
  }

  // 16. Initialize SD card logger
  err = sd_card_logger_init();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "SD card init failed: %s", esp_err_to_name(err));
  }

  // 17. Initialize system monitor
  ESP_ERROR_CHECK(system_monitor_init());

  // 18. Initialize safe-state heartbeat watchdog
  err = safe_state_init();
  if (err == ESP_OK) {
    safe_state_register_trigger_cb(safe_state_trigger_cb);
    app_config_t cfg;
    if (config_store_get_snapshot(&cfg) == ESP_OK) {
      safe_state_set_timeout_sec(cfg.safe_state_timeout_sec);
      ESP_LOGI(TAG, "Safe-state timeout set to %lu sec",
               (unsigned long)cfg.safe_state_timeout_sec);
    }
  }

  ESP_LOGI(TAG, "Waiting for cellular network connection...");

  int wait_sec = 0;
  while (!modem_manager_is_connected()) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    wait_sec++;
    if (wait_sec >= 60) {
      ESP_LOGW(TAG, "Timeout waiting for cellular connectivity.");
      break;
    }
  }

  if (modem_manager_is_connected()) {
    ESP_LOGI(TAG, "Cellular connection established after %d seconds.",
             wait_sec);
  }

  // 19. Initialize command handler
  ESP_ERROR_CHECK(command_handler_init());

  // 20. Initialize MQTT client
  err = mqtt_wrapper_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "MQTT init failed: %s", esp_err_to_name(err));
  }

  // 21. Start app logic
  ESP_ERROR_CHECK(app_logic_start());

  // 22. Start web server (WiFi AP+STA + HTTP REST API + Web GUI)
#if CONFIG_WEB_SERVER_ENABLED
  err = web_server_init();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Web server init failed: %s", esp_err_to_name(err));
  }
#endif

  // 23. Start factory reset button monitor
  err = factory_reset_init();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Factory reset init failed: %s", esp_err_to_name(err));
  }

  ESP_LOGI(TAG, "System initialization complete.");
}
