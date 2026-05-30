#include "gm_game_profile.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "room_catalog.h"
#include "room_scenario.h"
#include "scenehub_scenario_validation.h"
#include "sd_storage.h"

#define GM_GAME_PROFILE_JSON_VERSION 1
#define GM_GAME_PROFILE_FILE_MAX_BYTES (128 * 1024)

typedef struct {
    bool in_use;
    gm_game_profile_t profile;
} gm_game_profile_slot_t;

EXT_RAM_BSS_ATTR static gm_game_profile_slot_t s_profiles[GM_GAME_PROFILE_MAX_PROFILES];
static SemaphoreHandle_t s_lock = NULL;
static SemaphoreHandle_t s_persist_lock = NULL;
static StaticSemaphore_t s_lock_storage;
static StaticSemaphore_t s_persist_lock_storage;
static portMUX_TYPE s_init_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_generation = 0;

static esp_err_t gm_game_profile_save_to_path_locked(const char *path);
static esp_err_t gm_game_profile_load_from_path_locked(const char *path);

static esp_err_t gm_game_profile_ensure_lock(void)
{
    if (s_lock) {
        return ESP_OK;
    }
    portENTER_CRITICAL(&s_init_lock);
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
    }
    portEXIT_CRITICAL(&s_init_lock);
    return s_lock ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t gm_game_profile_ensure_persist_lock(void)
{
    if (s_persist_lock) {
        return ESP_OK;
    }
    portENTER_CRITICAL(&s_init_lock);
    if (!s_persist_lock) {
        s_persist_lock = xSemaphoreCreateMutexStatic(&s_persist_lock_storage);
    }
    portEXIT_CRITICAL(&s_init_lock);
    return s_persist_lock ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t gm_game_profile_lock(void)
{
    esp_err_t err = gm_game_profile_ensure_lock();
    if (err != ESP_OK) {
        return err;
    }
    return xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static esp_err_t gm_game_profile_persist_lock(void)
{
    esp_err_t err = gm_game_profile_ensure_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    return xSemaphoreTake(s_persist_lock, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void gm_game_profile_unlock(void)
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

static void gm_game_profile_persist_unlock(void)
{
    if (s_persist_lock) {
        xSemaphoreGive(s_persist_lock);
    }
}

static bool gm_game_profile_valid(const gm_game_profile_t *profile)
{
    return profile &&
           profile->id[0] &&
           profile->name[0] &&
           profile->room_id[0] &&
           profile->scenario_id[0] &&
           profile->duration_ms > 0;
}

static gm_game_profile_slot_t *gm_game_profile_find_locked(const char *profile_id)
{
    if (!profile_id || !profile_id[0]) {
        return NULL;
    }
    for (size_t i = 0; i < GM_GAME_PROFILE_MAX_PROFILES; ++i) {
        gm_game_profile_slot_t *slot = &s_profiles[i];
        if (slot->in_use && strcmp(slot->profile.id, profile_id) == 0) {
            return slot;
        }
    }
    return NULL;
}

static gm_game_profile_slot_t *gm_game_profile_find_free_locked(void)
{
    for (size_t i = 0; i < GM_GAME_PROFILE_MAX_PROFILES; ++i) {
        if (!s_profiles[i].in_use) {
            return &s_profiles[i];
        }
    }
    return NULL;
}

static gm_game_profile_t *gm_game_profile_alloc_items(size_t count)
{
    gm_game_profile_t *items = NULL;
    if (count == 0) {
        return NULL;
    }
    items = heap_caps_calloc(count, sizeof(*items), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!items) {
        items = heap_caps_calloc(count, sizeof(*items), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return items;
}

static char *gm_game_profile_alloc_bytes(size_t size)
{
    char *buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return buf;
}

static esp_err_t gm_game_profile_ensure_sd_for_path(const char *path)
{
    const char *root = sd_storage_root_path();
    size_t root_len = strlen(root);
    if (!path || strncmp(path, root, root_len) != 0 ||
        (path[root_len] != '\0' && path[root_len] != '/')) {
        return ESP_OK;
    }
    esp_err_t err = sd_storage_init();
    if (err != ESP_OK) {
        return err;
    }
    if (!sd_storage_available()) {
        err = sd_storage_mount();
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

static esp_err_t gm_game_profile_mkdir_parent(const char *path)
{
    char dir[160] = {0};
    const char *slash = NULL;
    size_t len = 0;
    if (!path || !path[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    slash = strrchr(path, '/');
    if (!slash || slash == path) {
        return ESP_OK;
    }
    len = (size_t)(slash - path);
    if (len >= sizeof(dir)) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(dir, path, len);
    dir[len] = '\0';
    for (char *p = dir + 1; *p; ++p) {
        if (*p != '/') {
            continue;
        }
        *p = '\0';
        if (mkdir(dir, 0775) != 0 && errno != EEXIST) {
            *p = '/';
            return ESP_FAIL;
        }
        *p = '/';
    }
    if (mkdir(dir, 0775) != 0 && errno != EEXIST) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t gm_game_profile_make_tmp_path(const char *path, char *tmp, size_t tmp_len)
{
    int written = 0;
    if (!path || !path[0] || !tmp || tmp_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    written = snprintf(tmp, tmp_len, "%s.tmp", path);
    if (written < 0 || (size_t)written >= tmp_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static esp_err_t gm_game_profile_copy_json_string(const cJSON *json,
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
    snprintf(dst, dst_len, "%s", item->valuestring);
    return ESP_OK;
}

esp_err_t gm_game_profile_init(void)
{
    esp_err_t err = gm_game_profile_ensure_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = gm_game_profile_ensure_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    return ESP_OK;
}

esp_err_t gm_game_profile_add(const gm_game_profile_t *profile)
{
    return gm_game_profile_upsert(profile);
}

esp_err_t gm_game_profile_upsert(const gm_game_profile_t *profile)
{
    gm_game_profile_slot_t *slot = NULL;
    esp_err_t err = ESP_OK;
    if (!gm_game_profile_valid(profile)) {
        return ESP_ERR_INVALID_ARG;
    }
    err = gm_game_profile_lock();
    if (err != ESP_OK) {
        return err;
    }
    slot = gm_game_profile_find_locked(profile->id);
    if (!slot) {
        slot = gm_game_profile_find_free_locked();
    }
    if (!slot) {
        gm_game_profile_unlock();
        return ESP_ERR_NO_MEM;
    }
    memset(slot, 0, sizeof(*slot));
    slot->in_use = true;
    slot->profile = *profile;
    s_generation++;
    gm_game_profile_unlock();
    return ESP_OK;
}

esp_err_t gm_game_profile_get(const char *profile_id, gm_game_profile_t *out)
{
    gm_game_profile_slot_t *slot = NULL;
    esp_err_t err = ESP_OK;
    if (!profile_id || !profile_id[0] || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    err = gm_game_profile_lock();
    if (err != ESP_OK) {
        return err;
    }
    slot = gm_game_profile_find_locked(profile_id);
    if (!slot) {
        gm_game_profile_unlock();
        return ESP_ERR_NOT_FOUND;
    }
    *out = slot->profile;
    gm_game_profile_unlock();
    return ESP_OK;
}

esp_err_t gm_game_profile_list_by_room(const char *room_id,
                                       gm_game_profile_t *out,
                                       size_t max_count,
                                       size_t *out_count)
{
    size_t count = 0;
    esp_err_t err = ESP_OK;
    if (!room_id || !room_id[0] || !out_count) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;
    if (max_count > 0 && !out) {
        return ESP_ERR_INVALID_ARG;
    }
    err = gm_game_profile_lock();
    if (err != ESP_OK) {
        return err;
    }
    for (size_t i = 0; i < GM_GAME_PROFILE_MAX_PROFILES; ++i) {
        const gm_game_profile_slot_t *slot = &s_profiles[i];
        if (!slot->in_use || strcmp(slot->profile.room_id, room_id) != 0) {
            continue;
        }
        if (count < max_count) {
            out[count] = slot->profile;
        }
        count++;
    }
    *out_count = count;
    gm_game_profile_unlock();
    return count > max_count ? ESP_ERR_INVALID_SIZE : ESP_OK;
}

esp_err_t gm_game_profile_delete(const char *profile_id)
{
    gm_game_profile_slot_t *slot = NULL;
    esp_err_t err = ESP_OK;
    if (!profile_id || !profile_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    err = gm_game_profile_lock();
    if (err != ESP_OK) {
        return err;
    }
    slot = gm_game_profile_find_locked(profile_id);
    if (!slot) {
        gm_game_profile_unlock();
        return ESP_ERR_NOT_FOUND;
    }
    memset(slot, 0, sizeof(*slot));
    s_generation++;
    gm_game_profile_unlock();
    return ESP_OK;
}

esp_err_t gm_game_profile_clear(void)
{
    esp_err_t err = gm_game_profile_lock();
    if (err != ESP_OK) {
        return err;
    }
    memset(s_profiles, 0, sizeof(s_profiles));
    s_generation++;
    gm_game_profile_unlock();
    return ESP_OK;
}

uint32_t gm_game_profile_generation(void)
{
    uint32_t generation = 0;
    if (gm_game_profile_lock() != ESP_OK) {
        return 0;
    }
    generation = s_generation;
    gm_game_profile_unlock();
    return generation;
}

esp_err_t gm_game_profile_validate(const gm_game_profile_t *profile)
{
    esp_err_t err = ESP_OK;
    room_scenario_t *scratch_scenario = NULL;
    room_scenario_validation_report_t *scratch_report = NULL;
    if (!gm_game_profile_valid(profile)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!room_catalog_exists(profile->room_id)) {
        return ESP_ERR_NOT_FOUND;
    }
    err = room_scenario_acquire_scratch(&scratch_scenario, &scratch_report);
    if (err != ESP_OK) {
        return err;
    }
    memset(scratch_scenario, 0, sizeof(*scratch_scenario));
    memset(scratch_report, 0, sizeof(*scratch_report));
    err = room_scenario_get(profile->scenario_id, scratch_scenario);
    if (err != ESP_OK) {
        room_scenario_release_scratch();
        return err;
    }
    if (strcmp(scratch_scenario->room_id, profile->room_id) != 0) {
        room_scenario_release_scratch();
        return ESP_ERR_INVALID_STATE;
    }
    err = scenehub_scenario_validate(scratch_scenario, scratch_report);
    if (err == ESP_OK && !scratch_report->valid) {
        err = ESP_ERR_INVALID_STATE;
    }
    room_scenario_release_scratch();
    return err;
}

esp_err_t gm_game_profile_validate_reference(const gm_game_profile_t *profile)
{
    if (!gm_game_profile_valid(profile)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!room_catalog_exists(profile->room_id)) {
        return ESP_ERR_NOT_FOUND;
    }
    return room_scenario_exists_in_room(profile->scenario_id, profile->room_id);
}

esp_err_t gm_game_profile_to_json(const gm_game_profile_t *profile, cJSON *out)
{
    if (!gm_game_profile_valid(profile) || !cJSON_IsObject(out)) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON_AddStringToObject(out, "id", profile->id);
    cJSON_AddStringToObject(out, "name", profile->name);
    cJSON_AddStringToObject(out, "room_id", profile->room_id);
    cJSON_AddStringToObject(out, "scenario_id", profile->scenario_id);
    cJSON_AddNumberToObject(out, "duration_ms", profile->duration_ms);
    cJSON_AddStringToObject(out, "hint_pack_id", profile->hint_pack_id);
    cJSON_AddStringToObject(out, "audio_pack_id", profile->audio_pack_id);
    cJSON_AddBoolToObject(out, "enabled", profile->enabled);
    return ESP_OK;
}

esp_err_t gm_game_profile_from_json(const cJSON *json, gm_game_profile_t *out)
{
    const cJSON *duration = NULL;
    const cJSON *enabled = NULL;
    esp_err_t err = ESP_OK;
    if (!cJSON_IsObject(json) || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    err = gm_game_profile_copy_json_string(json, "id", out->id, sizeof(out->id), true);
    if (err != ESP_OK) {
        return err;
    }
    err = gm_game_profile_copy_json_string(json, "name", out->name, sizeof(out->name), true);
    if (err != ESP_OK) {
        return err;
    }
    err = gm_game_profile_copy_json_string(json, "room_id", out->room_id, sizeof(out->room_id), true);
    if (err != ESP_OK) {
        return err;
    }
    err = gm_game_profile_copy_json_string(json, "scenario_id", out->scenario_id, sizeof(out->scenario_id), true);
    if (err != ESP_OK) {
        return err;
    }
    err = gm_game_profile_copy_json_string(json, "hint_pack_id", out->hint_pack_id, sizeof(out->hint_pack_id), false);
    if (err != ESP_OK) {
        return err;
    }
    err = gm_game_profile_copy_json_string(json, "audio_pack_id", out->audio_pack_id, sizeof(out->audio_pack_id), false);
    if (err != ESP_OK) {
        return err;
    }
    duration = cJSON_GetObjectItemCaseSensitive(json, "duration_ms");
    if (!cJSON_IsNumber(duration) || duration->valuedouble <= 0.0 ||
        duration->valuedouble > (double)UINT32_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    out->duration_ms = (uint32_t)duration->valuedouble;
    enabled = cJSON_GetObjectItemCaseSensitive(json, "enabled");
    if (!enabled) {
        out->enabled = true;
    } else if (cJSON_IsBool(enabled)) {
        out->enabled = cJSON_IsTrue(enabled);
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    return gm_game_profile_valid(out) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

static esp_err_t gm_game_profile_export_one_json(const gm_game_profile_t *profile, cJSON *array)
{
    cJSON *obj = cJSON_CreateObject();
    esp_err_t err = ESP_OK;
    if (!obj) {
        return ESP_ERR_NO_MEM;
    }
    err = gm_game_profile_to_json(profile, obj);
    if (err != ESP_OK) {
        cJSON_Delete(obj);
        return err;
    }
    if (!cJSON_AddItemToArray(array, obj)) {
        cJSON_Delete(obj);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t gm_game_profile_export_json(cJSON **out)
{
    cJSON *root = NULL;
    cJSON *array = NULL;
    esp_err_t err = ESP_OK;
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddNumberToObject(root, "version", GM_GAME_PROFILE_JSON_VERSION);
    array = cJSON_AddArrayToObject(root, "game_profiles");
    if (!array) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    err = gm_game_profile_lock();
    if (err != ESP_OK) {
        cJSON_Delete(root);
        return err;
    }
    for (size_t i = 0; i < GM_GAME_PROFILE_MAX_PROFILES; ++i) {
        const gm_game_profile_slot_t *slot = &s_profiles[i];
        if (!slot->in_use) {
            continue;
        }
        err = gm_game_profile_export_one_json(&slot->profile, array);
        if (err != ESP_OK) {
            gm_game_profile_unlock();
            cJSON_Delete(root);
            return err;
        }
    }
    gm_game_profile_unlock();
    *out = root;
    return ESP_OK;
}

esp_err_t gm_game_profile_import_json(const cJSON *root)
{
    const cJSON *version = NULL;
    const cJSON *array = NULL;
    gm_game_profile_t *items = NULL;
    int count = 0;
    esp_err_t err = ESP_OK;
    if (!cJSON_IsObject(root)) {
        return ESP_ERR_INVALID_ARG;
    }
    version = cJSON_GetObjectItemCaseSensitive(root, "version");
    if (!cJSON_IsNumber(version) || version->valueint != GM_GAME_PROFILE_JSON_VERSION) {
        return ESP_ERR_INVALID_VERSION;
    }
    array = cJSON_GetObjectItemCaseSensitive(root, "game_profiles");
    if (!cJSON_IsArray(array)) {
        return ESP_ERR_INVALID_ARG;
    }
    count = cJSON_GetArraySize(array);
    if (count < 0 || count > GM_GAME_PROFILE_MAX_PROFILES) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (count > 0) {
        items = gm_game_profile_alloc_items((size_t)count);
        if (!items) {
            return ESP_ERR_NO_MEM;
        }
    }
    for (int i = 0; i < count; ++i) {
        const cJSON *profile_obj = cJSON_GetArrayItem(array, i);
        err = gm_game_profile_from_json(profile_obj, &items[i]);
        if (err != ESP_OK) {
            heap_caps_free(items);
            return err;
        }
        for (int j = 0; j < i; ++j) {
            if (strcmp(items[j].id, items[i].id) == 0) {
                heap_caps_free(items);
                return ESP_ERR_INVALID_ARG;
            }
        }
    }

    err = gm_game_profile_lock();
    if (err != ESP_OK) {
        heap_caps_free(items);
        return err;
    }
    memset(s_profiles, 0, sizeof(s_profiles));
    for (int i = 0; i < count; ++i) {
        s_profiles[i].in_use = true;
        s_profiles[i].profile = items[i];
    }
    s_generation++;
    gm_game_profile_unlock();
    heap_caps_free(items);
    return ESP_OK;
}

esp_err_t gm_game_profile_save_to_path(const char *path)
{
    esp_err_t err = gm_game_profile_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = gm_game_profile_save_to_path_locked(path);
    gm_game_profile_persist_unlock();
    return err;
}

static esp_err_t gm_game_profile_save_to_path_locked(const char *path)
{
    cJSON *root = NULL;
    char *printed = NULL;
    char tmp_path[192] = {0};
    FILE *file = NULL;
    size_t len = 0;
    esp_err_t err = ESP_OK;
    uint32_t started_ms = sd_storage_trace_now_ms();
    char detail[64] = {0};
    if (!path || !path[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    err = gm_game_profile_ensure_sd_for_path(path);
    if (err != ESP_OK) {
        return err;
    }
    err = gm_game_profile_mkdir_parent(path);
    if (err != ESP_OK) {
        return err;
    }
    err = gm_game_profile_make_tmp_path(path, tmp_path, sizeof(tmp_path));
    if (err != ESP_OK) {
        return err;
    }
    err = gm_game_profile_export_json(&root);
    if (err != ESP_OK) {
        return err;
    }
    printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return ESP_ERR_NO_MEM;
    }
    len = strlen(printed);
    file = fopen(tmp_path, "wb");
    if (!file) {
        cJSON_free(printed);
        return ESP_FAIL;
    }
    if (len > 0 && fwrite(printed, 1, len, file) != len) {
        fclose(file);
        cJSON_free(printed);
        unlink(tmp_path);
        return ESP_FAIL;
    }
    if (fclose(file) != 0) {
        cJSON_free(printed);
        unlink(tmp_path);
        return ESP_FAIL;
    }
    cJSON_free(printed);
    unlink(path);
    if (rename(tmp_path, path) != 0) {
        unlink(tmp_path);
        return ESP_FAIL;
    }
    snprintf(detail, sizeof(detail), "bytes=%u", (unsigned)len);
    sd_storage_trace_log("gm_profile", "save", path, sd_storage_trace_now_ms() - started_ms, detail);
    return ESP_OK;
}

esp_err_t gm_game_profile_upsert_and_save(const gm_game_profile_t *profile)
{
    esp_err_t err = gm_game_profile_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = gm_game_profile_upsert(profile);
    if (err == ESP_OK) {
        err = gm_game_profile_save_to_path_locked(GM_GAME_PROFILE_STORAGE_PATH);
    }
    gm_game_profile_persist_unlock();
    return err;
}

esp_err_t gm_game_profile_delete_and_save(const char *profile_id)
{
    esp_err_t err = gm_game_profile_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = gm_game_profile_delete(profile_id);
    if (err == ESP_OK) {
        err = gm_game_profile_save_to_path_locked(GM_GAME_PROFILE_STORAGE_PATH);
    }
    gm_game_profile_persist_unlock();
    return err;
}

esp_err_t gm_game_profile_import_json_and_save(const cJSON *root)
{
    esp_err_t err = gm_game_profile_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = gm_game_profile_import_json(root);
    if (err == ESP_OK) {
        err = gm_game_profile_save_to_path_locked(GM_GAME_PROFILE_STORAGE_PATH);
    }
    gm_game_profile_persist_unlock();
    return err;
}

esp_err_t gm_game_profile_load_from_path(const char *path)
{
    esp_err_t err = gm_game_profile_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = gm_game_profile_load_from_path_locked(path);
    gm_game_profile_persist_unlock();
    return err;
}

static esp_err_t gm_game_profile_load_from_path_locked(const char *path)
{
    FILE *file = NULL;
    char *buf = NULL;
    long file_size = 0;
    size_t bytes_read = 0;
    cJSON *root = NULL;
    esp_err_t err = ESP_OK;
    uint32_t started_ms = sd_storage_trace_now_ms();
    char detail[64] = {0};
    if (!path || !path[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    err = gm_game_profile_ensure_sd_for_path(path);
    if (err != ESP_OK) {
        return err;
    }
    file = fopen(path, "rb");
    if (!file) {
        return ESP_ERR_NOT_FOUND;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return ESP_FAIL;
    }
    file_size = ftell(file);
    if (file_size < 0 || file_size > GM_GAME_PROFILE_FILE_MAX_BYTES) {
        fclose(file);
        return ESP_ERR_INVALID_SIZE;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return ESP_FAIL;
    }
    buf = gm_game_profile_alloc_bytes((size_t)file_size + 1);
    if (!buf) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }
    bytes_read = fread(buf, 1, (size_t)file_size, file);
    fclose(file);
    if (bytes_read != (size_t)file_size) {
        heap_caps_free(buf);
        return ESP_FAIL;
    }
    buf[bytes_read] = '\0';
    root = cJSON_ParseWithLength(buf, bytes_read);
    heap_caps_free(buf);
    if (!root) {
        return ESP_ERR_INVALID_ARG;
    }
    err = gm_game_profile_import_json(root);
    cJSON_Delete(root);
    snprintf(detail, sizeof(detail), "bytes=%u result=%s", (unsigned)bytes_read, esp_err_to_name(err));
    sd_storage_trace_log("gm_profile", "load", path, sd_storage_trace_now_ms() - started_ms, detail);
    return err;
}

esp_err_t gm_game_profile_save(void)
{
    return gm_game_profile_save_to_path(GM_GAME_PROFILE_STORAGE_PATH);
}

esp_err_t gm_game_profile_load(void)
{
    return gm_game_profile_load_from_path(GM_GAME_PROFILE_STORAGE_PATH);
}
