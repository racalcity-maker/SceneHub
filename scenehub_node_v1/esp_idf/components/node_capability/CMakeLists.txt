idf_component_register(
    SRCS "node_capability.c"
    INCLUDE_DIRS "include"
    REQUIRES node_common node_config node_runtime_mode node_rule_compile node_rule_engine node_driver_nfc_reader
)
