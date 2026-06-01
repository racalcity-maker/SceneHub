#pragma once

#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "esp_err.h"
#include "quest_common_limits.h"
#include "quest_device.h"
#include "room_scenario.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GM_SIDEBAR_PRESET_STORAGE_PATH "/sdcard/quest/gm_sidebar_presets.json"
#define GM_SIDEBAR_PRESET_MAX_ITEMS 32
#define GM_SIDEBAR_PRESET_ID_MAX_LEN QUEST_ID_MAX_LEN
#define GM_SIDEBAR_PRESET_LABEL_MAX_LEN QUEST_BUTTON_LABEL_MAX_LEN
#define GM_SIDEBAR_PRESET_RESOURCE_KEY_MAX_LEN 48
#define GM_SIDEBAR_PRESET_RESOURCE_LABEL_MAX_LEN QUEST_NAME_MAX_LEN
#define GM_SIDEBAR_PRESET_COMMAND_LABEL_MAX_LEN QUEST_NAME_MAX_LEN
#define GM_SIDEBAR_PRESET_PARAMS_JSON_MAX_LEN ROOM_SCENARIO_COMMAND_PARAMS_JSON_MAX_LEN

typedef struct {
    char id[GM_SIDEBAR_PRESET_ID_MAX_LEN];
    char label[GM_SIDEBAR_PRESET_LABEL_MAX_LEN];
    char device_id[QUEST_DEVICE_ID_MAX_LEN];
    char resource_key[GM_SIDEBAR_PRESET_RESOURCE_KEY_MAX_LEN];
    char resource_label[GM_SIDEBAR_PRESET_RESOURCE_LABEL_MAX_LEN];
    char command_id[QUEST_DEVICE_COMMAND_ID_MAX_LEN];
    char command_label[GM_SIDEBAR_PRESET_COMMAND_LABEL_MAX_LEN];
    char params_json[GM_SIDEBAR_PRESET_PARAMS_JSON_MAX_LEN];
} gm_sidebar_preset_t;

esp_err_t gm_sidebar_presets_init(void);
uint32_t gm_sidebar_preset_generation(void);
esp_err_t gm_sidebar_preset_list(gm_sidebar_preset_t *out,
                                 size_t max_count,
                                 size_t *out_count);
esp_err_t gm_sidebar_preset_replace(const gm_sidebar_preset_t *items, size_t count);
esp_err_t gm_sidebar_preset_replace_and_save(const gm_sidebar_preset_t *items, size_t count);
esp_err_t gm_sidebar_preset_validate_entry(const gm_sidebar_preset_t *preset,
                                           char *error,
                                           size_t error_size);
esp_err_t gm_sidebar_preset_export_json(cJSON **out);
esp_err_t gm_sidebar_preset_import_json(const cJSON *root,
                                        char *error,
                                        size_t error_size);
esp_err_t gm_sidebar_preset_import_json_and_save(const cJSON *root,
                                                 char *error,
                                                 size_t error_size);
esp_err_t gm_sidebar_preset_save(void);
esp_err_t gm_sidebar_preset_load(void);
esp_err_t gm_sidebar_preset_save_to_path(const char *path);
esp_err_t gm_sidebar_preset_load_from_path(const char *path);

#ifdef __cplusplus
}
#endif
