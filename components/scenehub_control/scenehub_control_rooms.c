#include "scenehub_control_internal.h"

#include <stdio.h>
#include <stdlib.h>

#include "esp_heap_caps.h"
#include "gm_game_profile.h"
#include "room_catalog.h"
#include "room_scenario.h"

static void *scenehub_control_heap_calloc(size_t count, size_t size)
{
    void *ptr = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
        ptr = heap_caps_calloc(count, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return ptr;
}

static esp_err_t scenehub_control_delete_profiles_for_room(const char *room_id,
                                                           size_t *out_deleted)
{
    gm_game_profile_t *profiles = NULL;
    size_t count = 0;
    esp_err_t err = ESP_OK;

    if (out_deleted) {
        *out_deleted = 0;
    }
    profiles = scenehub_control_heap_calloc(GM_GAME_PROFILE_MAX_PROFILES, sizeof(*profiles));
    if (!profiles) {
        return ESP_ERR_NO_MEM;
    }

    err = gm_game_profile_list_by_room(room_id, profiles, GM_GAME_PROFILE_MAX_PROFILES, &count);
    if (err != ESP_OK && err != ESP_ERR_INVALID_SIZE) {
        free(profiles);
        return err;
    }

    for (size_t i = 0; i < count; ++i) {
        if (gm_game_profile_delete_and_save(profiles[i].id) == ESP_OK && out_deleted) {
            ++(*out_deleted);
        }
    }

    free(profiles);
    return ESP_OK;
}

static esp_err_t scenehub_control_delete_scenarios_for_room(const char *room_id,
                                                            size_t *out_deleted)
{
    room_scenario_t *scenarios = NULL;
    size_t count = 0;
    esp_err_t err = ESP_OK;

    if (out_deleted) {
        *out_deleted = 0;
    }
    scenarios = scenehub_control_heap_calloc(ROOM_SCENARIO_MAX_SCENARIOS, sizeof(*scenarios));
    if (!scenarios) {
        return ESP_ERR_NO_MEM;
    }

    err = room_scenario_list_by_room(room_id, scenarios, ROOM_SCENARIO_MAX_SCENARIOS, &count);
    if (err != ESP_OK && err != ESP_ERR_INVALID_SIZE) {
        free(scenarios);
        return err;
    }

    for (size_t i = 0; i < count; ++i) {
        if (room_scenario_delete_and_save(scenarios[i].id) == ESP_OK && out_deleted) {
            ++(*out_deleted);
        }
    }

    free(scenarios);
    return ESP_OK;
}

esp_err_t scenehub_control_save_room(const char *source,
                                     const char *room_id,
                                     const char *name,
                                     scenehub_control_result_t *out_result)
{
    room_catalog_entry_t room = {0};
    const char *resolved_name = NULL;
    esp_err_t err = scenehub_control_prepare_result(room_id ? room_id : "", "room_save", out_result);

    (void)source;
    if (err != ESP_OK) {
        return err;
    }
    if (!room_id || !room_id[0]) {
        scenehub_control_fill_common_error(out_result, ESP_ERR_INVALID_ARG);
        return ESP_OK;
    }
    resolved_name = (name && name[0]) ? name : room_id;
    snprintf(room.room_id, sizeof(room.room_id), "%s", room_id);
    snprintf(room.name, sizeof(room.name), "%s", resolved_name);
    return scenehub_control_finalize_api_result_with_invalidation(
        out_result,
        scenehub_control_persistence_enabled() ? room_catalog_upsert_and_save(&room)
                                               : room_catalog_upsert(&room),
        SCENEHUB_STATE_SLICE_ROOM_CATALOG,
        room.room_id,
        "room_save");
}

esp_err_t scenehub_control_delete_room(const char *source,
                                       const char *room_id,
                                       bool delete_content,
                                       bool *out_existed,
                                       size_t *out_removed_profiles,
                                       size_t *out_removed_scenarios,
                                       scenehub_control_result_t *out_result)
{
    esp_err_t err = scenehub_control_prepare_result(room_id, "room_delete", out_result);
    bool existed = false;

    (void)source;
    if (err != ESP_OK) {
        return err;
    }
    if (!room_id || !room_id[0]) {
        scenehub_control_fill_common_error(out_result, ESP_ERR_INVALID_ARG);
        return ESP_OK;
    }
    if (out_existed) {
        *out_existed = false;
    }
    if (out_removed_profiles) {
        *out_removed_profiles = 0;
    }
    if (out_removed_scenarios) {
        *out_removed_scenarios = 0;
    }

    existed = room_catalog_exists(room_id);
    err = scenehub_control_persistence_enabled() ? room_catalog_delete_and_save(room_id)
                                                 : room_catalog_delete(room_id);
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        scenehub_control_fill_common_error(out_result, err);
        return ESP_OK;
    }

    if (delete_content) {
        err = scenehub_control_delete_profiles_for_room(room_id, out_removed_profiles);
        if (err != ESP_OK) {
            scenehub_control_set_result(out_result,
                                        SCENEHUB_CONTROL_STATUS_FAILED,
                                        err,
                                        false,
                                        "profile_cleanup_failed",
                                        "Profile cleanup failed");
            return ESP_OK;
        }
        err = scenehub_control_delete_scenarios_for_room(room_id, out_removed_scenarios);
        if (err != ESP_OK) {
            scenehub_control_set_result(out_result,
                                        SCENEHUB_CONTROL_STATUS_FAILED,
                                        err,
                                        false,
                                        "scenario_cleanup_failed",
                                        "Scenario cleanup failed");
            return ESP_OK;
        }
    }

    if (out_existed) {
        *out_existed = existed;
    }
    scenehub_control_finish_success_with_invalidation(out_result,
                                                      SCENEHUB_STATE_SLICE_FULL_SNAPSHOT,
                                                      room_id,
                                                      "room_delete");
    return ESP_OK;
}
