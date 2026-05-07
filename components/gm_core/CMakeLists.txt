idf_component_register(
    SRCS
        "gm_api.c"
        "gm_timer.c"
        "gm_hint.c"
        "gm_room_session.c"
        "gm_room_session_commands.c"
        "gm_room_session_events.c"
        "gm_room_session_game.c"
        "gm_room_session_reactive_v2.c"
        "gm_room_session_runtime.c"
        "gm_room_session_runtime_wait.c"
        "gm_game_profile.c"
    INCLUDE_DIRS "include"
    REQUIRES audio_player command_executor event_bus esp_timer hardware_io mqtt_core quest_common quest_device room_catalog room_scenario json sd_storage
)
