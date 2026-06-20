idf_component_register(
    SRCS "factory_reset.c"
    INCLUDE_DIRS "include"
    REQUIRES driver nvs_flash
    PRIV_REQUIRES config_store freertos log
)
