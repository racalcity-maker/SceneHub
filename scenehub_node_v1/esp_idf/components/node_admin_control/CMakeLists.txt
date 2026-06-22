idf_component_register(
    SRCS
        "node_admin_control.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_system node_common node_config node_control node_board node_rule_api node_rule_store node_driver_nfc_contract node_driver_nfc_api node_driver_nfc_config_api
)
