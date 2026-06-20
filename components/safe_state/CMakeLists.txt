idf_component_register(
    SRCS "safe_state.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer freertos
    PRIV_REQUIRES log
)
