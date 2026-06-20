#include "i2c_bus_manager.h"
#include "esp_log.h"

static const char *TAG = "I2C_BUS";
static bool s_initialized = false;

esp_err_t i2c_bus_manager_init(void) {
  if (s_initialized) {
    ESP_LOGW(TAG, "I2C bus already initialized");
    return ESP_OK;
  }

  i2c_config_t conf = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = CONFIG_RTC_I2C_SDA,
      .scl_io_num = CONFIG_RTC_I2C_SCL,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master.clk_speed = 100000,
  };
  esp_err_t err = i2c_param_config(I2C_NUM_0, &conf);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(err));
    return err;
  }
  err = i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(err));
    return err;
  }
  s_initialized = true;
  ESP_LOGI(TAG, "I2C bus manager initialized (SDA=%d, SCL=%d)",
           CONFIG_RTC_I2C_SDA, CONFIG_RTC_I2C_SCL);
  return ESP_OK;
}

bool i2c_bus_manager_is_initialized(void) {
  return s_initialized;
}

i2c_port_t i2c_bus_manager_get_port(void) {
  return I2C_NUM_0;
}
