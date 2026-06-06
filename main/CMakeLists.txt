idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_netif esp_event
    PRIV_REQUIRES config_store relay_control modem_manager sensor_hub mqtt_client_wrapper command_handler system_monitor app_logic digital_input analog_input eeprom_storage rtc_manager lcd_display rs485_manager sd_card_logger web_server
)
