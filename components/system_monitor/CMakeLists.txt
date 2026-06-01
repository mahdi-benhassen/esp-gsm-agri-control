idf_component_register(
    SRCS "system_monitor.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer esp_event esp_system json
    PRIV_REQUIRES freertos modem_manager mqtt_client_wrapper rtc_manager sd_card_logger
)
