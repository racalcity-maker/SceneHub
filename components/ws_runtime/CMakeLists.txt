idf_component_register(
    SRCS
        "ws_runtime.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        esp_http_server
        esp_timer
        json
)