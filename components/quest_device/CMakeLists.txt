idf_component_register(
    SRCS "quest_device.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos json quest_common sd_storage
)
