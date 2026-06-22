idf_component_register(
    SRCS "node_runtime_snapshot.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos node_common node_rule_api node_rule_compile node_fallback_runtime
)
