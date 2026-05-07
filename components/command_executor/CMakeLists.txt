idf_component_register(
    SRCS
        "command_executor.c"
        "command_executor_audio.c"
        "command_executor_hardware.c"
        "command_executor_mqtt.c"
        "command_executor_result.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES audio_player esp_timer event_bus freertos hardware_io mqtt_core quest_common quest_device room_scenario json
)
