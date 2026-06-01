idf_component_register(
    SRCS "sensor_hub.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer esp_event
    PRIV_REQUIRES freertos config_store onewire_sensor
)
