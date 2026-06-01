#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GM_GAME_PROFILE_ID_MAX_LEN 32
#define GM_GAME_PROFILE_NAME_MAX_LEN 64
#define GM_GAME_PROFILE_ROOM_ID_MAX_LEN 32
#define GM_GAME_PROFILE_SCENARIO_ID_MAX_LEN 32
#define GM_GAME_PROFILE_PACK_ID_MAX_LEN 32
#define GM_GAME_PROFILE_MAX_PROFILES 32
#define GM_GAME_PROFILE_STORAGE_PATH "/sdcard/quest/game_profiles.json"

typedef struct {
    char id[GM_GAME_PROFILE_ID_MAX_LEN];
    char name[GM_GAME_PROFILE_NAME_MAX_LEN];
    char room_id[GM_GAME_PROFILE_ROOM_ID_MAX_LEN];
    char scenario_id[GM_GAME_PROFILE_SCENARIO_ID_MAX_LEN];
    uint32_t duration_ms;
    char hint_pack_id[GM_GAME_PROFILE_PACK_ID_MAX_LEN];
    char audio_pack_id[GM_GAME_PROFILE_PACK_ID_MAX_LEN];
    bool enabled;
} gm_game_profile_t;

esp_err_t gm_game_profile_init(void);
esp_err_t gm_game_profile_add(const gm_game_profile_t *profile);
esp_err_t gm_game_profile_upsert(const gm_game_profile_t *profile);
esp_err_t gm_game_profile_upsert_and_save(const gm_game_profile_t *profile);
esp_err_t gm_game_profile_get(const char *profile_id, gm_game_profile_t *out);
esp_err_t gm_game_profile_list_by_room(const char *room_id,
                                       gm_game_profile_t *out,
                                       size_t max_count,
                                       size_t *out_count);
esp_err_t gm_game_profile_delete(const char *profile_id);
esp_err_t gm_game_profile_delete_and_save(const char *profile_id);
esp_err_t gm_game_profile_clear(void);
uint32_t gm_game_profile_generation(void);
esp_err_t gm_game_profile_validate(const gm_game_profile_t *profile);
esp_err_t gm_game_profile_validate_reference(const gm_game_profile_t *profile);

esp_err_t gm_game_profile_to_json(const gm_game_profile_t *profile, cJSON *out);
esp_err_t gm_game_profile_from_json(const cJSON *json, gm_game_profile_t *out);
esp_err_t gm_game_profile_export_json(cJSON **out);
esp_err_t gm_game_profile_import_json(const cJSON *root);
esp_err_t gm_game_profile_import_json_and_save(const cJSON *root);
esp_err_t gm_game_profile_save(void);
esp_err_t gm_game_profile_load(void);
esp_err_t gm_game_profile_save_to_path(const char *path);
esp_err_t gm_game_profile_load_from_path(const char *path);

#ifdef __cplusplus
}
#endif
