idf_component_register(
    SRCS "device_control_ingest.c" "device_control_ingest_apply.c" "device_control_ingest_events.c"
    INCLUDE_DIRS "include"
    REQUIRES event_bus esp_timer quest_common quest_device
)
