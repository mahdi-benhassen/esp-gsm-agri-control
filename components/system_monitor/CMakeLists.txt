idf_component_register(
    SRCS "system_monitor.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer esp_event esp_system cJSON
    PRIV_REQUIRES freertos modem_manager mqtt_client_wrapper
)
