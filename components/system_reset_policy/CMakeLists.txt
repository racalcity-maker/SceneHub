idf_component_register(
    SRCS "system_reset_policy.c"
    INCLUDE_DIRS "include"
    REQUIRES config_store scenehub_config
    PRIV_REQUIRES esp_driver_gpio
)
