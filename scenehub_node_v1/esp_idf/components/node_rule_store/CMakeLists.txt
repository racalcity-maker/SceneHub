idf_component_register(
    SRCS "node_rule_store.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash spiffs node_common
)
