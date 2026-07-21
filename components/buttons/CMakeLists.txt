idf_component_register(
    SRCS
        "buttons.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        driver
        esp_timer
        freertos
        log
)

