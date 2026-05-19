idf_component_register(
    SRCS "node_config.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash node_common
)
