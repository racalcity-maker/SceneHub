idf_component_register(
    SRCS
        "room_catalog.c"
        "json/room_catalog_json.c"
        "storage/room_catalog_persistence.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES json sd_storage scenehub_config
)
