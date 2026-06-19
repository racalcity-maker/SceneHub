idf_component_register(
    SRCS "node_fallback_runtime.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer node_rule_engine
)
