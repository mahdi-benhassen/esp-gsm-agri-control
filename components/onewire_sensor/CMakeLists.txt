idf_component_register(
    SRCS "onewire_sensor.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_timer
    PRIV_REQUIRES freertos
)
