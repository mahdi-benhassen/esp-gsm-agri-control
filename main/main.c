#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

// Component headers
#include "app_logic.h"
#include "command_handler.h"
#include "config_store.h"
#include "modem_manager.h"
#include "mqtt_client_wrapper.h"
#include "relay_control.h"
#include "sensor_hub.h"
#include "system_monitor.h"

static const char *TAG = "MAIN";

void app_main(void) {
  ESP_LOGI(TAG, "================================================");
  ESP_LOGI(TAG, "  Agriculture Smart Control System - Version %s",
           CONFIG_APP_VERSION);
  ESP_LOGI(TAG, "================================================");

  // 1. Initialize Non-Volatile Storage (NVS)
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

  // 4. Initialize config store (NVS key-value storage)
  ESP_ERROR_CHECK(config_store_init());

  // 5. Initialize system monitor (watchdogs, heap reporting)
  ESP_ERROR_CHECK(system_monitor_init());

  // 6. Initialize relay control GPIO pins
  ESP_ERROR_CHECK(relay_control_init());

  // 7. Initialize sensor hub (soil moisture ADC + DHT22 GPIO)
  ESP_ERROR_CHECK(sensor_hub_init());

  // 8. Initialize GSM/4G modem & establish PPPoS interface
  err = modem_manager_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Modem Manager initialization failed: %s",
             esp_err_to_name(err));
    // We log error but don't abort, system will try to boot or handle it
  }

  ESP_LOGI(TAG, "Waiting for cellular network connection...");

  // 9. Wait for modem connection (up to 60 seconds)
  int wait_sec = 0;
  while (!modem_manager_is_connected()) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    wait_sec++;
    if (wait_sec >= 60) {
      ESP_LOGW(TAG, "Timeout waiting for cellular connectivity. Continuing "
                    "initialization...");
      break;
    }
  }

  if (modem_manager_is_connected()) {
    ESP_LOGI(TAG,
             "Cellular connection established successfully after %d seconds.",
             wait_sec);
  }

  // 10. Initialize remote command handler before MQTT can receive commands
  ESP_ERROR_CHECK(command_handler_init());

  // 11. Initialize MQTT client wrapper (connect to broker)
  err = mqtt_wrapper_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "MQTT Wrapper initialization failed: %s",
             esp_err_to_name(err));
  }

  // 12. Start app logic (auto-irrigation scheduling and events orchestration)
  ESP_ERROR_CHECK(app_logic_start());

  ESP_LOGI(TAG, "System initialization complete. Run loop handed to FreeRTOS.");
}
