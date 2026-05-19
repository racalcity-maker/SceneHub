idf_component_register(
    SRCS "scenehub_device_command_resolver.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common freertos json quest_common quest_device
)
