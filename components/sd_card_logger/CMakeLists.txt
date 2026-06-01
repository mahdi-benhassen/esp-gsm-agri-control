idf_component_register(
    SRCS "sd_card_logger.c"
    INCLUDE_DIRS "include"
    REQUIRES driver vfs fatfs
    PRIV_REQUIRES rtc_manager
)
