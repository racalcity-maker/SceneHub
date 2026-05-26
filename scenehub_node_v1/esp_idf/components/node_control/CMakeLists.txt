idf_component_register(
    SRCS "node_control.c" "node_control_json.c" "node_control_led.c" "node_control_output.c" "node_control_mosfet.c" "node_control_describe.c"
    INCLUDE_DIRS "include"
    REQUIRES node_common node_config node_capability node_hardware_io
)
