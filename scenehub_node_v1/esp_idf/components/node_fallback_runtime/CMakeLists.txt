idf_component_register(
    SRCS
        "node_fallback_runtime.c"
        "node_fallback_policy.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_timer node_rule_engine
)
