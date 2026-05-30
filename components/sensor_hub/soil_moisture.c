#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "sensor_hub.h"

static const char *TAG = "SOIL_MOISTURE";
static adc_oneshot_unit_handle_t s_adc_handle = NULL;

esp_err_t soil_moisture_init(void)
{
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
        .clk_src = 0,
    };
    
    esp_err_t err = adc_oneshot_new_unit(&init_cfg, &s_adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ADC unit: %s", esp_err_to_name(err));
        return err;
    }
    
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12, // 0 to 3.3V attenuation (12 dB) for ESP32 v5
        .bitwidth = ADC_BITWIDTH_12,
    };
    
    err = adc_oneshot_config_channel(s_adc_handle, CONFIG_SOIL_MOISTURE_ADC_CHANNEL, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to config ADC channel: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "Soil moisture ADC initialized on Channel %d", CONFIG_SOIL_MOISTURE_ADC_CHANNEL);
    return ESP_OK;
}

esp_err_t soil_moisture_read(float *moisture_pct)
{
    if (s_adc_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    int sum = 0;
    int raw_val = 0;
    
    for (int i = 0; i < CONFIG_SOIL_MOISTURE_SAMPLES; i++) {
        esp_err_t err = adc_oneshot_read(s_adc_handle, CONFIG_SOIL_MOISTURE_ADC_CHANNEL, &raw_val);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ADC Read failed: %s", esp_err_to_name(err));
            return err;
        }
        sum += raw_val;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    int average = sum / CONFIG_SOIL_MOISTURE_SAMPLES;
    
    // Map average from [WET, DRY] -> [100.0, 0.0]
    // dry_val corresponds to 0%, wet_val corresponds to 100%
    float pct = 100.0f - ((float)(average - CONFIG_SOIL_MOISTURE_WET_VALUE) * 100.0f / 
                (float)(CONFIG_SOIL_MOISTURE_DRY_VALUE - CONFIG_SOIL_MOISTURE_WET_VALUE));
    
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    
    *moisture_pct = pct;
    ESP_LOGD(TAG, "Raw ADC Avg: %d, Calc Moisture: %.1f%%", average, (double)pct);
    
    return ESP_OK;
}
