idf_component_register(
    SRCS "rs485_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES driver
    PRIV_REQUIRES freertos
)
