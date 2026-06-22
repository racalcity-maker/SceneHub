idf_component_register(
    SRCS "node_action_router.c"
    INCLUDE_DIRS "include"
    REQUIRES node_common node_control node_mqtt_transport node_rule_action_port
)
