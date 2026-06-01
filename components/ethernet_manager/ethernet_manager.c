#include "ethernet_manager.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "ETHERNET";

static esp_netif_t *s_eth_netif = NULL;
static esp_eth_handle_t s_eth_handle = NULL;
static bool s_connected = false;
static SemaphoreHandle_t s_mutex = NULL;

static void eth_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
  switch (event_id) {
  case ETHERNET_EVENT_CONNECTED:
    ESP_LOGI(TAG, "Ethernet Link Up");
    break;
  case ETHERNET_EVENT_DISCONNECTED:
    ESP_LOGW(TAG, "Ethernet Link Down");
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_connected = false;
    xSemaphoreGive(s_mutex);
    break;
  case ETHERNET_EVENT_START:
    ESP_LOGI(TAG, "Ethernet Started");
    break;
  case ETHERNET_EVENT_STOP:
    ESP_LOGI(TAG, "Ethernet Stopped");
    break;
  default:
    break;
  }
}

static void got_ip_event_handler(void *arg, esp_event_base_t base,
                                  int32_t event_id, void *event_data) {
  ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
  ESP_LOGI(TAG, "Ethernet Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_connected = true;
  xSemaphoreGive(s_mutex);
}

esp_err_t ethernet_manager_init(void) {
  s_mutex = xSemaphoreCreateMutex();
  if (s_mutex == NULL) return ESP_ERR_NO_MEM;

  s_eth_netif = esp_netif_create_default_wifi_eth();

  eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
  eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();

  // W5500 uses SPI
  spi_bus_config_t buscfg = {
      .mosi_io_num = CONFIG_ETH_SPI_MOSI,
      .miso_io_num = CONFIG_ETH_SPI_MISO,
      .sclk_io_num = CONFIG_ETH_SPI_SCK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
  };

  esp_err_t err = spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
    return err;
  }

  eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(SPI3_HOST);
  w5500_config.int_gpio_num = CONFIG_ETH_INT_GPIO;
  w5500_config.poll_period_ms = CONFIG_ETH_POLL_PERIOD_MS;

  esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
  if (mac == NULL) {
    ESP_LOGE(TAG, "Failed to create W5500 MAC");
    return ESP_FAIL;
  }

  esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);
  if (phy == NULL) {
    ESP_LOGE(TAG, "Failed to create W5500 PHY");
    return ESP_FAIL;
  }

  esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
  err = esp_eth_driver_install(&eth_config, &s_eth_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Ethernet driver install failed: %s", esp_err_to_name(err));
    return err;
  }

  esp_netif_attach(s_eth_netif, esp_eth_new_netif_glue(s_eth_handle));

  esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                             &eth_event_handler, NULL);
  esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                             &got_ip_event_handler, NULL);

  err = esp_eth_start(s_eth_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Ethernet start failed: %s", esp_err_to_name(err));
    return err;
  }

  ESP_LOGI(TAG, "Ethernet W5500 initialized");
  return ESP_OK;
}

bool ethernet_manager_is_connected(void) {
  bool conn = false;
  if (s_mutex) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    conn = s_connected;
    xSemaphoreGive(s_mutex);
  }
  return conn;
}

esp_err_t ethernet_manager_get_ip(char *buf, size_t len) {
  if (buf == NULL || s_eth_netif == NULL) return ESP_ERR_INVALID_ARG;

  esp_netif_ip_info_t ip_info;
  if (esp_netif_get_ip_info(s_eth_netif, &ip_info) != ESP_OK) {
    snprintf(buf, len, "0.0.0.0");
    return ESP_OK;
  }
  snprintf(buf, len, IPSTR, IP2STR(&ip_info.ip));
  return ESP_OK;
}
