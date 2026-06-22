idf_component_register(
    SRCS
        "node_mqtt_transport.c"
        "node_mqtt_json.c"
        "node_mqtt_publish.c"
        "node_mqtt_duplicate.c"
        "node_mqtt_command.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES mqtt esp_timer node_common node_config node_control node_protocol node_admin_control node_runtime_snapshot node_runtime_mode node_fallback_runtime node_driver_nfc_api node_capability
)
