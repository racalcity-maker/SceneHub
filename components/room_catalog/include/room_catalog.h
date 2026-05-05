#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ROOM_CATALOG_MAX_ROOMS 4
#define ROOM_CATALOG_ROOM_ID_MAX_LEN 32
#define ROOM_CATALOG_ROOM_NAME_MAX_LEN 64
#define ROOM_CATALOG_STORAGE_PATH "/sdcard/quest/rooms.json"

typedef struct {
    char room_id[ROOM_CATALOG_ROOM_ID_MAX_LEN];
    char name[ROOM_CATALOG_ROOM_NAME_MAX_LEN];
    uint16_t device_count;
} room_catalog_entry_t;

esp_err_t room_catalog_init(void);
esp_err_t room_catalog_refresh(void);

esp_err_t room_catalog_upsert(const room_catalog_entry_t *entry);
esp_err_t room_catalog_upsert_and_save(const room_catalog_entry_t *entry);
esp_err_t room_catalog_delete(const char *room_id);
esp_err_t room_catalog_delete_and_save(const char *room_id);
esp_err_t room_catalog_clear(void);
uint32_t room_catalog_generation(void);

bool room_catalog_exists(const char *room_id);
size_t room_catalog_count(void);
esp_err_t room_catalog_get(size_t index, room_catalog_entry_t *out);
esp_err_t room_catalog_find(const char *room_id, room_catalog_entry_t *out);

esp_err_t room_catalog_export_json(cJSON **out);
esp_err_t room_catalog_import_json(const cJSON *root);
esp_err_t room_catalog_import_json_and_save(const cJSON *root);
esp_err_t room_catalog_save(void);
esp_err_t room_catalog_load(void);
esp_err_t room_catalog_save_to_path(const char *path);
esp_err_t room_catalog_load_from_path(const char *path);

#ifdef __cplusplus
}
#endif
