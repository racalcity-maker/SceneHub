set(TEST_SRCS
    "test_runner.c"
    "test_service_status.c"
    "test_event_bus.c"
    "test_config_store_utils.c"
    "test_audio_player_state.c"
    "test_command_executor.c"
    "test_ota_manager.c"
    "test_device_control_ingest.c"
    "test_quest_device.c"
    "test_room_catalog.c"
    "test_room_scenario.c"
    "test_gm_core_primitives.c"
    "test_gm_api.c"
    "test_gm_game_profile.c"
    "test_gm_room_session.c"
    "test_integration_quest_flow.c"
    "test_web_ui_contract.c"
    "test_web_ui_handlers.c"
    "test_orchestrator_registry.c"
    "test_orchestrator_audit.c"
    "test_orchestrator_timeline.c"
)

idf_component_register(
    SRCS ${TEST_SRCS}
    INCLUDE_DIRS "." "../../../components/web_ui" "../../../components/audio_player"
    PRIV_REQUIRES web_ui audio_player command_executor ota_manager gm_core gm_control room_catalog orchestrator_core scenehub_read_model event_bus service_status config_store device_control_ingest room_scenario quest_device scenehub_scenario_validation unity json
)

target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=unity_putc"
)
