idf_component_register(
    SRCS "scenehub_scenario_validation.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos hardware_io json quest_device room_scenario scenehub_device_command_resolver
)
