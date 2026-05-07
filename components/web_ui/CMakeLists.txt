set(GM_PANEL_PARTS
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/gm_panel/gm_panel_00_base.js
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/gm_panel/gm_panel_00_ui.js
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/gm_panel/gm_panel_00_api.js
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/gm_panel/gm_panel_00_form.js
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/gm_panel/gm_panel_00_actions.js
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/gm_panel/gm_panel_01_state_helpers.js
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/gm_panel/gm_panel_01a_scenario_runtime.js
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/gm_panel/gm_panel_01b_editor_dirty.js
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/gm_panel/gm_panel_01c_quest_device_status.js
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/gm_panel/gm_panel_01d_generic_helpers.js
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/gm_panel/gm_panel_02_room_rendering.js
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/gm_panel/gm_panel_03_main_views.js
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/gm_panel/gm_panel_04_hardware_io_view.js
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/gm_panel/gm_panel_04_profiles_view.js
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/gm_panel/gm_panel_05a_scenario_model.js
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/gm_panel/gm_panel_05b_scenario_payloads.js
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/gm_panel/gm_panel_05c_reactive_v2.js
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/gm_panel/gm_panel_05d_scenario_branches.js
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/gm_panel/gm_panel_05e_scenario_steps.js
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/gm_panel/gm_panel_05f_scenario_validation.js
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/gm_panel/gm_panel_05_scenario_builder.js
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/gm_panel/gm_panel_06_storage_and_render.js
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/gm_panel/gm_panel_07_loaders_and_runtime_actions.js
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/gm_panel/gm_panel_08_editor_actions.js
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/gm_panel/gm_panel_09_scenario_change_events.js
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/gm_panel/gm_panel_09_editor_events.js
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/gm_panel/gm_panel_09_events_boot.js
)

set(GENERATED_GM_PANEL_JS ${CMAKE_CURRENT_SOURCE_DIR}/assets/gm_panel.js)

if(CMAKE_SCRIPT_MODE_FILE)
    if(NOT EXISTS ${GENERATED_GM_PANEL_JS})
        file(WRITE ${GENERATED_GM_PANEL_JS} "")
    endif()
else()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_custom_command(
        OUTPUT ${GENERATED_GM_PANEL_JS}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/assets/build_gm_panel.py
                ${GENERATED_GM_PANEL_JS} ${GM_PANEL_PARTS}
        DEPENDS ${GM_PANEL_PARTS} ${CMAKE_CURRENT_SOURCE_DIR}/assets/build_gm_panel.py
        COMMENT "Bundling gm_panel.js"
    )
    add_custom_target(gm_panel_bundle ALL DEPENDS ${GENERATED_GM_PANEL_JS})
endif()

idf_component_register(
    SRCS
        "web_ui_utils.c"
        "web_ui_devices.c"
        "web_ui_page.c"
        "web_ui_auth.c"
        "web_ui_status.c"
        "web_ui_ota.c"
        "web_ui_media.c"
        "web_ui_system.c"
        "web_ui_gm.c"
        "web_ui_gm_action.c"
        "web_ui_gm_profile.c"
        "web_ui_gm_quest_device.c"
        "web_ui_gm_scenario.c"
        "web_ui_gm_scenario_catalog.c"
        "web_ui_gm_scenario_store.c"
        "orchestrator_api_view.c"
        "orchestrator_api_view_activity.c"
        "orchestrator_api_view_control_devices.c"
        "orchestrator_api_view_room_scenarios.c"
        "web_ui_orchestrator.c"
        "web_ui_gm_timer.c"
        "web_ui_gm_hint.c"
        "web_ui_hardware_io.c"
        "web_ui.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    EMBED_TXTFILES
        "assets/gm_panel.css"
        ${GENERATED_GM_PANEL_JS}
    REQUIRES esp_http_server event_bus config_store network audio_player mqtt_core ota_manager sd_storage service_status device_control_ingest orchestrator_core gm_core gm_control quest_common room_catalog room_scenario quest_device hardware_io
)

if(TARGET gm_panel_bundle)
    add_dependencies(${COMPONENT_LIB} gm_panel_bundle)
endif()
