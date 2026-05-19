idf_component_register(
    SRCS
        "room_scenario.c"
        "json/room_scenario_json.c"
        "json/room_scenario_json_export.c"
        "json/room_scenario_json_import_reactive.c"
        "storage/room_scenario_persistence.c"
        "room_scenario_device_refs.c"
        "room_scenario_types.c"
        "room_scenario_validation.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos json
    PRIV_REQUIRES sd_storage
)
