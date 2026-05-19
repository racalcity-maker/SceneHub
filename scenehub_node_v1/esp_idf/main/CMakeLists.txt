idf_component_register(
    SRCS "node_main.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash node_config node_capability node_board node_reset_button node_provisioning node_hardware_io node_control node_mqtt_transport node_management
)
