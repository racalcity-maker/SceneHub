idf_component_register(
    SRCS "node_management.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_system node_config node_reset_button
)
