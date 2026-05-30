idf_component_register(
    SRCS "mqtt_client_wrapper.c"
    INCLUDE_DIRS "include"
    REQUIRES mqtt esp_event json
    PRIV_REQUIRES freertos config_store sensor_hub relay_control
)
