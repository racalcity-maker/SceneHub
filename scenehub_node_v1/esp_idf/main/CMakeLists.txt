idf_component_register(
    SRCS "node_main.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash node_config node_runtime_mode node_capability node_board node_reset_button node_provisioning node_hardware_io node_control node_action_router node_mqtt_transport node_management node_driver_registry node_driver_nfc_reader node_event_router node_rule_compile node_rule_engine node_fallback_runtime
)
