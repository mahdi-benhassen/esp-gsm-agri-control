idf_component_register(
    SRCS "sd_card_logger.c"
    INCLUDE_DIRS "include"
    REQUIRES driver sdmmc_cmd vfs fatfs
    PRIV_REQUIRES rtc_manager
)
