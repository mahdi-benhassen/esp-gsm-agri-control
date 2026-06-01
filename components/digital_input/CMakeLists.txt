idf_component_register(
    SRCS "digital_input.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_timer esp_event
    PRIV_REQUIRES freertos config_store
)
