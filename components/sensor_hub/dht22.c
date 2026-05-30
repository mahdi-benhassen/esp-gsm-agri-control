#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sensor_hub.h"

static const char *TAG = "DHT22";
static gpio_num_t s_dht_pin;

esp_err_t dht22_init(gpio_num_t pin)
{
    s_dht_pin = pin;
    gpio_reset_pin(s_dht_pin);
    gpio_set_direction(s_dht_pin, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_level(s_dht_pin, 1); // Keep high by default (pull-up pull)
    ESP_LOGI(TAG, "DHT22 initialized on GPIO %d", pin);
    return ESP_OK;
}

static int wait_or_timeout(uint16_t micro_seconds, int level)
{
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(s_dht_pin) == level) {
        if ((esp_timer_get_time() - start) > micro_seconds) {
            return -1; // Timeout
        }
    }
    return esp_timer_get_time() - start;
}

esp_err_t dht22_read(float *temperature, float *humidity)
{
    uint8_t data[5] = {0};
    
    // 1. Send host start signal: pull low for at least 1-10ms (we use 18ms to be safe)
    gpio_set_direction(s_dht_pin, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(s_dht_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    
    // Release line and switch to input with open-drain (high due to external pull-up)
    gpio_set_level(s_dht_pin, 1);
    esp_rom_delay_us(40); // Wait for sensor to pull low
    
    gpio_set_direction(s_dht_pin, GPIO_MODE_INPUT);

    // Enter critical section to avoid FreeRTOS scheduler preemption messing up microsecond timing
    portMUX_TYPE myMutex = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&myMutex);

    // 2. DHT responds by pulling low for 80us, then releasing high for 80us
    if (wait_or_timeout(100, 0) < 0) {
        portEXIT_CRITICAL(&myMutex);
        ESP_LOGE(TAG, "Timeout waiting for sensor response low signal");
        return ESP_ERR_TIMEOUT;
    }
    if (wait_or_timeout(100, 1) < 0) {
        portEXIT_CRITICAL(&myMutex);
        ESP_LOGE(TAG, "Timeout waiting for sensor response high signal");
        return ESP_ERR_TIMEOUT;
    }

    // 3. Read 40 bits (5 bytes)
    for (int i = 0; i < 40; i++) {
        // Every bit starts with a 50us low representation
        if (wait_or_timeout(80, 0) < 0) {
            portEXIT_CRITICAL(&myMutex);
            ESP_LOGE(TAG, "Timeout waiting for bit %d start low signal", i);
            return ESP_ERR_TIMEOUT;
        }
        
        // Then it goes high. The length of the high period determines if it's a 0 (26-28us) or 1 (70us)
        int64_t len = wait_or_timeout(100, 1);
        if (len < 0) {
            portEXIT_CRITICAL(&myMutex);
            ESP_LOGE(TAG, "Timeout waiting for bit %d data high signal", i);
            return ESP_ERR_TIMEOUT;
        }
        
        // Shift bits in
        data[i / 8] <<= 1;
        if (len > 40) { // If high duration is greater than 40us, it's a '1'
            data[i / 8] |= 1;
        }
    }

    portEXIT_CRITICAL(&myMutex);

    // 4. Verify checksum: sum of first 4 bytes should equal the 5th byte
    uint8_t checksum = (data[0] + data[1] + data[2] + data[3]) & 0xFF;
    if (checksum != data[4]) {
        ESP_LOGE(TAG, "Checksum mismatch! Calc: 0x%02X, Got: 0x%02X", checksum, data[4]);
        return ESP_ERR_INVALID_CRC;
    }

    // 5. Parse values
    // Humidity: bytes 0 and 1 represent integral and decimal parts of humidity (DHT22: hum = (byte0 * 256 + byte1) / 10)
    float hum = ((float)((data[0] << 8) | data[1])) * 0.1f;
    
    // Temperature: bytes 2 and 3 represent temp (temp = (byte2 * 256 + byte3) / 10). If byte2's MSB is 1, temperature is negative.
    int16_t raw_temp = ((data[2] & 0x7F) << 8) | data[3];
    float temp = (float)raw_temp * 0.1f;
    if (data[2] & 0x80) {
        temp = -temp;
    }

    *humidity = hum;
    *temperature = temp;

    ESP_LOGD(TAG, "Read Temp: %.1f C, Hum: %.1f%%", (double)temp, (double)hum);
    return ESP_OK;
}
