idf_component_register(
    SRCS
        "api/gm_api.c"
        "control/gm_control.c"
        "timer/gm_timer.c"
        "hint/gm_hint.c"
        "session/gm_room_session.c"
        "session/gm_room_session_commands.c"
        "session/gm_room_session_events.c"
        "session/gm_room_session_game.c"
        "session/gm_room_session_reactive_v2.c"
        "session/gm_room_session_runtime.c"
        "session/gm_room_session_runtime_wait.c"
        "session/gm_room_session_view.c"
        "profile/gm_game_profile.c"
        "sidebar/gm_sidebar_presets.c"
        "sidebar/gm_sidebar_presets_json.c"
        "sidebar/gm_sidebar_presets_storage.c"
    INCLUDE_DIRS "include"
    REQUIRES audio_player command_executor event_bus esp_timer hardware_io mqtt_core orchestrator_core quest_common quest_device room_catalog room_scenario json scenehub_device_command_resolver scenehub_scenario_validation sd_storage scenehub_events service_status
)
