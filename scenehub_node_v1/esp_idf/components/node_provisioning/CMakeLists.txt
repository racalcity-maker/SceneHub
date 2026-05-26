idf_component_register(
    SRCS
        "node_provisioning.c"
        "node_provisioning_http.c"
        "node_provisioning_ui.c"
        "node_provisioning_config_api.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_netif esp_event esp_http_server esp_system node_config node_common node_board node_control node_admin_control
)
