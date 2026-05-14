#include "scenehub_control_internal.h"

#include "gm_api.h"
#include "room_scenario.h"
#include "scenehub_scenario_validation.h"

esp_err_t scenehub_control_select_scenario(const char *source,
                                           const char *room_id,
                                           const char *scenario_id,
                                           scenehub_control_result_t *out_result)
{
    (void)source;
    esp_err_t err = scenehub_control_prepare_result(room_id, "scenario_select", out_result);
    if (err != ESP_OK) {
        return err;
    }
    return scenehub_control_finalize_api_result_with_invalidation(out_result,
                                                                  gm_api_select_scenario(room_id, scenario_id),
                                                                  SCENEHUB_STATE_SLICE_ROOM_RUNTIME,
                                                                  room_id,
                                                                  "scenario_select");
}

esp_err_t scenehub_control_scenario_start(const char *source,
                                          const char *room_id,
                                          scenehub_control_result_t *out_result)
{
    (void)source;
    esp_err_t err = scenehub_control_prepare_result(room_id, "scenario_start", out_result);
    if (err != ESP_OK) {
        return err;
    }
    return scenehub_control_finalize_api_result_with_invalidation(out_result,
                                                                  gm_api_scenario_start(room_id),
                                                                  SCENEHUB_STATE_SLICE_ROOM_RUNTIME,
                                                                  room_id,
                                                                  "scenario_start");
}

esp_err_t scenehub_control_scenario_stop(const char *source,
                                         const char *room_id,
                                         scenehub_control_result_t *out_result)
{
    (void)source;
    esp_err_t err = scenehub_control_prepare_result(room_id, "scenario_stop", out_result);
    if (err != ESP_OK) {
        return err;
    }
    return scenehub_control_finalize_api_result_with_invalidation(out_result,
                                                                  gm_api_scenario_stop(room_id),
                                                                  SCENEHUB_STATE_SLICE_ROOM_RUNTIME,
                                                                  room_id,
                                                                  "scenario_stop");
}

esp_err_t scenehub_control_scenario_next(const char *source,
                                         const char *room_id,
                                         const char *branch_id,
                                         scenehub_control_result_t *out_result)
{
    (void)source;
    esp_err_t err = scenehub_control_prepare_result(room_id, "scenario_next", out_result);
    if (err != ESP_OK) {
        return err;
    }
    if (branch_id && branch_id[0]) {
        err = gm_api_scenario_next_branch(room_id, branch_id);
    } else {
        err = gm_api_scenario_next(room_id);
    }
    return scenehub_control_finalize_api_result_with_invalidation(out_result,
                                                                  err,
                                                                  SCENEHUB_STATE_SLICE_ROOM_RUNTIME,
                                                                  room_id,
                                                                  "scenario_next");
}

esp_err_t scenehub_control_scenario_approve(const char *source,
                                            const char *room_id,
                                            scenehub_control_result_t *out_result)
{
    (void)source;
    esp_err_t err = scenehub_control_prepare_result(room_id, "scenario_approve", out_result);
    if (err != ESP_OK) {
        return err;
    }
    return scenehub_control_finalize_api_result_with_invalidation(out_result,
                                                                  gm_api_scenario_approve(room_id),
                                                                  SCENEHUB_STATE_SLICE_ROOM_RUNTIME,
                                                                  room_id,
                                                                  "scenario_approve");
}

esp_err_t scenehub_control_scenario_reset(const char *source,
                                          const char *room_id,
                                          scenehub_control_result_t *out_result)
{
    (void)source;
    esp_err_t err = scenehub_control_prepare_result(room_id, "scenario_reset", out_result);
    if (err != ESP_OK) {
        return err;
    }
    return scenehub_control_finalize_api_result_with_invalidation(out_result,
                                                                  gm_api_scenario_reset(room_id),
                                                                  SCENEHUB_STATE_SLICE_ROOM_RUNTIME,
                                                                  room_id,
                                                                  "scenario_reset");
}

esp_err_t scenehub_control_validate_scenario(const char *source,
                                             const room_scenario_t *scenario,
                                             room_scenario_validation_report_t *out_report,
                                             scenehub_control_result_t *out_result)
{
    (void)source;
    esp_err_t err = scenehub_control_prepare_result(scenario ? scenario->room_id : "", "scenario_validate", out_result);
    if (err != ESP_OK) {
        return err;
    }
    if (!scenario || !out_report) {
        scenehub_control_fill_common_error(out_result, ESP_ERR_INVALID_ARG);
        return ESP_OK;
    }
    return scenehub_control_finalize_no_state_change_result(out_result,
                                                            scenehub_scenario_validate(scenario, out_report));
}

esp_err_t scenehub_control_save_scenario(const char *source,
                                         const room_scenario_t *scenario,
                                         scenehub_control_result_t *out_result)
{
    (void)source;
    esp_err_t err = scenehub_control_prepare_result(scenario ? scenario->room_id : "", "scenario_save", out_result);
    if (err != ESP_OK) {
        return err;
    }
    if (!scenario) {
        scenehub_control_fill_common_error(out_result, ESP_ERR_INVALID_ARG);
        return ESP_OK;
    }
    return scenehub_control_finalize_api_result_with_invalidation(out_result,
                                                                  room_scenario_add_and_save(scenario),
                                                                  SCENEHUB_STATE_SLICE_ROOM_SCENARIOS,
                                                                  scenario->room_id,
                                                                  "scenario_save");
}

esp_err_t scenehub_control_delete_scenario(const char *source,
                                           const char *scenario_id,
                                           scenehub_control_result_t *out_result)
{
    room_scenario_t scenario = {0};
    const char *room_id = "";
    (void)source;
    esp_err_t err = scenehub_control_prepare_result("", "scenario_delete", out_result);
    if (err != ESP_OK) {
        return err;
    }
    if (scenario_id && room_scenario_get(scenario_id, &scenario) == ESP_OK) {
        room_id = scenario.room_id;
    }
    return scenehub_control_finalize_api_result_with_invalidation(out_result,
                                                                  room_scenario_delete_and_save(scenario_id),
                                                                  SCENEHUB_STATE_SLICE_ROOM_SCENARIOS,
                                                                  room_id,
                                                                  "scenario_delete");
}

esp_err_t scenehub_control_import_scenarios(const char *source,
                                            cJSON *root,
                                            scenehub_control_result_t *out_result)
{
    (void)source;
    esp_err_t err = scenehub_control_prepare_result("", "scenario_import", out_result);
    if (err != ESP_OK) {
        return err;
    }
    return scenehub_control_finalize_api_result_with_invalidation(out_result,
                                                                  room_scenario_store_import_json_and_save(root),
                                                                  SCENEHUB_STATE_SLICE_ROOM_SCENARIOS,
                                                                  "",
                                                                  "scenario_import");
}

esp_err_t scenehub_control_load_scenarios(const char *source,
                                          scenehub_control_result_t *out_result)
{
    (void)source;
    esp_err_t err = scenehub_control_prepare_result("", "scenario_load", out_result);
    if (err != ESP_OK) {
        return err;
    }
    return scenehub_control_finalize_api_result_with_invalidation(out_result,
                                                                  room_scenario_store_load(),
                                                                  SCENEHUB_STATE_SLICE_ROOM_SCENARIOS,
                                                                  "",
                                                                  "scenario_load");
}

esp_err_t scenehub_control_save_scenarios_store(const char *source,
                                                scenehub_control_result_t *out_result)
{
    (void)source;
    esp_err_t err = scenehub_control_prepare_result("", "scenario_store_save", out_result);
    if (err != ESP_OK) {
        return err;
    }
    return scenehub_control_finalize_no_state_change_result(out_result, room_scenario_store_save());
}
