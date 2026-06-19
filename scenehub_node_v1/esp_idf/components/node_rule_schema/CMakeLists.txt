idf_component_register(
    SRCS "node_rule_schema.c"
    INCLUDE_DIRS "include"
    REQUIRES node_common node_runtime_mode node_config node_driver_nfc_reader json
)
