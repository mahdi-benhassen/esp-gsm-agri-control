#include "analog_input.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "ANALOG_INPUT";

static const adc_channel_t s_adc_channels[ANALOG_INPUT_COUNT] = {
    CONFIG_ANALOG_INPUT_1_ADC_CHANNEL,
    CONFIG_ANALOG_INPUT_2_ADC_CHANNEL,
};

static const gpio_num_t s_gpio_pins[ANALOG_INPUT_COUNT] = {
    CONFIG_ANALOG_INPUT_1_GPIO,
    CONFIG_ANALOG_INPUT_2_GPIO,
};

static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t s_adc_cali[ANALOG_INPUT_COUNT] = {NULL};
static bool s_cali_enabled[ANALOG_INPUT_COUNT] = {false};
static SemaphoreHandle_t s_mutex = NULL;

esp_err_t analog_input_init(void) {
  s_mutex = xSemaphoreCreateMutex();
  if (s_mutex == NULL) return ESP_ERR_NO_MEM;

  adc_oneshot_unit_init_cfg_t init_config = {
      .unit_id = ADC_UNIT_1,
      .ulp_mode = ADC_ULP_MODE_DISABLE,
  };
  esp_err_t err = adc_oneshot_new_unit(&init_config, &s_adc_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "ADC unit init failed: %s", esp_err_to_name(err));
    return err;
  }

  adc_oneshot_chan_cfg_t chan_cfg = {
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_12,
  };

  for (int i = 0; i < ANALOG_INPUT_COUNT; i++) {
    err = adc_oneshot_config_channel(s_adc_handle, s_adc_channels[i], &chan_cfg);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "ADC channel config failed for ch %d: %s",
               s_adc_channels[i], esp_err_to_name(err));
      return err;
    }

    // Create calibration scheme
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    esp_err_t cali_err = adc_cali_create_scheme_curve_fitting(&cali_config, &s_adc_cali[i]);
    if (cali_err == ESP_OK) {
      s_cali_enabled[i] = true;
      ESP_LOGI(TAG, "ADC calibration enabled for ch %d", s_adc_channels[i]);
    } else {
      ESP_LOGW(TAG, "ADC calibration failed for ch %d: %s",
               s_adc_channels[i], esp_err_to_name(cali_err));
      s_cali_enabled[i] = false;
    }

    ESP_LOGI(TAG, "Analog input %d configured on GPIO %d (ADC ch %d)",
             i + 1, s_gpio_pins[i], s_adc_channels[i]);
  }

  ESP_LOGI(TAG, "Analog inputs initialized");
  return ESP_OK;
}

int analog_input_read_raw(int channel) {
  if (channel < 0 || channel >= ANALOG_INPUT_COUNT || s_adc_handle == NULL)
    return -1;

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  int raw = 0;
  esp_err_t err = adc_oneshot_read(s_adc_handle, s_adc_channels[channel], &raw);
  xSemaphoreGive(s_mutex);

  if (err != ESP_OK) {
    ESP_LOGW(TAG, "ADC read failed on ch %d: %s",
             s_adc_channels[channel], esp_err_to_name(err));
    return -1;
  }
  return raw;
}

int analog_input_read_mv(int channel) {
  int raw = analog_input_read_raw(channel);
  if (raw < 0) return -1;

  int voltage_mv = 0;
  if (s_cali_enabled[channel]) {
    esp_err_t err = adc_cali_raw_to_voltage(s_adc_cali[channel], raw, &voltage_mv);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "ADC cali conversion failed: %s", esp_err_to_name(err));
      return -1;
    }
  } else {
    // Fallback linear scaling
    voltage_mv = (int)((int64_t)raw * 3300LL / 4095LL);
  }

  // Scale to terminal voltage via divider ratio
  int64_t terminal_mv = (int64_t)voltage_mv * CONFIG_ANALOG_INPUT_DIVIDER_RATIO / 1000LL;
  return (int)terminal_mv;
}
