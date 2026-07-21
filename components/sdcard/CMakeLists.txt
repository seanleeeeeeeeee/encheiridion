idf_component_register(
    SRCS
        "sdcard.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        driver
        esp_timer
        freertos
        log
)

