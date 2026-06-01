idf_component_register(
    SRCS "mqtt_client_wrapper.c"
    INCLUDE_DIRS "include"
    REQUIRES mqtt esp_event json sensor_hub
    PRIV_REQUIRES freertos config_store
)
