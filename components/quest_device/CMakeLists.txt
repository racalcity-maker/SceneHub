idf_component_register(
    SRCS "quest_device.c" "quest_device_json.c" "quest_device_storage.c" "quest_device_system.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES freertos json quest_common sd_storage
)
