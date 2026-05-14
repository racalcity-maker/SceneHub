idf_component_register(
    SRCS "scenehub_state.c"
    INCLUDE_DIRS "include"
    REQUIRES
        device_control_ingest
        freertos
        gm_core
        quest_device
        room_catalog
        room_scenario
        scenehub_read_model
        ws_runtime
)
