idf_component_register(
    SRCS
        "gm_game_profile.c"
    INCLUDE_DIRS "include"
    REQUIRES
        freertos
        json
        room_catalog
        room_scenario
        scenehub_scenario_validation
        sd_storage
)
