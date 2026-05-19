idf_component_register(
    SRCS "node_reset_button.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_timer node_common node_config
)
