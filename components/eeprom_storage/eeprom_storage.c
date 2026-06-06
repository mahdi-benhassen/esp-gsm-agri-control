#include "eeprom_storage.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "EEPROM";

#define EEPROM_ADDR     0x50
#define I2C_MASTER_PORT I2C_NUM_0
#define EEPROM_PAGE_SIZE 8
#define EEPROM_WRITE_DELAY_MS 6

static bool s_detected = false;

static esp_err_t eeprom_write_page(uint8_t mem_addr, const uint8_t *data, size_t len) {
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (EEPROM_ADDR << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, mem_addr, true);
  i2c_master_write(cmd, (uint8_t *)data, len, true);
  i2c_master_stop(cmd);
  esp_err_t err = i2c_master_cmd_begin(I2C_MASTER_PORT, cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);
  return err;
}

esp_err_t eeprom_storage_init(void) {
  uint8_t test = 0;
  esp_err_t err = eeprom_storage_read(0, &test, 1);
  if (err == ESP_OK) {
    s_detected = true;
    ESP_LOGI(TAG, "24C02 EEPROM detected at 0x%02X", EEPROM_ADDR);
  } else {
    ESP_LOGW(TAG, "24C02 EEPROM not detected at 0x%02X", EEPROM_ADDR);
  }
  return ESP_OK;
}

bool eeprom_storage_detected(void) {
  return s_detected;
}

esp_err_t eeprom_storage_read(uint8_t addr, uint8_t *data, size_t len) {
  if (data == NULL || len == 0 || addr + len > EEPROM_STORAGE_SIZE)
    return ESP_ERR_INVALID_ARG;

  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (EEPROM_ADDR << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, addr, true);
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (EEPROM_ADDR << 1) | I2C_MASTER_READ, true);
  if (len > 1) {
    i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
  }
  i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
  i2c_master_stop(cmd);
  esp_err_t err = i2c_master_cmd_begin(I2C_MASTER_PORT, cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);
  return err;
}

esp_err_t eeprom_storage_write(uint8_t addr, const uint8_t *data, size_t len) {
  if (data == NULL || len == 0 || addr + len > EEPROM_STORAGE_SIZE)
    return ESP_ERR_INVALID_ARG;

  size_t written = 0;
  while (written < len) {
    uint8_t page_offset = (addr + written) % EEPROM_PAGE_SIZE;
    size_t page_remain = EEPROM_PAGE_SIZE - page_offset;
    size_t chunk = len - written;
    if (chunk > page_remain) chunk = page_remain;

    esp_err_t err = eeprom_write_page(addr + written, data + written, chunk);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "EEPROM write failed at addr %d: %s",
               addr + written, esp_err_to_name(err));
      return err;
    }
    written += chunk;
    vTaskDelay(pdMS_TO_TICKS(EEPROM_WRITE_DELAY_MS));
  }
  return ESP_OK;
}
