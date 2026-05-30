idf_component_register(
    SRCS "sensor_hub.c" "soil_moisture.c" "dht22.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_adc esp_log esp_timer driver esp_event
    PRIV_REQUIRES freertos config_store
)
