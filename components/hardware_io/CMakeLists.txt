idf_component_register(
    SRCS
        "hardware_io.c"
        "hardware_io_relay.c"
        "hardware_io_mosfet.c"
        "hardware_io_input.c"
        "hardware_io_gpio.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_timer event_bus freertos
)
