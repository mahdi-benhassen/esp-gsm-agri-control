idf_component_register(
    SRCS "command_handler.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_event json
    PRIV_REQUIRES relay_control config_store sensor_hub mqtt_client_wrapper freertos
)
