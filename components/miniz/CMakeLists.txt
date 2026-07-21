idf_component_register(
    SRCS
        "miniz.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        driver
        esp_timer
        freertos
        log
)

