idf_component_register(
    SRCS "modem_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_modem esp_netif esp_event driver
    PRIV_REQUIRES freertos
)
