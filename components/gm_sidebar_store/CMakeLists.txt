idf_component_register(
    SRCS
        "gm_sidebar_presets.c"
        "gm_sidebar_presets_json.c"
        "gm_sidebar_presets_storage.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES
        freertos
        json
        quest_common
        quest_device
        room_scenario
        scenehub_device_command_resolver
        sd_storage
)
