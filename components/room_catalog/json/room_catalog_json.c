#include "room_catalog_internal.h"

#include <string.h>

#include "esp_heap_caps.h"

static esp_err_t room_catalog_json_copy_string(const cJSON *json,
                                               const char *name,
                                               char *dst,
                                               size_t dst_len,
                                               bool required)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(json, name);
    if (!item || cJSON_IsNull(item)) {
        if (required) {
            return ESP_ERR_INVALID_ARG;
        }
        dst[0] = '\0';
        return ESP_OK;
    }
    if (!cJSON_IsString(item) || !item->valuestring ||
        (required && !item->valuestring[0]) ||
        strlen(item->valuestring) >= dst_len) {
        return ESP_ERR_INVALID_ARG;
    }
    room_catalog_copy(dst, dst_len, item->valuestring);
    return ESP_OK;
}

static esp_err_t room_catalog_entry_to_json(const room_catalog_entry_t *entry, cJSON *array)
{
    cJSON *obj = cJSON_CreateObject();
    if (!obj) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(obj, "room_id", entry->room_id);
    cJSON_AddStringToObject(obj, "name", entry->name);
    if (!cJSON_AddItemToArray(array, obj)) {
        cJSON_Delete(obj);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t room_catalog_entry_from_json(const cJSON *json, room_catalog_entry_t *out)
{
    esp_err_t err = ESP_OK;
    if (!cJSON_IsObject(json) || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    err = room_catalog_json_copy_string(json, "room_id", out->room_id, sizeof(out->room_id), false);
    if (err != ESP_OK) {
        return err;
    }
    if (!out->room_id[0]) {
        err = room_catalog_json_copy_string(json, "id", out->room_id, sizeof(out->room_id), true);
        if (err != ESP_OK) {
            return err;
        }
    }
    err = room_catalog_json_copy_string(json, "name", out->name, sizeof(out->name), false);
    if (err != ESP_OK) {
        return err;
    }
    if (!out->name[0]) {
        room_catalog_copy(out->name, sizeof(out->name), out->room_id);
    }
    return room_catalog_entry_valid(out) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

static bool room_catalog_has_duplicate_ids(const room_catalog_entry_t *items, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        for (size_t j = i + 1; j < count; ++j) {
            if (strcmp(items[i].room_id, items[j].room_id) == 0) {
                return true;
            }
        }
    }
    return false;
}

esp_err_t room_catalog_export_json(cJSON **out)
{
    cJSON *root = NULL;
    cJSON *rooms = NULL;
    esp_err_t err = ESP_OK;
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddNumberToObject(root, "version", ROOM_CATALOG_JSON_VERSION);
    rooms = cJSON_AddArrayToObject(root, "rooms");
    if (!rooms) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    err = room_catalog_lock();
    if (err != ESP_OK) {
        cJSON_Delete(root);
        return err;
    }
    for (size_t i = 0; i < room_catalog_count_locked(); ++i) {
        err = room_catalog_entry_to_json(&room_catalog_entries_locked()[i], rooms);
        if (err != ESP_OK) {
            room_catalog_unlock();
            cJSON_Delete(root);
            return err;
        }
    }
    room_catalog_unlock();
    *out = root;
    return ESP_OK;
}

esp_err_t room_catalog_import_json(const cJSON *root)
{
    const cJSON *version = NULL;
    const cJSON *array = NULL;
    room_catalog_entry_t *items = NULL;
    int count = 0;
    esp_err_t err = ESP_OK;
    if (!cJSON_IsObject(root)) {
        return ESP_ERR_INVALID_ARG;
    }
    version = cJSON_GetObjectItemCaseSensitive(root, "version");
    if (!cJSON_IsNumber(version) || version->valueint != ROOM_CATALOG_JSON_VERSION) {
        return ESP_ERR_INVALID_ARG;
    }
    array = cJSON_GetObjectItemCaseSensitive(root, "rooms");
    if (!cJSON_IsArray(array)) {
        return ESP_ERR_INVALID_ARG;
    }
    count = cJSON_GetArraySize(array);
    if (count < 0 || count > ROOM_CATALOG_MAX_ROOMS) {
        return ESP_ERR_INVALID_ARG;
    }
    items = room_catalog_alloc_entries((size_t)count);
    if (count > 0 && !items) {
        return ESP_ERR_NO_MEM;
    }
    for (int i = 0; i < count; ++i) {
        err = room_catalog_entry_from_json(cJSON_GetArrayItem(array, i), &items[i]);
        if (err != ESP_OK) {
            heap_caps_free(items);
            return err;
        }
    }
    if (count > 1 && room_catalog_has_duplicate_ids(items, (size_t)count)) {
        heap_caps_free(items);
        return ESP_ERR_INVALID_ARG;
    }
    err = room_catalog_lock();
    if (err != ESP_OK) {
        heap_caps_free(items);
        return err;
    }
    room_catalog_replace_all_locked(items, (size_t)count);
    room_catalog_unlock();
    heap_caps_free(items);
    return ESP_OK;
}
