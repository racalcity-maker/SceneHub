idf_component_register(
    SRCS "node_capability.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos node_common node_config node_runtime_mode node_driver_nfc_config_api node_hardware_io node_runtime_snapshot
)
