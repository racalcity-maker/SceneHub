idf_component_register(
    SRCS
        "node_hardware_io.c"
        "relay/node_hw_relay.c"
        "mosfet/node_hw_mosfet.c"
        "universal_io/node_hw_universal_io.c"
        "led/node_hw_led.c"
        "led/node_hw_led_driver.c"
        "led/node_hw_led_worker.c"
        "led/node_hw_led_effects_basic.c"
        "led/node_hw_led_effects_motion.c"
        "led/node_hw_led_effects_noise.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES driver esp_timer node_config node_board led_strip
)
