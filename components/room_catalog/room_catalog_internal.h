#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "room_catalog.h"

#define ROOM_CATALOG_JSON_VERSION 1
#define ROOM_CATALOG_FILE_MAX_BYTES (32 * 1024)

const char *room_catalog_default_room_id(void);
const char *room_catalog_default_room_name(void);

esp_err_t room_catalog_ensure_mutex(void);
esp_err_t room_catalog_ensure_persist_mutex(void);
esp_err_t room_catalog_lock(void);
esp_err_t room_catalog_persist_lock(void);
void room_catalog_unlock(void);
void room_catalog_persist_unlock(void);

void room_catalog_copy(char *dst, size_t dst_len, const char *src);
bool room_catalog_valid_id(const char *room_id);
const char *room_catalog_normalize_name(const room_catalog_entry_t *entry);
room_catalog_entry_t *room_catalog_find_entry(room_catalog_entry_t *entries,
                                              size_t count,
                                              const char *room_id);
int room_catalog_find_entry_index_locked(const char *room_id);
bool room_catalog_entry_valid(const room_catalog_entry_t *entry);

room_catalog_entry_t *room_catalog_alloc_entries(size_t count);
char *room_catalog_alloc_bytes(size_t size);

size_t room_catalog_count_locked(void);
room_catalog_entry_t *room_catalog_entries_locked(void);
uint32_t room_catalog_generation_locked(void);
void room_catalog_replace_all_locked(const room_catalog_entry_t *items, size_t count);
void room_catalog_increment_generation_locked(void);

esp_err_t room_catalog_save_to_path_locked(const char *path);
esp_err_t room_catalog_load_from_path_locked(const char *path);
