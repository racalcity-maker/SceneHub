idf_component_register(
    SRCS "device_control_ingest.c"
    INCLUDE_DIRS "include"
    REQUIRES event_bus json esp_timer quest_common quest_device
)
