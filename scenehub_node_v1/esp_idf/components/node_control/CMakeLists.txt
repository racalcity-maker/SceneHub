idf_component_register(
    SRCS "node_control.c"
    INCLUDE_DIRS "include"
    REQUIRES node_common node_config node_capability node_hardware_io
)
