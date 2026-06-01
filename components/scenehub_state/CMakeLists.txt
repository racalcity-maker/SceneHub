idf_component_register(
    SRCS "scenehub_state.c"
    INCLUDE_DIRS "include"
    REQUIRES
        device_control_ingest
        freertos
        gm_core
        gm_profile_store
        gm_sidebar_store
        quest_device
        room_catalog
        room_scenario
        scenehub_read_model
        ws_runtime
)
