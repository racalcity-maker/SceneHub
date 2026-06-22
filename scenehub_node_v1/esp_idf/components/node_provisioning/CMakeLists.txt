idf_component_register(
    SRCS
        "node_provisioning.c"
        "node_provisioning_http.c"
        "node_provisioning_ui.c"
        "node_provisioning_body.c"
        "node_provisioning_admin_json.c"
        "node_provisioning_admin_api.c"
        "node_provisioning_config_api.c"
        "node_provisioning_status_api.c"
        "node_provisioning_rules_api.c"
        "node_provisioning_rules_context_api.c"
        "node_provisioning_nfc_api.c"
        "node_provisioning_led_api.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_netif esp_event esp_http_server esp_system node_config node_runtime_mode node_common node_board node_control node_admin_control node_rule_api node_driver_nfc_api node_driver_nfc_config_api node_runtime_snapshot json
)
