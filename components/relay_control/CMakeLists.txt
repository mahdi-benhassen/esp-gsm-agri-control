idf_component_register(
    SRCS "relay_control.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_log esp_timer esp_event
    PRIV_REQUIRES freertos
)
