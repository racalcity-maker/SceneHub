idf_component_register(
    SRCS
        "hardware_io.c"
        "hardware_io_relay.c"
        "hardware_io_mosfet.c"
        "hardware_io_mosfet_effects.c"
        "hardware_io_io.c"
        "hardware_io_io_runtime.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES driver esp_timer event_bus freertos scenehub_config
    PRIV_REQUIRES quest_device
)
