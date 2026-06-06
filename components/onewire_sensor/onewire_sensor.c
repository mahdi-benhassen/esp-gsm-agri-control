#include "onewire_sensor.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ONEWIRE";

static portMUX_TYPE s_onewire_spinlock = portMUX_INITIALIZER_UNLOCKED;

static bool onewire_reset(int pin) {
  portENTER_CRITICAL(&s_onewire_spinlock);
  gpio_set_direction(pin, GPIO_MODE_OUTPUT_OD);
  gpio_set_level(pin, 0);
  esp_rom_delay_us(480);
  gpio_set_level(pin, 1);
  esp_rom_delay_us(70);
  gpio_set_direction(pin, GPIO_MODE_INPUT);

  bool presence = (gpio_get_level(pin) == 0);
  esp_rom_delay_us(410);
  portEXIT_CRITICAL(&s_onewire_spinlock);
  return presence;
}

static void onewire_write_bit(int pin, int bit) {
  portENTER_CRITICAL(&s_onewire_spinlock);
  gpio_set_direction(pin, GPIO_MODE_OUTPUT_OD);
  gpio_set_level(pin, 0);
  esp_rom_delay_us(bit ? 2 : 60);
  gpio_set_level(pin, 1);
  esp_rom_delay_us(bit ? 58 : 2);
  portEXIT_CRITICAL(&s_onewire_spinlock);
}

static int onewire_read_bit(int pin) {
  portENTER_CRITICAL(&s_onewire_spinlock);
  gpio_set_direction(pin, GPIO_MODE_OUTPUT_OD);
  gpio_set_level(pin, 0);
  esp_rom_delay_us(2);
  gpio_set_direction(pin, GPIO_MODE_INPUT);
  esp_rom_delay_us(8);
  int bit = gpio_get_level(pin);
  esp_rom_delay_us(50);
  portEXIT_CRITICAL(&s_onewire_spinlock);
  return bit;
}

static void onewire_write_byte(int pin, uint8_t byte) {
  for (int i = 0; i < 8; i++) {
    onewire_write_bit(pin, byte & 0x01);
    byte >>= 1;
  }
}

static uint8_t onewire_read_byte(int pin) {
  uint8_t byte = 0;
  for (int i = 0; i < 8; i++) {
    byte |= (onewire_read_bit(pin) << i);
  }
  return byte;
}

esp_err_t onewire_init(int pin) {
  gpio_reset_pin(pin);
  gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
  ESP_LOGI(TAG, "1-Wire initialized on GPIO %d", pin);
  return ESP_OK;
}

esp_err_t onewire_deinit(int pin) {
  gpio_reset_pin(pin);
  return ESP_OK;
}

bool onewire_is_present(int pin) {
  return onewire_reset(pin);
}

esp_err_t onewire_read_temperature(int pin, float *temp_c) {
  if (temp_c == NULL) return ESP_ERR_INVALID_ARG;

  if (!onewire_reset(pin)) {
    ESP_LOGE(TAG, "No DS18B20 on GPIO %d", pin);
    return ESP_ERR_NOT_FOUND;
  }

  onewire_write_byte(pin, 0xCC);  // Skip ROM
  onewire_write_byte(pin, 0x44);  // Convert T

  vTaskDelay(pdMS_TO_TICKS(750));

  if (!onewire_reset(pin)) {
    return ESP_ERR_TIMEOUT;
  }

  onewire_write_byte(pin, 0xCC);  // Skip ROM
  onewire_write_byte(pin, 0xBE);  // Read Scratchpad

  uint8_t lsb = onewire_read_byte(pin);
  uint8_t msb = onewire_read_byte(pin);

  int16_t raw = (msb << 8) | lsb;
  *temp_c = raw * 0.0625f;

  return ESP_OK;
}
