idf_component_register(
    SRCS
        "room_scenario.c"
        "room_scenario_json.c"
        "room_scenario_json_export.c"
        "room_scenario_json_import_reactive.c"
        "room_scenario_persistence.c"
        "room_scenario_types.c"
        "room_scenario_validation.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos json quest_device sd_storage
)
