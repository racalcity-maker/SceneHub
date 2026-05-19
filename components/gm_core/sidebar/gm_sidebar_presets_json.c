#include "gm_sidebar_presets_internal.h"

#include <string.h>

#include "esp_heap_caps.h"

static const cJSON *gm_sidebar_preset_json_array(const cJSON *root)
{
    const cJSON *items = NULL;
    if (cJSON_IsArray(root)) {
        return root;
    }
    if (!cJSON_IsObject(root)) {
        return NULL;
    }
    items = cJSON_GetObjectItemCaseSensitive(root, "gm_sidebar_presets");
    if (cJSON_IsArray(items)) {
        return items;
    }
    items = cJSON_GetObjectItemCaseSensitive(root, "presets");
    if (cJSON_IsArray(items)) {
        return items;
    }
    return NULL;
}

static const char *gm_sidebar_preset_json_string(const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : "";
}

static esp_err_t gm_sidebar_preset_json_add_raw_object(cJSON *obj,
                                                       const char *key,
                                                       const char *json_text)
{
    cJSON *parsed = NULL;
    if (!obj || !key || !key[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!json_text || !json_text[0]) {
        return ESP_OK;
    }
    parsed = cJSON_Parse(json_text);
    if (!cJSON_IsObject(parsed)) {
        cJSON_Delete(parsed);
        return ESP_ERR_INVALID_ARG;
    }
    cJSON_AddItemToObject(obj, key, parsed);
    return ESP_OK;
}

static esp_err_t gm_sidebar_preset_parse_raw_object_string(const cJSON *obj,
                                                           const char *key,
                                                           char *out,
                                                           size_t out_size)
{
    const cJSON *item = NULL;
    char *printed = NULL;
    if (!obj || !key || !out || out_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    out[0] = '\0';
    item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!item || cJSON_IsNull(item)) {
        return ESP_OK;
    }
    if (!cJSON_IsObject(item)) {
        return ESP_ERR_INVALID_ARG;
    }
    printed = cJSON_PrintUnformatted(item);
    if (!printed) {
        return ESP_ERR_NO_MEM;
    }
    if (strlen(printed) >= out_size) {
        cJSON_free(printed);
        return ESP_ERR_INVALID_SIZE;
    }
    gm_sidebar_preset_copy(out, out_size, printed);
    cJSON_free(printed);
    return ESP_OK;
}

static esp_err_t gm_sidebar_preset_from_json(const cJSON *json,
                                             gm_sidebar_preset_t *out,
                                             char *error,
                                             size_t error_size)
{
    esp_err_t err = ESP_OK;
    if (!cJSON_IsObject(json) || !out) {
        gm_sidebar_preset_set_error(error, error_size, "sidebar_preset_invalid");
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    gm_sidebar_preset_copy(out->id, sizeof(out->id), gm_sidebar_preset_json_string(json, "id"));
    gm_sidebar_preset_copy(out->label,
                           sizeof(out->label),
                           gm_sidebar_preset_json_string(json, "label"));
    gm_sidebar_preset_copy(out->device_id,
                           sizeof(out->device_id),
                           gm_sidebar_preset_json_string(json, "device_id"));
    gm_sidebar_preset_copy(out->resource_key,
                           sizeof(out->resource_key),
                           gm_sidebar_preset_json_string(json, "resource_key"));
    gm_sidebar_preset_copy(out->resource_label,
                           sizeof(out->resource_label),
                           gm_sidebar_preset_json_string(json, "resource_label"));
    gm_sidebar_preset_copy(out->command_id,
                           sizeof(out->command_id),
                           gm_sidebar_preset_json_string(json, "command_id"));
    gm_sidebar_preset_copy(out->command_label,
                           sizeof(out->command_label),
                           gm_sidebar_preset_json_string(json, "command_label"));
    err = gm_sidebar_preset_parse_raw_object_string(json,
                                                    "params",
                                                    out->params_json,
                                                    sizeof(out->params_json));
    if (err != ESP_OK) {
        gm_sidebar_preset_set_error(error, error_size, "sidebar_preset_params_invalid");
        return err;
    }
    return gm_sidebar_preset_validate_one(out, error, error_size);
}

static esp_err_t gm_sidebar_preset_to_json(const gm_sidebar_preset_t *preset, cJSON *array)
{
    cJSON *obj = NULL;
    esp_err_t err = ESP_OK;
    if (!preset || !array) {
        return ESP_ERR_INVALID_ARG;
    }
    obj = cJSON_CreateObject();
    if (!obj) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(obj, "id", preset->id);
    cJSON_AddStringToObject(obj, "label", preset->label);
    cJSON_AddStringToObject(obj, "device_id", preset->device_id);
    cJSON_AddStringToObject(obj, "resource_key", preset->resource_key);
    cJSON_AddStringToObject(obj, "resource_label", preset->resource_label);
    cJSON_AddStringToObject(obj, "command_id", preset->command_id);
    cJSON_AddStringToObject(obj, "command_label", preset->command_label);
    err = gm_sidebar_preset_json_add_raw_object(obj, "params", preset->params_json);
    if (err != ESP_OK) {
        cJSON_Delete(obj);
        return err;
    }
    cJSON_AddItemToArray(array, obj);
    return ESP_OK;
}

esp_err_t gm_sidebar_preset_import_json_locked(const cJSON *root,
                                               char *error,
                                               size_t error_size)
{
    gm_sidebar_preset_t *items = NULL;
    size_t count = 0;
    size_t max_items = 0;
    const cJSON *array = NULL;
    const cJSON *item = NULL;
    esp_err_t err = ESP_OK;
    if (!root) {
        gm_sidebar_preset_set_error(error, error_size, "sidebar_preset_payload_invalid");
        return ESP_ERR_INVALID_ARG;
    }
    array = gm_sidebar_preset_json_array(root);
    if (!cJSON_IsArray(array)) {
        gm_sidebar_preset_set_error(error, error_size, "sidebar_preset_array_missing");
        return ESP_ERR_INVALID_ARG;
    }
    if ((size_t)cJSON_GetArraySize(array) > GM_SIDEBAR_PRESET_MAX_ITEMS) {
        gm_sidebar_preset_set_error(error, error_size, "sidebar_preset_limit_exceeded");
        return ESP_ERR_INVALID_SIZE;
    }
    max_items = (size_t)cJSON_GetArraySize(array);
    if (max_items > 0) {
        items = (gm_sidebar_preset_t *)gm_sidebar_preset_alloc_bytes(sizeof(*items) * max_items);
        if (!items) {
            gm_sidebar_preset_set_error(error, error_size, "sidebar_preset_no_mem");
            return ESP_ERR_NO_MEM;
        }
        memset(items, 0, sizeof(*items) * max_items);
    }
    cJSON_ArrayForEach(item, array) {
        bool duplicate_id = false;
        if (count >= max_items) {
            gm_sidebar_preset_set_error(error, error_size, "sidebar_preset_limit_exceeded");
            heap_caps_free(items);
            return ESP_ERR_INVALID_SIZE;
        }
        err = gm_sidebar_preset_from_json(item, &items[count], error, error_size);
        if (err != ESP_OK) {
            heap_caps_free(items);
            return err;
        }
        for (size_t i = 0; i < count; ++i) {
            if (strcmp(items[i].id, items[count].id) == 0) {
                duplicate_id = true;
                break;
            }
        }
        if (duplicate_id) {
            gm_sidebar_preset_set_error(error, error_size, "sidebar_preset_duplicate_id");
            heap_caps_free(items);
            return ESP_ERR_INVALID_ARG;
        }
        ++count;
    }
    err = gm_sidebar_preset_replace_locked(items, count);
    heap_caps_free(items);
    return err;
}

esp_err_t gm_sidebar_preset_export_json_locked(cJSON **out)
{
    cJSON *root = NULL;
    cJSON *array = NULL;
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    root = cJSON_CreateObject();
    array = cJSON_CreateArray();
    if (!root || !array) {
        cJSON_Delete(root);
        cJSON_Delete(array);
        return ESP_ERR_NO_MEM;
    }
    size_t count = 0;
    const gm_sidebar_preset_t *items = gm_sidebar_preset_items_locked(&count);
    for (size_t i = 0; i < count; ++i) {
        esp_err_t err = gm_sidebar_preset_to_json(&items[i], array);
        if (err != ESP_OK) {
            cJSON_Delete(root);
            cJSON_Delete(array);
            return err;
        }
    }
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddItemToObject(root, "gm_sidebar_presets", array);
    *out = root;
    return ESP_OK;
}

esp_err_t gm_sidebar_preset_export_json(cJSON **out)
{
    esp_err_t err = gm_sidebar_preset_store_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = gm_sidebar_preset_export_json_locked(out);
    gm_sidebar_preset_store_unlock();
    return err;
}

esp_err_t gm_sidebar_preset_import_json(const cJSON *root,
                                        char *error,
                                        size_t error_size)
{
    esp_err_t err = gm_sidebar_preset_store_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = gm_sidebar_preset_import_json_locked(root, error, error_size);
    gm_sidebar_preset_store_unlock();
    return err;
}

esp_err_t gm_sidebar_preset_import_json_and_save(const cJSON *root,
                                                 char *error,
                                                 size_t error_size)
{
    esp_err_t err = gm_sidebar_preset_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = gm_sidebar_preset_store_lock();
    if (err != ESP_OK) {
        gm_sidebar_preset_persist_unlock();
        return err;
    }
    err = gm_sidebar_preset_import_json_locked(root, error, error_size);
    if (err == ESP_OK) {
        err = gm_sidebar_preset_save_to_path_locked(GM_SIDEBAR_PRESET_STORAGE_PATH);
    }
    gm_sidebar_preset_store_unlock();
    gm_sidebar_preset_persist_unlock();
    return err;
}
