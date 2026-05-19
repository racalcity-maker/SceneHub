set(BACKEND_CORE_MAIN ${CMAKE_CURRENT_LIST_DIR}/../../backend_core/main)
set(COMMAND_BACKEND_MAIN ${CMAKE_CURRENT_LIST_DIR}/../../command_backend/main)
set(GM_SESSION_MAIN ${CMAKE_CURRENT_LIST_DIR}/../../gm_session/main)
set(WEB_UI_BACKEND_MAIN ${CMAKE_CURRENT_LIST_DIR}/../../web_ui_backend/main)

set(TEST_SRCS
    "test_runner.c"
    "${BACKEND_CORE_MAIN}/test_service_status.c"
    "${BACKEND_CORE_MAIN}/test_event_bus.c"
    "${BACKEND_CORE_MAIN}/test_config_store_utils.c"
    "${BACKEND_CORE_MAIN}/test_audio_player_state.c"
    "${COMMAND_BACKEND_MAIN}/test_command_executor.c"
    "${BACKEND_CORE_MAIN}/test_ota_manager.c"
    "${COMMAND_BACKEND_MAIN}/test_device_control_ingest.c"
    "${BACKEND_CORE_MAIN}/test_quest_device.c"
    "${BACKEND_CORE_MAIN}/test_room_catalog.c"
    "${BACKEND_CORE_MAIN}/test_room_scenario.c"
    "${GM_SESSION_MAIN}/test_gm_core_primitives.c"
    "${GM_SESSION_MAIN}/test_gm_api.c"
    "${BACKEND_CORE_MAIN}/test_gm_game_profile.c"
    "${GM_SESSION_MAIN}/test_gm_room_session.c"
    "${GM_SESSION_MAIN}/test_integration_quest_flow.c"
    "${WEB_UI_BACKEND_MAIN}/test_web_ui_contract.c"
    "${WEB_UI_BACKEND_MAIN}/test_web_ui_handlers.c"
    "${WEB_UI_BACKEND_MAIN}/test_orchestrator_registry.c"
    "${WEB_UI_BACKEND_MAIN}/test_orchestrator_audit.c"
    "${WEB_UI_BACKEND_MAIN}/test_orchestrator_timeline.c"
)

idf_component_register(
    SRCS ${TEST_SRCS}
    INCLUDE_DIRS "." "${BACKEND_CORE_MAIN}" "${COMMAND_BACKEND_MAIN}" "${GM_SESSION_MAIN}" "${WEB_UI_BACKEND_MAIN}" "../../../components/web_ui" "../../../components/audio_player" "../../../components/command_executor" "../../../components/device_control_ingest"
    PRIV_REQUIRES web_ui audio_player command_executor ota_manager gm_core room_catalog orchestrator_core scenehub_read_model event_bus service_status config_store device_control_ingest room_scenario quest_device scenehub_scenario_validation unity json
)

target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=unity_putc"
)
