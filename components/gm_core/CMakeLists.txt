idf_component_register(
    SRCS
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
    INCLUDE_DIRS "include"
    REQUIRES event_bus esp_timer quest_common room_catalog room_scenario scenehub_scenario_validation scenehub_events
)
