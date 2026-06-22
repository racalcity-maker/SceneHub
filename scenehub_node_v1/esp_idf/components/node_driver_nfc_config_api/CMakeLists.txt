idf_component_register(
    SRCS "node_driver_nfc_config_api.c"
    INCLUDE_DIRS "include"
    REQUIRES node_config node_driver_nfc_reader node_driver_nfc_contract
)
