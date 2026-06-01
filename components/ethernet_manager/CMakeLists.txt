idf_component_register(
    SRCS "ethernet_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_eth esp_netif esp_event driver
    PRIV_REQUIRES freertos
)
