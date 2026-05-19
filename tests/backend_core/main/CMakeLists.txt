set(TEST_SHARED ${CMAKE_CURRENT_LIST_DIR}/../../shared)

set(TEST_SRCS
    "${TEST_SHARED}/scenehub_unity_runner.c"
    "test_suite_backend_core.c"
    "test_service_status.c"
    "test_event_bus.c"
    "test_config_store_utils.c"
    "test_audio_player_state.c"
    "test_ota_manager.c"
    "test_quest_device.c"
    "test_room_catalog.c"
    "test_room_scenario.c"
    "test_gm_game_profile.c"
)

idf_component_register(
    SRCS ${TEST_SRCS}
    INCLUDE_DIRS "." "${TEST_SHARED}" "../../../components/web_ui" "../../../components/audio_player" "../../../components/command_executor" "../../../components/device_control_ingest"
    PRIV_REQUIRES web_ui audio_player command_executor ota_manager gm_core room_catalog orchestrator_core scenehub_read_model event_bus service_status config_store device_control_ingest room_scenario quest_device scenehub_scenario_validation unity json
)

target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=unity_putc"
)
