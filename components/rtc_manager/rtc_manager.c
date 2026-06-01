#include "rtc_manager.h"
#include "driver/i2c.h"
#include "esp_log.h"

static const char *TAG = "RTC_MANAGER";

#define DS3231_ADDR     0x68
#define I2C_MASTER_PORT I2C_NUM_0

static bool s_initialized = false;

static inline uint8_t bcd_to_dec(uint8_t val) {
  return (val / 16 * 10) + (val % 16);
}

static inline uint8_t dec_to_bcd(uint8_t val) {
  return (val / 10 * 16) + (val % 10);
}

static esp_err_t i2c_init(void) {
  i2c_config_t conf = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = CONFIG_RTC_I2C_SDA,
      .scl_io_num = CONFIG_RTC_I2C_SCL,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master.clk_speed = 100000,
  };
  esp_err_t err = i2c_param_config(I2C_MASTER_PORT, &conf);
  if (err != ESP_OK) return err;
  return i2c_driver_install(I2C_MASTER_PORT, I2C_MODE_MASTER, 0, 0, 0);
}

static esp_err_t ds3231_read_reg(uint8_t reg, uint8_t *data, size_t len) {
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (DS3231_ADDR << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, reg, true);
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (DS3231_ADDR << 1) | I2C_MASTER_READ, true);
  if (len > 1) i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
  i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
  i2c_master_stop(cmd);
  esp_err_t err = i2c_master_cmd_begin(I2C_MASTER_PORT, cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);
  return err;
}

static esp_err_t ds3231_write_reg(uint8_t reg, uint8_t *data, size_t len) {
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (DS3231_ADDR << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, reg, true);
  i2c_master_write(cmd, data, len, true);
  i2c_master_stop(cmd);
  esp_err_t err = i2c_master_cmd_begin(I2C_MASTER_PORT, cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);
  return err;
}

esp_err_t rtc_manager_init(void) {
  esp_err_t err = i2c_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "I2C init failed: %s", esp_err_to_name(err));
    return err;
  }

  uint8_t test_byte = 0;
  err = ds3231_read_reg(0x00, &test_byte, 1);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "DS3231 not detected at 0x%02X", DS3231_ADDR);
    return err;
  }

  s_initialized = true;
  ESP_LOGI(TAG, "DS3231 RTC initialized");
  return ESP_OK;
}

esp_err_t rtc_manager_get_time(struct tm *timeinfo) {
  if (!s_initialized || timeinfo == NULL) return ESP_ERR_INVALID_STATE;

  uint8_t data[7];
  esp_err_t err = ds3231_read_reg(0x00, data, 7);
  if (err != ESP_OK) return err;

  timeinfo->tm_sec  = bcd_to_dec(data[0] & 0x7F);
  timeinfo->tm_min  = bcd_to_dec(data[1]);
  timeinfo->tm_hour = bcd_to_dec(data[2] & 0x3F);
  timeinfo->tm_wday = bcd_to_dec(data[3]) - 1;
  timeinfo->tm_mday = bcd_to_dec(data[4]);
  timeinfo->tm_mon  = bcd_to_dec(data[5] & 0x1F) - 1;
  timeinfo->tm_year = bcd_to_dec(data[6]) + 100;

  return ESP_OK;
}

esp_err_t rtc_manager_set_time(const struct tm *timeinfo) {
  if (!s_initialized || timeinfo == NULL) return ESP_ERR_INVALID_STATE;

  uint8_t data[7];
  data[0] = dec_to_bcd(timeinfo->tm_sec);
  data[1] = dec_to_bcd(timeinfo->tm_min);
  data[2] = dec_to_bcd(timeinfo->tm_hour);
  data[3] = dec_to_bcd(timeinfo->tm_wday + 1);
  data[4] = dec_to_bcd(timeinfo->tm_mday);
  data[5] = dec_to_bcd(timeinfo->tm_mon + 1);
  data[6] = dec_to_bcd(timeinfo->tm_year - 100);

  return ds3231_write_reg(0x00, data, 7);
}

esp_err_t rtc_manager_get_unixtime(uint32_t *timestamp) {
  struct tm t = {0};
  esp_err_t err = rtc_manager_get_time(&t);
  if (err != ESP_OK) return err;
  *timestamp = (uint32_t)mktime(&t);
  return ESP_OK;
}

float rtc_manager_get_temperature(void) {
  uint8_t data[2];
  if (ds3231_read_reg(0x11, data, 2) != ESP_OK)
    return -999.0f;
  return data[0] + (data[1] >> 6) * 0.25f;
}
