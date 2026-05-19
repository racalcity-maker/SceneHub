idf_component_register(
    SRCS
        "node_hardware_io.c"
        "relay/node_hw_relay.c"
        "mosfet/node_hw_mosfet.c"
        "universal_io/node_hw_universal_io.c"
        "led/node_hw_led.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES driver node_config node_board
)
