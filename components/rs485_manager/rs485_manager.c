#include "rs485_manager.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"

static const char *TAG = "RS485";

static int s_uart_num = UART_NUM_1;

esp_err_t rs485_init(void) {
  uart_config_t uart_config = {
      .baud_rate = CONFIG_RS485_BAUD_RATE,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  esp_err_t err = uart_driver_install(s_uart_num, 1024, 1024, 0, NULL, 0);
  if (err != ESP_OK) return err;

  err = uart_param_config(s_uart_num, &uart_config);
  if (err != ESP_OK) return err;

  err = uart_set_pin(s_uart_num, CONFIG_RS485_TXD, CONFIG_RS485_RXD,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (err != ESP_OK) return err;

  uart_set_mode(s_uart_num, UART_MODE_RS485_HALF_DUPLEX);

  ESP_LOGI(TAG, "RS485 initialized (TXD=%d, RXD=%d, baud=%d)",
           CONFIG_RS485_TXD, CONFIG_RS485_RXD, CONFIG_RS485_BAUD_RATE);
  return ESP_OK;
}

esp_err_t rs485_send(const uint8_t *data, size_t len) {
  int sent = uart_write_bytes(s_uart_num, data, len);
  if (sent != len) {
    ESP_LOGE(TAG, "RS485 write failed: %d/%d bytes", sent, len);
    return ESP_FAIL;
  }
  return ESP_OK;
}

static uint16_t modbus_crc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 1)
        crc = (crc >> 1) ^ 0xA001;
      else
        crc >>= 1;
    }
  }
  return crc;
}

esp_err_t rs485_send_modbus_rtu(uint8_t slave_addr, uint8_t func_code,
                                 uint16_t reg, uint16_t value) {
  uint8_t frame[8];
  frame[0] = slave_addr;
  frame[1] = func_code;
  frame[2] = (reg >> 8) & 0xFF;
  frame[3] = reg & 0xFF;
  frame[4] = (value >> 8) & 0xFF;
  frame[5] = value & 0xFF;

  uint16_t crc = modbus_crc16(frame, 6);
  frame[6] = crc & 0xFF;
  frame[7] = (crc >> 8) & 0xFF;

  return rs485_send(frame, 8);
}

esp_err_t rs485_set_baudrate(int baudrate) {
  return uart_set_baudrate(s_uart_num, baudrate);
}
