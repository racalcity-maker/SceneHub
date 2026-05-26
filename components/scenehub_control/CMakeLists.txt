idf_component_register(
    SRCS "scenehub_control_dispatch.c" "scenehub_control.c" "scenehub_control_rooms.c" "scenehub_control_devices.c" "scenehub_control_scenarios.c" "scenehub_control_profiles.c" "scenehub_control_sidebar_presets.c" "scenehub_control_hardware_io.c"
    INCLUDE_DIRS "include"
    REQUIRES
        command_executor
        device_control_ingest
        gm_core
        hardware_io
        json
        mqtt_core
        orchestrator_core
        quest_common
        quest_device
        room_catalog
        room_scenario
        scenehub_read_model
        scenehub_device_command_resolver
        scenehub_scenario_validation
        scenehub_state
)
