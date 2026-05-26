#include "scenehub_control_internal.h"

#include <string.h>

#include "gm_sidebar_presets.h"

esp_err_t scenehub_control_save_sidebar_presets_payload(const char *source,
                                                        const cJSON *payload,
                                                        scenehub_control_result_t *out_result)
{
    char error[64] = {0};
    (void)source;
    esp_err_t err = scenehub_control_prepare_result("", "sidebar_presets_save", out_result);
    if (err != ESP_OK) {
        return err;
    }
    if (!payload) {
        scenehub_control_fill_common_error(out_result, ESP_ERR_INVALID_ARG);
        return ESP_OK;
    }
    err = gm_sidebar_preset_import_json_and_save(payload, error, sizeof(error));
    if (err == ESP_OK) {
        scenehub_control_finish_success_with_invalidation(out_result,
                                                          SCENEHUB_STATE_SLICE_GM_SIDEBAR_PRESETS,
                                                          "",
                                                          "sidebar_presets_save");
        return ESP_OK;
    }
    if ((err == ESP_ERR_INVALID_ARG || err == ESP_ERR_INVALID_SIZE || err == ESP_ERR_NOT_FOUND ||
         err == ESP_ERR_INVALID_STATE) &&
        error[0]) {
        scenehub_control_set_result(out_result,
                                    SCENEHUB_CONTROL_STATUS_REJECTED,
                                    err,
                                    false,
                                    "invalid_request",
                                    error);
        return ESP_OK;
    }
    scenehub_control_fill_common_error(out_result, err);
    return ESP_OK;
}

esp_err_t scenehub_control_import_sidebar_presets(const char *source,
                                                  cJSON *root,
                                                  scenehub_control_result_t *out_result)
{
    return scenehub_control_save_sidebar_presets_payload(source, root, out_result);
}

esp_err_t scenehub_control_load_sidebar_presets(const char *source,
                                                scenehub_control_result_t *out_result)
{
    (void)source;
    esp_err_t err = scenehub_control_prepare_result("", "sidebar_presets_load", out_result);
    if (err != ESP_OK) {
        return err;
    }
    return scenehub_control_finalize_api_result_with_invalidation(out_result,
                                                                  gm_sidebar_preset_load(),
                                                                  SCENEHUB_STATE_SLICE_GM_SIDEBAR_PRESETS,
                                                                  "",
                                                                  "sidebar_presets_load");
}
