#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "gm_game_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char id[GM_GAME_PROFILE_ID_MAX_LEN];
    char name[GM_GAME_PROFILE_NAME_MAX_LEN];
    char room_id[GM_GAME_PROFILE_ROOM_ID_MAX_LEN];
    char scenario_id[GM_GAME_PROFILE_SCENARIO_ID_MAX_LEN];
    uint32_t duration_ms;
    char hint_pack_id[GM_GAME_PROFILE_PACK_ID_MAX_LEN];
    char audio_pack_id[GM_GAME_PROFILE_PACK_ID_MAX_LEN];
    bool enabled;
    bool valid;
} orch_room_profile_entry_t;

esp_err_t orchestrator_registry_list_room_profiles(const char *room_id,
                                                   orch_room_profile_entry_t *out_profiles,
                                                   size_t max_profiles,
                                                   size_t *out_count);

#ifdef __cplusplus
}
#endif
