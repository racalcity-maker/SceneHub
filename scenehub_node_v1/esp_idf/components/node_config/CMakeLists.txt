idf_component_register(
    SRCS "node_config.c" "node_config_migrations.c" "node_config_storage.c" "node_config_led_overlay.c" "node_config_defaults.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES nvs_flash node_common
)
