idf_component_register(
    SRCS "scenehub_control.c" "scenehub_control_rooms.c" "scenehub_control_devices.c" "scenehub_control_scenarios.c" "scenehub_control_profiles.c"
    INCLUDE_DIRS "include"
    REQUIRES
        device_control_ingest
        gm_control
        gm_core
        json
        mqtt_core
        orchestrator_core
        quest_common
        quest_device
        room_catalog
        room_scenario
        scenehub_scenario_validation
        scenehub_state
)
