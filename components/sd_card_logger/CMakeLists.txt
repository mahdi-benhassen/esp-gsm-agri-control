idf_component_register(
    SRCS "sd_card_logger.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_sdmmc esp_driver_spi vfs fatfs
    PRIV_REQUIRES rtc_manager
)
