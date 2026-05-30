idf_component_register(
    SRCS "app_logic.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_event esp_timer
    PRIV_REQUIRES freertos sensor_hub relay_control mqtt_client_wrapper config_store system_monitor
)
