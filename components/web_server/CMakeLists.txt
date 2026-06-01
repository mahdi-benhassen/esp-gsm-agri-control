idf_component_register(
    SRCS "web_server.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server esp_wifi esp_netif esp_event json nvs_flash
    PRIV_REQUIRES freertos relay_control sensor_hub digital_input modem_manager mqtt_client_wrapper config_store system_monitor rtc_manager sd_card_logger
)
