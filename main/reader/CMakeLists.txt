idf_component_register(
    SRCS
        "r_navigator.cpp"
        "page_state.cpp"
        "webserver.cpp"
        "epub_parser.cpp"
    
    INCLUDE_DIRS
        "."
        ".."
    
    REQUIRES
	miniz
        nvs_flash
        esp_http_client
        esp_wifi
        esp_event
        esp_netif
        esp_tls
        freertos
        log
        heap
	fatfs
	spi_flash
	driver
)
