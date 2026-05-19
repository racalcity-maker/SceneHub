#include "scenehub_control_internal.h"

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "gm_api.h"
#include "gm_game_profile.h"

esp_err_t scenehub_control_select_profile(const char *source,
                                          const char *room_id,
                                          const char *profile_id,
                                          scenehub_control_result_t *out_result)
{
    (void)source;
    esp_err_t err = scenehub_control_prepare_result(room_id, "profile_select", out_result);
    if (err != ESP_OK) {
        return err;
    }
    return scenehub_control_finalize_api_result_with_invalidation(out_result,
                                                                  gm_api_select_profile(room_id, profile_id),
                                                                  SCENEHUB_STATE_SLICE_ROOM_RUNTIME,
                                                                  room_id,
                                                                  "profile_select");
}

esp_err_t scenehub_control_save_profile(const char *source,
                                        const gm_game_profile_t *profile,
                                        scenehub_control_result_t *out_result)
{
    (void)source;
    esp_err_t err = scenehub_control_prepare_result(profile ? profile->room_id : "", "profile_save", out_result);
    if (err != ESP_OK) {
        return err;
    }
    if (!profile) {
        scenehub_control_fill_common_error(out_result, ESP_ERR_INVALID_ARG);
        return ESP_OK;
    }
    err = gm_game_profile_validate_reference(profile);
    if (err == ESP_OK) {
        err = scenehub_control_persistence_enabled() ? gm_game_profile_upsert_and_save(profile)
                                                     : gm_game_profile_upsert(profile);
    }
    return scenehub_control_finalize_api_result_with_invalidation(out_result,
                                                                  err,
                                                                  SCENEHUB_STATE_SLICE_ROOM_PROFILES,
                                                                  profile->room_id,
                                                                  "profile_save");
}

esp_err_t scenehub_control_save_profile_payload(const char *source,
                                                const cJSON *payload,
                                                cJSON **out_profile_json,
                                                scenehub_control_result_t *out_result)
{
    (void)source;
    const cJSON *profile_payload = NULL;
    gm_game_profile_t *profile = NULL;
    cJSON *profile_json = NULL;
    esp_err_t err = scenehub_control_prepare_result("", "profile_save", out_result);
    if (err != ESP_OK) {
        return err;
    }
    if (out_profile_json) {
        *out_profile_json = NULL;
    }
    if (!payload) {
        scenehub_control_fill_common_error(out_result, ESP_ERR_INVALID_ARG);
        return ESP_OK;
    }

    profile_payload = cJSON_GetObjectItemCaseSensitive(payload, "profile");
    if (!cJSON_IsObject(profile_payload)) {
        profile_payload = payload;
    }

    profile = heap_caps_calloc(1, sizeof(*profile), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!profile) {
        profile = heap_caps_calloc(1, sizeof(*profile), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!profile) {
        scenehub_control_fill_common_error(out_result, ESP_ERR_NO_MEM);
        return ESP_OK;
    }

    err = gm_game_profile_from_json(profile_payload, profile);
    if (err == ESP_OK && out_profile_json) {
        profile_json = cJSON_CreateObject();
        if (!profile_json) {
            err = ESP_ERR_NO_MEM;
        } else {
            err = gm_game_profile_to_json(profile, profile_json);
        }
    }
    if (err == ESP_OK) {
        scenehub_control_copy(out_result->room_id, sizeof(out_result->room_id), profile->room_id);
        err = gm_game_profile_validate_reference(profile);
    }
    if (err == ESP_OK) {
        err = scenehub_control_persistence_enabled() ? gm_game_profile_upsert_and_save(profile)
                                                     : gm_game_profile_upsert(profile);
    }
    if (err == ESP_OK) {
        scenehub_control_finish_success_with_invalidation(out_result,
                                                          SCENEHUB_STATE_SLICE_ROOM_PROFILES,
                                                          profile->room_id,
                                                          "profile_save");
    } else {
        cJSON_Delete(profile_json);
        profile_json = NULL;
        scenehub_control_fill_common_error(out_result, err);
    }
    heap_caps_free(profile);
    if (out_profile_json) {
        *out_profile_json = profile_json;
    } else {
        cJSON_Delete(profile_json);
    }
    return ESP_OK;
}

esp_err_t scenehub_control_delete_profile(const char *source,
                                          const char *profile_id,
                                          scenehub_control_result_t *out_result)
{
    gm_game_profile_t profile = {0};
    const char *room_id = "";
    (void)source;
    esp_err_t err = scenehub_control_prepare_result("", "profile_delete", out_result);
    if (err != ESP_OK) {
        return err;
    }
    if (profile_id && gm_game_profile_get(profile_id, &profile) == ESP_OK) {
        room_id = profile.room_id;
    }
    return scenehub_control_finalize_api_result_with_invalidation(
        out_result,
        scenehub_control_persistence_enabled() ? gm_game_profile_delete_and_save(profile_id)
                                               : gm_game_profile_delete(profile_id),
        SCENEHUB_STATE_SLICE_ROOM_PROFILES,
        room_id,
        "profile_delete");
}

esp_err_t scenehub_control_import_profiles(const char *source,
                                           cJSON *root,
                                           scenehub_control_result_t *out_result)
{
    (void)source;
    esp_err_t err = scenehub_control_prepare_result("", "profile_import", out_result);
    if (err != ESP_OK) {
        return err;
    }
    return scenehub_control_finalize_api_result_with_invalidation(out_result,
                                                                  gm_game_profile_import_json_and_save(root),
                                                                  SCENEHUB_STATE_SLICE_ROOM_PROFILES,
                                                                  "",
                                                                  "profile_import");
}

esp_err_t scenehub_control_load_profiles(const char *source,
                                         scenehub_control_result_t *out_result)
{
    (void)source;
    esp_err_t err = scenehub_control_prepare_result("", "profile_load", out_result);
    if (err != ESP_OK) {
        return err;
    }
    return scenehub_control_finalize_api_result_with_invalidation(out_result,
                                                                  gm_game_profile_load(),
                                                                  SCENEHUB_STATE_SLICE_ROOM_PROFILES,
                                                                  "",
                                                                  "profile_load");
}

esp_err_t scenehub_control_save_profiles_store(const char *source,
                                               scenehub_control_result_t *out_result)
{
    (void)source;
    esp_err_t err = scenehub_control_prepare_result("", "profile_store_save", out_result);
    if (err != ESP_OK) {
        return err;
    }
    return scenehub_control_finalize_no_state_change_result(out_result, gm_game_profile_save());
}
