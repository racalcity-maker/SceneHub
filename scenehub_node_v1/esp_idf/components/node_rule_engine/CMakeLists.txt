idf_component_register(
    SRCS
        "node_rule_engine.c"
    INCLUDE_DIRS "include"
    REQUIRES node_action_router node_common node_config node_event_router node_hardware_io node_mqtt_transport node_rule_compile node_runtime_mode
)
