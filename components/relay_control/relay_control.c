#include "relay_control.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "RELAY_CONTROL";

ESP_EVENT_DEFINE_BASE(RELAY_EVENTS);

static const gpio_num_t s_relay_pins[RELAY_CH_MAX] = {
    CONFIG_RELAY_1_GPIO,
    CONFIG_RELAY_2_GPIO,
    CONFIG_RELAY_3_GPIO,
    CONFIG_RELAY_4_GPIO
};

static esp_timer_handle_t s_timers[RELAY_CH_MAX];
static bool s_relay_states[RELAY_CH_MAX] = { false };
static SemaphoreHandle_t s_mutex = NULL;

static inline int gpio_level_for_state(bool on)
{
#ifdef CONFIG_RELAY_ACTIVE_LOW
    return on ? 0 : 1;
#else
    return on ? 1 : 0;
#endif
}

static void relay_timer_cb(void *arg)
{
    relay_channel_t ch = (relay_channel_t)(uintptr_t)arg;
    ESP_LOGW(TAG, "Timer expired for channel %d! Auto-turning OFF.", ch);
    relay_set(ch, false);
}

esp_err_t relay_control_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
        .pin_bit_mask = 0
    };

    for (int i = 0; i < RELAY_CH_MAX; i++) {
        io_conf.pin_bit_mask |= (1ULL << s_relay_pins[i]);
    }

    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(err));
        return err;
    }

    // Set defaults (all off)
    for (int i = 0; i < RELAY_CH_MAX; i++) {
        gpio_set_level(s_relay_pins[i], gpio_level_for_state(false));
        s_relay_states[i] = false;

        // Create timers for auto-off
        esp_timer_create_args_t timer_args = {
            .callback = &relay_timer_cb,
            .arg = (void *)(uintptr_t)i,
            .name = "relay_auto_off"
        };
        esp_timer_create(&timer_args, &s_timers[i]);
    }

    ESP_LOGI(TAG, "Relays initialized successfully.");
    return ESP_OK;
}

esp_err_t relay_set(relay_channel_t ch, bool on)
{
    if (ch >= RELAY_CH_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    // If turning off, stop any active timer
    if (!on) {
        esp_timer_stop(s_timers[ch]);
    }

    gpio_set_level(s_relay_pins[ch], gpio_level_for_state(on));
    s_relay_states[ch] = on;

    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "Relay %d set to %s", ch, on ? "ON" : "OFF");

    // Post event
    relay_event_data_t event_data = {
        .channel = ch,
        .state = on
    };
    esp_event_post(RELAY_EVENTS, RELAY_EVENT_STATE_CHANGED, &event_data, sizeof(event_data), pdMS_TO_TICKS(100));

    return ESP_OK;
}

bool relay_get(relay_channel_t ch)
{
    if (ch >= RELAY_CH_MAX) {
        return false;
    }
    bool state = false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    state = s_relay_states[ch];
    xSemaphoreGive(s_mutex);
    return state;
}

esp_err_t relay_set_timed(relay_channel_t ch, uint32_t duration_sec)
{
    if (ch >= RELAY_CH_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    if (duration_sec == 0) {
        return relay_set(ch, false);
    }

    if (duration_sec > CONFIG_RELAY_MAX_ON_TIME_SEC) {
        duration_sec = CONFIG_RELAY_MAX_ON_TIME_SEC;
        ESP_LOGW(TAG, "Duration clamped to max config limit: %d sec", CONFIG_RELAY_MAX_ON_TIME_SEC);
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    // Stop active timer if any
    esp_timer_stop(s_timers[ch]);

    // Turn ON
    gpio_set_level(s_relay_pins[ch], gpio_level_for_state(true));
    s_relay_states[ch] = true;

    // Start timer (convert to microseconds)
    uint64_t timeout_us = (uint64_t)duration_sec * 1000000ULL;
    esp_timer_start_once(s_timers[ch], timeout_us);

    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "Relay %d set to ON for %lu seconds", ch, (unsigned long)duration_sec);

    // Post event
    relay_event_data_t event_data = {
        .channel = ch,
        .state = true
    };
    esp_event_post(RELAY_EVENTS, RELAY_EVENT_STATE_CHANGED, &event_data, sizeof(event_data), pdMS_TO_TICKS(100));

    return ESP_OK;
}

esp_err_t relay_all_off(void)
{
    ESP_LOGW(TAG, "EMERGENCY SHUTDOWN: Turning all relays OFF");
    for (int i = 0; i < RELAY_CH_MAX; i++) {
        relay_set(i, false);
    }
    return ESP_OK;
}
