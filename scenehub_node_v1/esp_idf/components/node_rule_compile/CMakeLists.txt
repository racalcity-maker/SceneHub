idf_component_register(
    SRCS "node_rule_compile.c"
    INCLUDE_DIRS "include"
    REQUIRES node_common node_config node_rule_schema node_rule_store node_runtime_mode json
)
