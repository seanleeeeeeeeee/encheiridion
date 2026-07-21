idf_component_register(
    SRCS
        "s_navigator.cpp"
        "page_state.cpp"
        "scaife_client.cpp"
        "xml_parser.cpp"
    
    INCLUDE_DIRS
        "."
        ".."
    
    REQUIRES
        nvs_flash
        esp_http_client
        esp_wifi
        esp_event
        esp_netif
        esp_tls
        freertos
        log
        heap
)
