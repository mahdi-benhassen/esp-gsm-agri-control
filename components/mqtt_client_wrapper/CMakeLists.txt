idf_component_register(
    SRCS "mqtt_client_wrapper.c"
    INCLUDE_DIRS "include"
    REQUIRES mqtt esp_log esp_event cJSON
    PRIV_REQUIRES freertos config_store sensor_hub relay_control
)
