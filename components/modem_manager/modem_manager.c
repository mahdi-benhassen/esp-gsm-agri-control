#include "modem_manager.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_modem_api.h"
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "MODEM_MANAGER";

static esp_modem_dce_t *s_dce = NULL;
static esp_netif_t *s_esp_netif = NULL;
static bool s_connected = false;
static int s_last_rssi = -1;
static SemaphoreHandle_t s_mutex = NULL;

static void modem_power_on(void) {
  if (CONFIG_MODEM_POWER_PIN < 0) {
    ESP_LOGI(TAG, "No PWRKEY pin configured, skipping power pulse.");
    return;
  }

  ESP_LOGI(TAG, "Power pulsing modem (PWRKEY GPIO %d)...",
           CONFIG_MODEM_POWER_PIN);
  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << CONFIG_MODEM_POWER_PIN),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&io_conf);

  gpio_set_level(CONFIG_MODEM_POWER_PIN, 0);
  vTaskDelay(pdMS_TO_TICKS(CONFIG_MODEM_POWER_ON_PULSE_MS));
  gpio_set_level(CONFIG_MODEM_POWER_PIN, 1);

  ESP_LOGI(TAG, "Power pulse complete. Waiting for modem boot...");
  vTaskDelay(pdMS_TO_TICKS(3000));
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t event_id,
                        void *data) {
  if (event_id == IP_EVENT_PPP_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
    ESP_LOGI(TAG, "PPPoS Got IP: " IPSTR, IP2STR(&event->ip_info.ip));

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_connected = true;
    xSemaphoreGive(s_mutex);
  } else if (event_id == IP_EVENT_PPP_LOST_IP) {
    ESP_LOGW(TAG, "PPPoS Lost IP");

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_connected = false;
    xSemaphoreGive(s_mutex);
  }
}

static void on_ppp_changed(void *arg, esp_event_base_t base, int32_t event_id,
                           void *data) {
  ESP_LOGI(TAG, "PPP Status changed event: %ld", event_id);
}

esp_err_t modem_manager_init(void) {
  if (s_mutex == NULL) {
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
      return ESP_ERR_NO_MEM;
    }
  }

  // Register event handlers
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                             &on_ip_event, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(NETIF_PPP_STATUS, ESP_EVENT_ANY_ID,
                                             &on_ppp_changed, NULL));

  // Power cycle modem
  modem_power_on();

  // Create PPP Netif
  esp_netif_config_t netif_ppp_config = ESP_NETIF_DEFAULT_PPP();
  s_esp_netif = esp_netif_new(&netif_ppp_config);
  if (s_esp_netif == NULL) {
    ESP_LOGE(TAG, "Failed to create PPP netif");
    return ESP_FAIL;
  }

  // Enable PPP events
  esp_netif_ppp_config_t ppp_config = {.ppp_phase_event_enabled = true,
                                       .ppp_error_event_enabled = true};
  esp_netif_ppp_set_params(s_esp_netif, &ppp_config);

  // DTE config
  esp_modem_dte_config_t dte_config = ESP_MODEM_DTE_DEFAULT_CONFIG();
  dte_config.uart_config.tx_io_num = CONFIG_MODEM_UART_TX_PIN;
  dte_config.uart_config.rx_io_num = CONFIG_MODEM_UART_RX_PIN;
  dte_config.uart_config.rts_io_num = CONFIG_MODEM_UART_RTS_PIN;
  dte_config.uart_config.cts_io_num = CONFIG_MODEM_UART_CTS_PIN;
  dte_config.uart_config.rx_buffer_size = CONFIG_MODEM_UART_RX_BUFFER_SIZE;
  dte_config.uart_config.tx_buffer_size = CONFIG_MODEM_UART_TX_BUFFER_SIZE;
  dte_config.uart_config.baud_rate = CONFIG_MODEM_UART_BAUD_RATE;

  // DCE config
  esp_modem_dce_config_t dce_config =
      ESP_MODEM_DCE_DEFAULT_CONFIG(CONFIG_MODEM_APN);

  esp_modem_dce_device_t modem_dev = ESP_MODEM_DCE_SIM7600;
#ifdef CONFIG_MODEM_TYPE_SIM800L
  modem_dev = ESP_MODEM_DCE_SIM800;
#endif
  ESP_LOGI(TAG, "Initializing esp_modem (%s) with APN: %s",
#ifdef CONFIG_MODEM_TYPE_SIM800L
           "SIM800L",
#else
           "SIM7600",
#endif
           CONFIG_MODEM_APN);
  s_dce = esp_modem_new_dev(modem_dev, &dte_config, &dce_config,
                            s_esp_netif);
  if (s_dce == NULL) {
    ESP_LOGE(TAG, "Failed to create modem DCE");
    return ESP_FAIL;
  }

  int initial_rssi = -1;
  int initial_ber = -1;
  esp_err_t sq_err =
      esp_modem_get_signal_quality(s_dce, &initial_rssi, &initial_ber);
  if (sq_err == ESP_OK) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_last_rssi = initial_rssi;
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "Initial signal quality: RSSI=%d, BER=%d", initial_rssi,
             initial_ber);
  } else {
    ESP_LOGW(TAG, "Initial signal quality query failed: %s",
             esp_err_to_name(sq_err));
  }

  ESP_LOGI(TAG, "Switching modem to data mode (PPP)...");
  esp_err_t err = esp_modem_set_mode(s_dce, ESP_MODEM_MODE_DATA);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set modem to DATA mode: %s", esp_err_to_name(err));
    return err;
  }

  ESP_LOGI(TAG, "Modem initialized successfully.");
  return ESP_OK;
}

bool modem_manager_is_connected(void) {
  bool conn = false;
  if (s_mutex) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    conn = s_connected;
    xSemaphoreGive(s_mutex);
  }
  return conn;
}

int modem_manager_get_rssi(void) {
  int rssi = -1;
  if (s_mutex) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    rssi = s_last_rssi;
    xSemaphoreGive(s_mutex);
  }
  return rssi;
}

esp_err_t modem_manager_reconnect(void) {
  ESP_LOGW(TAG, "Modem reconnection requested...");
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_connected = false;
  if (s_dce) {
    esp_modem_set_mode(s_dce, ESP_MODEM_MODE_COMMAND);
    esp_modem_set_mode(s_dce, ESP_MODEM_MODE_DATA);
  }
  xSemaphoreGive(s_mutex);
  return ESP_OK;
}

esp_err_t modem_manager_deinit(void) {
  ESP_LOGI(TAG, "Deinitializing modem...");
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (s_dce) {
    esp_modem_set_mode(s_dce, ESP_MODEM_MODE_COMMAND);
    esp_modem_destroy(s_dce);
    s_dce = NULL;
  }
  s_connected = false;
  xSemaphoreGive(s_mutex);
  return ESP_OK;
}
