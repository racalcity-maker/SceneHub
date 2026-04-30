#include "room_catalog.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sd_storage.h"

#define ROOM_CATALOG_JSON_VERSION 1
#define ROOM_CATALOG_FILE_MAX_BYTES (32 * 1024)

static const char *ROOM_CATALOG_DEFAULT_ROOM_ID = "unassigned";
static const char *ROOM_CATALOG_DEFAULT_ROOM_NAME = "Unassigned";

EXT_RAM_BSS_ATTR static room_catalog_entry_t s_rooms[ROOM_CATALOG_MAX_ROOMS];
static size_t s_room_count = 0;
static uint32_t s_generation = 0;
static SemaphoreHandle_t s_room_catalog_mutex = NULL;
static SemaphoreHandle_t s_room_catalog_persist_mutex = NULL;
static portMUX_TYPE s_room_catalog_init_lock = portMUX_INITIALIZER_UNLOCKED;

static esp_err_t room_catalog_save_to_path_locked(const char *path);
static esp_err_t room_catalog_load_from_path_locked(const char *path);

static esp_err_t room_catalog_ensure_mutex(void)
{
    if (s_room_catalog_mutex) {
        return ESP_OK;
    }
    portENTER_CRITICAL(&s_room_catalog_init_lock);
    if (!s_room_catalog_mutex) {
        s_room_catalog_mutex = xSemaphoreCreateMutex();
    }
    portEXIT_CRITICAL(&s_room_catalog_init_lock);
    return s_room_catalog_mutex ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t room_catalog_ensure_persist_mutex(void)
{
    if (s_room_catalog_persist_mutex) {
        return ESP_OK;
    }
    portENTER_CRITICAL(&s_room_catalog_init_lock);
    if (!s_room_catalog_persist_mutex) {
        s_room_catalog_persist_mutex = xSemaphoreCreateMutex();
    }
    portEXIT_CRITICAL(&s_room_catalog_init_lock);
    return s_room_catalog_persist_mutex ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t room_catalog_lock(void)
{
    esp_err_t err = room_catalog_ensure_mutex();
    if (err != ESP_OK) {
        return err;
    }
    return (xSemaphoreTake(s_room_catalog_mutex, portMAX_DELAY) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

static esp_err_t room_catalog_persist_lock(void)
{
    esp_err_t err = room_catalog_ensure_persist_mutex();
    if (err != ESP_OK) {
        return err;
    }
    return (xSemaphoreTake(s_room_catalog_persist_mutex, portMAX_DELAY) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void room_catalog_unlock(void)
{
    if (s_room_catalog_mutex) {
        xSemaphoreGive(s_room_catalog_mutex);
    }
}

static void room_catalog_persist_unlock(void)
{
    if (s_room_catalog_persist_mutex) {
        xSemaphoreGive(s_room_catalog_persist_mutex);
    }
}

static void room_catalog_copy(char *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_len, "%s", src);
}

static bool room_catalog_valid_id(const char *room_id)
{
    return room_id && room_id[0];
}

static const char *room_catalog_normalize_name(const room_catalog_entry_t *entry)
{
    if (entry && entry->name[0]) {
        return entry->name;
    }
    if (entry && entry->room_id[0]) {
        return entry->room_id;
    }
    return ROOM_CATALOG_DEFAULT_ROOM_NAME;
}

static room_catalog_entry_t *find_entry(room_catalog_entry_t *entries, size_t count, const char *room_id)
{
    if (!entries || !room_catalog_valid_id(room_id)) {
        return NULL;
    }
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(entries[i].room_id, room_id) == 0) {
            return &entries[i];
        }
    }
    return NULL;
}

static int find_entry_index(const char *room_id)
{
    if (!room_catalog_valid_id(room_id)) {
        return -1;
    }
    for (size_t i = 0; i < s_room_count; ++i) {
        if (strcmp(s_rooms[i].room_id, room_id) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static bool room_catalog_entry_valid(const room_catalog_entry_t *entry)
{
    if (!entry || !entry->room_id[0] ||
        strlen(entry->room_id) >= ROOM_CATALOG_ROOM_ID_MAX_LEN ||
        strlen(room_catalog_normalize_name(entry)) >= ROOM_CATALOG_ROOM_NAME_MAX_LEN) {
        return false;
    }
    return true;
}

static room_catalog_entry_t *room_catalog_alloc_entries(size_t count)
{
    if (count == 0) {
        return NULL;
    }
    room_catalog_entry_t *entries =
        heap_caps_calloc(count, sizeof(room_catalog_entry_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!entries) {
        entries = heap_caps_calloc(count, sizeof(room_catalog_entry_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return entries;
}

static char *room_catalog_alloc_bytes(size_t size)
{
    char *buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return buf;
}

static esp_err_t room_catalog_ensure_sd_for_path(const char *path)
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

static esp_err_t room_catalog_mkdir_parent(const char *path)
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

static esp_err_t room_catalog_make_tmp_path(const char *path, char *tmp, size_t tmp_len)
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

esp_err_t room_catalog_init(void)
{
    esp_err_t err = room_catalog_ensure_mutex();
    if (err != ESP_OK) {
        return err;
    }
    return room_catalog_ensure_persist_mutex();
}

esp_err_t room_catalog_refresh(void)
{
    return room_catalog_init();
}

esp_err_t room_catalog_upsert(const room_catalog_entry_t *entry)
{
    room_catalog_entry_t normalized = {0};
    int existing = -1;
    esp_err_t err = ESP_OK;
    if (!room_catalog_entry_valid(entry)) {
        return ESP_ERR_INVALID_ARG;
    }
    room_catalog_copy(normalized.room_id, sizeof(normalized.room_id), entry->room_id);
    room_catalog_copy(normalized.name, sizeof(normalized.name), room_catalog_normalize_name(entry));
    normalized.device_count = entry->device_count;

    err = room_catalog_lock();
    if (err != ESP_OK) {
        return err;
    }
    existing = find_entry_index(normalized.room_id);
    if (existing >= 0) {
        s_rooms[existing] = normalized;
    } else {
        if (s_room_count >= ROOM_CATALOG_MAX_ROOMS) {
            room_catalog_unlock();
            return ESP_ERR_NO_MEM;
        }
        s_rooms[s_room_count++] = normalized;
    }
    s_generation++;
    room_catalog_unlock();
    return ESP_OK;
}

esp_err_t room_catalog_delete(const char *room_id)
{
    int index = -1;
    esp_err_t err = ESP_OK;
    if (!room_catalog_valid_id(room_id) || strcmp(room_id, ROOM_CATALOG_DEFAULT_ROOM_ID) == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    err = room_catalog_lock();
    if (err != ESP_OK) {
        return err;
    }
    index = find_entry_index(room_id);
    if (index < 0) {
        room_catalog_unlock();
        return ESP_ERR_NOT_FOUND;
    }
    for (size_t i = (size_t)index; i + 1 < s_room_count; ++i) {
        s_rooms[i] = s_rooms[i + 1];
    }
    if (s_room_count > 0) {
        memset(&s_rooms[s_room_count - 1], 0, sizeof(s_rooms[0]));
        s_room_count--;
    }
    s_generation++;
    room_catalog_unlock();
    return ESP_OK;
}

esp_err_t room_catalog_clear(void)
{
    esp_err_t err = room_catalog_lock();
    if (err != ESP_OK) {
        return err;
    }
    memset(s_rooms, 0, sizeof(s_rooms));
    s_room_count = 0;
    s_generation++;
    room_catalog_unlock();
    return ESP_OK;
}

uint32_t room_catalog_generation(void)
{
    uint32_t generation = 0;
    if (room_catalog_lock() != ESP_OK) {
        return 0;
    }
    generation = s_generation;
    room_catalog_unlock();
    return generation;
}

bool room_catalog_exists(const char *room_id)
{
    bool exists = false;
    if (!room_catalog_valid_id(room_id)) {
        return false;
    }
    if (room_catalog_lock() != ESP_OK) {
        return false;
    }
    exists = (find_entry(s_rooms, s_room_count, room_id) != NULL);
    room_catalog_unlock();
    return exists;
}

size_t room_catalog_count(void)
{
    size_t count = 0;
    if (room_catalog_lock() != ESP_OK) {
        return 0;
    }
    count = s_room_count;
    room_catalog_unlock();
    return count;
}

esp_err_t room_catalog_get(size_t index, room_catalog_entry_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = room_catalog_lock();
    if (err != ESP_OK) {
        return err;
    }
    if (index >= s_room_count) {
        room_catalog_unlock();
        return ESP_ERR_NOT_FOUND;
    }
    *out = s_rooms[index];
    room_catalog_unlock();
    return ESP_OK;
}

esp_err_t room_catalog_find(const char *room_id, room_catalog_entry_t *out)
{
    room_catalog_entry_t *entry = NULL;
    if (!room_catalog_valid_id(room_id) || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = room_catalog_lock();
    if (err != ESP_OK) {
        return err;
    }
    entry = find_entry(s_rooms, s_room_count, room_id);
    if (!entry) {
        room_catalog_unlock();
        return ESP_ERR_NOT_FOUND;
    }
    *out = *entry;
    room_catalog_unlock();
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
    for (size_t i = 0; i < s_room_count; ++i) {
        err = room_catalog_entry_to_json(&s_rooms[i], rooms);
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
    memset(s_rooms, 0, sizeof(s_rooms));
    s_room_count = (size_t)count;
    for (int i = 0; i < count; ++i) {
        s_rooms[i] = items[i];
    }
    s_generation++;
    room_catalog_unlock();
    heap_caps_free(items);
    return ESP_OK;
}

esp_err_t room_catalog_save(void)
{
    return room_catalog_save_to_path(ROOM_CATALOG_STORAGE_PATH);
}

esp_err_t room_catalog_load(void)
{
    return room_catalog_load_from_path(ROOM_CATALOG_STORAGE_PATH);
}

esp_err_t room_catalog_save_to_path(const char *path)
{
    esp_err_t err = room_catalog_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = room_catalog_save_to_path_locked(path);
    room_catalog_persist_unlock();
    return err;
}

static esp_err_t room_catalog_save_to_path_locked(const char *path)
{
    cJSON *root = NULL;
    char *printed = NULL;
    FILE *f = NULL;
    char tmp[192] = {0};
    esp_err_t err = ESP_OK;
    if (!path || !path[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    err = room_catalog_ensure_sd_for_path(path);
    if (err != ESP_OK) {
        return err;
    }
    err = room_catalog_mkdir_parent(path);
    if (err != ESP_OK) {
        return err;
    }
    err = room_catalog_make_tmp_path(path, tmp, sizeof(tmp));
    if (err != ESP_OK) {
        return err;
    }
    err = room_catalog_export_json(&root);
    if (err != ESP_OK) {
        return err;
    }
    printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return ESP_ERR_NO_MEM;
    }
    f = fopen(tmp, "wb");
    if (!f) {
        cJSON_free(printed);
        return ESP_FAIL;
    }
    size_t len = strlen(printed);
    if (fwrite(printed, 1, len, f) != len) {
        fclose(f);
        unlink(tmp);
        cJSON_free(printed);
        return ESP_FAIL;
    }
    if (fclose(f) != 0) {
        unlink(tmp);
        cJSON_free(printed);
        return ESP_FAIL;
    }
    cJSON_free(printed);
    unlink(path);
    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t room_catalog_upsert_and_save(const room_catalog_entry_t *entry)
{
    esp_err_t err = room_catalog_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = room_catalog_upsert(entry);
    if (err == ESP_OK) {
        err = room_catalog_save_to_path_locked(ROOM_CATALOG_STORAGE_PATH);
    }
    room_catalog_persist_unlock();
    return err;
}

esp_err_t room_catalog_delete_and_save(const char *room_id)
{
    esp_err_t err = room_catalog_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = room_catalog_delete(room_id);
    if (err == ESP_OK) {
        err = room_catalog_save_to_path_locked(ROOM_CATALOG_STORAGE_PATH);
    }
    room_catalog_persist_unlock();
    return err;
}

esp_err_t room_catalog_import_json_and_save(const cJSON *root)
{
    esp_err_t err = room_catalog_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = room_catalog_import_json(root);
    if (err == ESP_OK) {
        err = room_catalog_save_to_path_locked(ROOM_CATALOG_STORAGE_PATH);
    }
    room_catalog_persist_unlock();
    return err;
}

esp_err_t room_catalog_load_from_path(const char *path)
{
    esp_err_t err = room_catalog_persist_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = room_catalog_load_from_path_locked(path);
    room_catalog_persist_unlock();
    return err;
}

static esp_err_t room_catalog_load_from_path_locked(const char *path)
{
    FILE *f = NULL;
    char *buf = NULL;
    long size = 0;
    size_t bytes_read = 0;
    cJSON *root = NULL;
    esp_err_t err = ESP_OK;
    if (!path || !path[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    err = room_catalog_ensure_sd_for_path(path);
    if (err != ESP_OK) {
        return err;
    }
    f = fopen(path, "rb");
    if (!f) {
        return ESP_ERR_NOT_FOUND;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return ESP_FAIL;
    }
    size = ftell(f);
    if (size < 0 || size > ROOM_CATALOG_FILE_MAX_BYTES) {
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }
    rewind(f);
    buf = room_catalog_alloc_bytes((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    bytes_read = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (bytes_read != (size_t)size) {
        heap_caps_free(buf);
        return ESP_FAIL;
    }
    buf[bytes_read] = '\0';
    root = cJSON_ParseWithLength(buf, bytes_read);
    heap_caps_free(buf);
    if (!root) {
        return ESP_ERR_INVALID_ARG;
    }
    err = room_catalog_import_json(root);
    cJSON_Delete(root);
    return err;
}
