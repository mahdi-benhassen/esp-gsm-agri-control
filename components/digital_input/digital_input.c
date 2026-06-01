#include "digital_input.h"
#include "config_store.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "DIGITAL_INPUT";

ESP_EVENT_DEFINE_BASE(DIGITAL_INPUT_EVENTS);

#define INPUT_COUNT 2
static const gpio_num_t s_input_pins[INPUT_COUNT] = {
    CONFIG_DIGITAL_INPUT_1_GPIO,
    CONFIG_DIGITAL_INPUT_2_GPIO,
};

static bool s_input_states[INPUT_COUNT] = {false};
static SemaphoreHandle_t s_mutex = NULL;

static bool input_read_raw(int channel) {
  return gpio_get_level(s_input_pins[channel]) > 0;
}

esp_err_t digital_input_init(void) {
  s_mutex = xSemaphoreCreateMutex();
  if (s_mutex == NULL) return ESP_ERR_NO_MEM;

  gpio_config_t io_conf = {.mode = GPIO_MODE_INPUT,
                           .pull_up_en = GPIO_PULLUP_ENABLE,
                           .pull_down_en = GPIO_PULLDOWN_DISABLE,
                           .intr_type = GPIO_INTR_DISABLE,
                           .pin_bit_mask = 0};

  for (int i = 0; i < INPUT_COUNT; i++) {
    io_conf.pin_bit_mask |= (1ULL << s_input_pins[i]);
  }

  gpio_config(&io_conf);

  for (int i = 0; i < INPUT_COUNT; i++) {
    s_input_states[i] = input_read_raw(i);
  }

  ESP_LOGI(TAG, "Digital inputs initialized (GPIO %d, %d)",
           s_input_pins[0], s_input_pins[1]);
  return ESP_OK;
}

bool digital_input_get(int channel) {
  if (channel < 0 || channel >= INPUT_COUNT || s_mutex == NULL)
    return false;

  bool raw = input_read_raw(channel);
  app_config_t cfg;
  bool inverted = false;
  if (config_store_get_snapshot(&cfg) == ESP_OK) {
    inverted = (channel == 0) ? cfg.input_1_inverted : cfg.input_2_inverted;
  }

  bool state = inverted ? !raw : raw;

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  if (state != s_input_states[channel]) {
    s_input_states[channel] = state;

    digital_input_event_t evt = {.channel = channel,
                                 .state = state,
                                 .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL)};
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "Input %d changed to %s", channel, state ? "ON" : "OFF");
    esp_event_post(DIGITAL_INPUT_EVENTS, DIGITAL_INPUT_EVENT_CHANGED,
                   &evt, sizeof(evt), pdMS_TO_TICKS(100));
  } else {
    xSemaphoreGive(s_mutex);
  }

  return state;
}
