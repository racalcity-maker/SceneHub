set(TEST_SRCS
    "test_runner.c"
    "test_device_control_ingest.c"
    "test_quest_device.c"
    "test_room_scenario.c"
    "test_gm_game_profile.c"
)

idf_component_register(
    SRCS ${TEST_SRCS}
    INCLUDE_DIRS "."
    PRIV_REQUIRES gm_core gm_control room_catalog orchestrator_core event_bus device_control_ingest room_scenario quest_device unity json
)
