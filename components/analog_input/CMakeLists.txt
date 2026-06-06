idf_component_register(
    SRCS "analog_input.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_adc
    PRIV_REQUIRES freertos
)
