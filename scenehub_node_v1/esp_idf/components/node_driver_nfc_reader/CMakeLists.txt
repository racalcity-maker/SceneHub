idf_component_register(
    SRCS
        "node_driver_nfc_reader.c"
        "node_driver_nfc_reader_factory.c"
        "node_driver_nfc_reader_overlay.c"
        "node_driver_nfc_reader_runtime.c"
        "node_driver_pn532_i2c.c"
    INCLUDE_DIRS "include"
    REQUIRES driver nvs_flash node_common node_driver_registry node_board node_config node_rule_engine
)
