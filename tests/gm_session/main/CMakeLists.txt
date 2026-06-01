set(TEST_SHARED ${CMAKE_CURRENT_LIST_DIR}/../../shared)

set(TEST_SRCS
    "${TEST_SHARED}/scenehub_unity_runner.c"
    "test_suite_gm_session.c"
    "test_gm_core_primitives.c"
    "test_gm_room_session.c"
    "test_gm_room_session_runtime_chaos.c"
    "test_integration_quest_flow.c"
)

idf_component_register(
    SRCS ${TEST_SRCS}
    INCLUDE_DIRS "." "${TEST_SHARED}" "../../../components/web_ui" "../../../components/audio_player" "../../../components/command_executor" "../../../components/device_control_ingest" "../../../components/gm_core/session"
    PRIV_REQUIRES web_ui audio_player command_executor ota_manager gm_core gm_profile_store room_catalog orchestrator_core scenehub_control scenehub_read_model event_bus service_status config_store device_control_ingest room_scenario quest_device scenehub_scenario_validation unity json
)

target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=unity_putc"
)
