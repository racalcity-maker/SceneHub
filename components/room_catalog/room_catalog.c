#include "room_catalog_internal.h"

#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *ROOM_CATALOG_DEFAULT_ROOM_ID = "unassigned";
static const char *ROOM_CATALOG_DEFAULT_ROOM_NAME = "Unassigned";

EXT_RAM_BSS_ATTR static room_catalog_entry_t s_rooms[ROOM_CATALOG_MAX_ROOMS];
static size_t s_room_count = 0;
static uint32_t s_generation = 0;
static SemaphoreHandle_t s_room_catalog_mutex = NULL;
static SemaphoreHandle_t s_room_catalog_persist_mutex = NULL;
static portMUX_TYPE s_room_catalog_init_lock = portMUX_INITIALIZER_UNLOCKED;

const char *room_catalog_default_room_id(void)
{
    return ROOM_CATALOG_DEFAULT_ROOM_ID;
}

const char *room_catalog_default_room_name(void)
{
    return ROOM_CATALOG_DEFAULT_ROOM_NAME;
}

esp_err_t room_catalog_ensure_mutex(void)
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

esp_err_t room_catalog_ensure_persist_mutex(void)
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

esp_err_t room_catalog_lock(void)
{
    esp_err_t err = room_catalog_ensure_mutex();
    if (err != ESP_OK) {
        return err;
    }
    return (xSemaphoreTake(s_room_catalog_mutex, portMAX_DELAY) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t room_catalog_persist_lock(void)
{
    esp_err_t err = room_catalog_ensure_persist_mutex();
    if (err != ESP_OK) {
        return err;
    }
    return (xSemaphoreTake(s_room_catalog_persist_mutex, portMAX_DELAY) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

void room_catalog_unlock(void)
{
    if (s_room_catalog_mutex) {
        xSemaphoreGive(s_room_catalog_mutex);
    }
}

void room_catalog_persist_unlock(void)
{
    if (s_room_catalog_persist_mutex) {
        xSemaphoreGive(s_room_catalog_persist_mutex);
    }
}

void room_catalog_copy(char *dst, size_t dst_len, const char *src)
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

bool room_catalog_valid_id(const char *room_id)
{
    return room_id && room_id[0];
}

const char *room_catalog_normalize_name(const room_catalog_entry_t *entry)
{
    if (entry && entry->name[0]) {
        return entry->name;
    }
    if (entry && entry->room_id[0]) {
        return entry->room_id;
    }
    return room_catalog_default_room_name();
}

room_catalog_entry_t *room_catalog_find_entry(room_catalog_entry_t *entries,
                                              size_t count,
                                              const char *room_id)
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

int room_catalog_find_entry_index_locked(const char *room_id)
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

bool room_catalog_entry_valid(const room_catalog_entry_t *entry)
{
    if (!entry || !entry->room_id[0] ||
        strlen(entry->room_id) >= ROOM_CATALOG_ROOM_ID_MAX_LEN ||
        strlen(room_catalog_normalize_name(entry)) >= ROOM_CATALOG_ROOM_NAME_MAX_LEN) {
        return false;
    }
    return true;
}

room_catalog_entry_t *room_catalog_alloc_entries(size_t count)
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

char *room_catalog_alloc_bytes(size_t size)
{
    char *buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return buf;
}

size_t room_catalog_count_locked(void)
{
    return s_room_count;
}

room_catalog_entry_t *room_catalog_entries_locked(void)
{
    return s_rooms;
}

uint32_t room_catalog_generation_locked(void)
{
    return s_generation;
}

void room_catalog_replace_all_locked(const room_catalog_entry_t *items, size_t count)
{
    memset(s_rooms, 0, sizeof(s_rooms));
    s_room_count = count;
    for (size_t i = 0; i < count; ++i) {
        s_rooms[i] = items[i];
    }
    s_generation++;
}

void room_catalog_increment_generation_locked(void)
{
    s_generation++;
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
    existing = room_catalog_find_entry_index_locked(normalized.room_id);
    if (existing >= 0) {
        s_rooms[existing] = normalized;
    } else {
        if (s_room_count >= ROOM_CATALOG_MAX_ROOMS) {
            room_catalog_unlock();
            return ESP_ERR_NO_MEM;
        }
        s_rooms[s_room_count++] = normalized;
    }
    room_catalog_increment_generation_locked();
    room_catalog_unlock();
    return ESP_OK;
}

esp_err_t room_catalog_delete(const char *room_id)
{
    int index = -1;
    esp_err_t err = ESP_OK;
    if (!room_catalog_valid_id(room_id) ||
        strcmp(room_id, room_catalog_default_room_id()) == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    err = room_catalog_lock();
    if (err != ESP_OK) {
        return err;
    }
    index = room_catalog_find_entry_index_locked(room_id);
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
    room_catalog_increment_generation_locked();
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
    room_catalog_increment_generation_locked();
    room_catalog_unlock();
    return ESP_OK;
}

uint32_t room_catalog_generation(void)
{
    uint32_t generation = 0;
    if (room_catalog_lock() != ESP_OK) {
        return 0;
    }
    generation = room_catalog_generation_locked();
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
    exists = (room_catalog_find_entry(s_rooms, s_room_count, room_id) != NULL);
    room_catalog_unlock();
    return exists;
}

size_t room_catalog_count(void)
{
    size_t count = 0;
    if (room_catalog_lock() != ESP_OK) {
        return 0;
    }
    count = room_catalog_count_locked();
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
    entry = room_catalog_find_entry(s_rooms, s_room_count, room_id);
    if (!entry) {
        room_catalog_unlock();
        return ESP_ERR_NOT_FOUND;
    }
    *out = *entry;
    room_catalog_unlock();
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
