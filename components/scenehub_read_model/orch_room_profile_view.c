#include "orchestrator_registry_internal.h"

#include <string.h>

#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"

static EXT_RAM_BSS_ATTR gm_game_profile_t s_room_profile_scratch[GM_GAME_PROFILE_MAX_PROFILES];
static SemaphoreHandle_t s_room_profile_scratch_mutex = NULL;
static StaticSemaphore_t s_room_profile_scratch_mutex_storage;
static portMUX_TYPE s_room_profile_scratch_mutex_init_lock = portMUX_INITIALIZER_UNLOCKED;

static esp_err_t orch_room_profile_scratch_lock(void)
{
    if (!s_room_profile_scratch_mutex) {
        portENTER_CRITICAL(&s_room_profile_scratch_mutex_init_lock);
        if (!s_room_profile_scratch_mutex) {
            s_room_profile_scratch_mutex =
                xSemaphoreCreateMutexStatic(&s_room_profile_scratch_mutex_storage);
        }
        portEXIT_CRITICAL(&s_room_profile_scratch_mutex_init_lock);
    }
    if (!s_room_profile_scratch_mutex) {
        return ESP_ERR_NO_MEM;
    }
    return xSemaphoreTake(s_room_profile_scratch_mutex, portMAX_DELAY) == pdTRUE
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
}

static void orch_room_profile_scratch_unlock(void)
{
    if (s_room_profile_scratch_mutex) {
        xSemaphoreGive(s_room_profile_scratch_mutex);
    }
}

static void orch_room_profile_copy(const gm_game_profile_t *src, orch_room_profile_entry_t *dst)
{
    if (!src || !dst) {
        return;
    }
    memset(dst, 0, sizeof(*dst));
    quest_str_copy(dst->id, sizeof(dst->id), src->id);
    quest_str_copy(dst->name, sizeof(dst->name), src->name);
    quest_str_copy(dst->room_id, sizeof(dst->room_id), src->room_id);
    quest_str_copy(dst->scenario_id, sizeof(dst->scenario_id), src->scenario_id);
    dst->duration_ms = src->duration_ms;
    quest_str_copy(dst->hint_pack_id, sizeof(dst->hint_pack_id), src->hint_pack_id);
    quest_str_copy(dst->audio_pack_id, sizeof(dst->audio_pack_id), src->audio_pack_id);
    dst->enabled = src->enabled;
    dst->valid = gm_game_profile_validate_reference(src) == ESP_OK;
}

esp_err_t orch_room_profile_view_list(const char *room_id,
                                      orch_room_profile_entry_t *out_profiles,
                                      size_t max_profiles,
                                      size_t *out_count)
{
    size_t count = 0;
    size_t emitted = 0;
    esp_err_t err = ESP_OK;

    if (!room_id || !room_id[0] || !out_profiles || max_profiles == 0 || !out_count) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;
    if (room_catalog_init() != ESP_OK || room_catalog_refresh() != ESP_OK) {
        return ESP_FAIL;
    }
    if (!room_catalog_exists(room_id)) {
        return ESP_ERR_NOT_FOUND;
    }
    err = orch_room_profile_scratch_lock();
    if (err != ESP_OK) {
        return err;
    }
    memset(s_room_profile_scratch, 0, sizeof(s_room_profile_scratch));
    err = gm_game_profile_list_by_room(room_id,
                                       s_room_profile_scratch,
                                       GM_GAME_PROFILE_MAX_PROFILES,
                                       &count);
    if (err != ESP_OK) {
        orch_room_profile_scratch_unlock();
        return err;
    }
    for (size_t i = 0; i < count && emitted < max_profiles; ++i) {
        orch_room_profile_copy(&s_room_profile_scratch[i], &out_profiles[emitted]);
        emitted++;
    }
    orch_room_profile_scratch_unlock();
    *out_count = count;
    return count > max_profiles ? ESP_ERR_INVALID_SIZE : ESP_OK;
}
