idf_component_register(
    SRCS "eeprom_storage.c"
    INCLUDE_DIRS "include"
    REQUIRES driver
    PRIV_REQUIRES freertos
)
