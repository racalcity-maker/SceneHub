#include "scenehub_control_internal.h"

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
        err = gm_game_profile_upsert_and_save(profile);
    }
    return scenehub_control_finalize_api_result_with_invalidation(out_result,
                                                                  err,
                                                                  SCENEHUB_STATE_SLICE_ROOM_PROFILES,
                                                                  profile->room_id,
                                                                  "profile_save");
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
    return scenehub_control_finalize_api_result_with_invalidation(out_result,
                                                                  gm_game_profile_delete_and_save(profile_id),
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
