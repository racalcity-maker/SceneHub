idf_component_register(
    SRCS "node_driver_nfc_contract.c"
    INCLUDE_DIRS "include"
    REQUIRES node_common node_board node_driver_registry
)
