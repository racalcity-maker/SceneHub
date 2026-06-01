#pragma once

#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "gm_sidebar_presets.h"

#ifdef __cplusplus
extern "C" {
#endif

void gm_sidebar_preset_copy(char *dst, size_t dst_size, const char *src);
void gm_sidebar_preset_set_error(char *error, size_t error_size, const char *message);

esp_err_t gm_sidebar_preset_store_lock(void);
void gm_sidebar_preset_store_unlock(void);
esp_err_t gm_sidebar_preset_persist_lock(void);
void gm_sidebar_preset_persist_unlock(void);

char *gm_sidebar_preset_alloc_bytes(size_t size);

esp_err_t gm_sidebar_preset_validate_one(const gm_sidebar_preset_t *preset,
                                         char *error,
                                         size_t error_size);
esp_err_t gm_sidebar_preset_replace_locked(const gm_sidebar_preset_t *items, size_t count);
const gm_sidebar_preset_t *gm_sidebar_preset_items_locked(size_t *out_count);

esp_err_t gm_sidebar_preset_import_json_locked(const cJSON *root,
                                               char *error,
                                               size_t error_size);
esp_err_t gm_sidebar_preset_export_json_locked(cJSON **out);
esp_err_t gm_sidebar_preset_save_to_path_locked(const char *path);
esp_err_t gm_sidebar_preset_load_from_path_locked(const char *path);

#ifdef __cplusplus
}
#endif
