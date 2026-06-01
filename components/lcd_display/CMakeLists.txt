idf_component_register(
    SRCS "lcd_display.c"
    INCLUDE_DIRS "include"
    REQUIRES driver
    PRIV_REQUIRES freertos
)
