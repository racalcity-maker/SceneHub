idf_component_register(
    SRCS "node_rule_api.c"
    INCLUDE_DIRS "include"
    REQUIRES node_common node_config node_rule_compile node_rule_store node_rule_engine
)
