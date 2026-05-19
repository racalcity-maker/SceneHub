#include "scenehub_control_internal.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "gm_api.h"
#include "room_scenario.h"
#include "scenehub_scenario_validation.h"

static const cJSON *scenehub_control_scenario_payload_object(const cJSON *payload)
{
    const cJSON *scenario = cJSON_GetObjectItemCaseSensitive(payload, "scenario");
    if (cJSON_IsObject(scenario)) {
        return scenario;
    }
    return payload;
}

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

esp_err_t scenehub_control_validate_scenario_payload(const char *source,
                                                     const cJSON *payload,
                                                     char *out_scenario_id,
                                                     size_t out_scenario_id_size,
                                                     room_scenario_validation_report_t *out_report,
                                                     scenehub_control_result_t *out_result)
{
    room_scenario_t *scenario = NULL;
    esp_err_t err = ESP_OK;

    err = room_scenario_acquire_scratch(&scenario, NULL);
    if (err != ESP_OK) {
        if (scenehub_control_prepare_result("", "scenario_validate", out_result) == ESP_OK) {
            scenehub_control_fill_common_error(out_result, err);
        }
        return ESP_OK;
    }
    err = scenehub_control_validate_scenario_payload_into(source,
                                                         payload,
                                                         scenario,
                                                         out_scenario_id,
                                                         out_scenario_id_size,
                                                         out_report,
                                                         out_result);
    room_scenario_release_scratch();
    return err;
}

esp_err_t scenehub_control_validate_scenario_payload_into(
    const char *source,
    const cJSON *payload,
    room_scenario_t *scratch_scenario,
    char *out_scenario_id,
    size_t out_scenario_id_size,
    room_scenario_validation_report_t *out_report,
    scenehub_control_result_t *out_result)
{
    (void)source;
    esp_err_t err = scenehub_control_prepare_result("", "scenario_validate", out_result);
    if (err != ESP_OK) {
        return err;
    }
    if (out_scenario_id && out_scenario_id_size > 0) {
        out_scenario_id[0] = '\0';
    }
    if (!payload || !scratch_scenario || !out_report) {
        scenehub_control_fill_common_error(out_result, ESP_ERR_INVALID_ARG);
        return ESP_OK;
    }
    memset(scratch_scenario, 0, sizeof(*scratch_scenario));
    memset(out_report, 0, sizeof(*out_report));
    err = room_scenario_from_json(scenehub_control_scenario_payload_object(payload), scratch_scenario);
    if (err == ESP_OK && out_scenario_id && out_scenario_id_size > 0) {
        snprintf(out_scenario_id, out_scenario_id_size, "%s", scratch_scenario->id);
    }
    if (err == ESP_OK) {
        scenehub_control_copy(out_result->room_id, sizeof(out_result->room_id), scratch_scenario->room_id);
    }
    if (err == ESP_OK) {
        err = scenehub_scenario_validate(scratch_scenario, out_report);
    }
    return scenehub_control_finalize_no_state_change_result(out_result, err);
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
    return scenehub_control_finalize_api_result_with_invalidation(
        out_result,
        scenehub_control_persistence_enabled() ? room_scenario_add_and_save(scenario)
                                               : room_scenario_add(scenario),
        SCENEHUB_STATE_SLICE_ROOM_SCENARIOS,
        scenario->room_id,
        "scenario_save");
}

esp_err_t scenehub_control_save_scenario_payload(const char *source,
                                                 const cJSON *payload,
                                                 cJSON **out_scenario_json,
                                                 scenehub_control_result_t *out_result)
{
    (void)source;
    room_scenario_t *scenario = NULL;
    room_scenario_validation_report_t *report = NULL;
    cJSON *scenario_json = NULL;
    esp_err_t err = scenehub_control_prepare_result("", "scenario_save", out_result);
    if (err != ESP_OK) {
        return err;
    }
    if (out_scenario_json) {
        *out_scenario_json = NULL;
    }
    if (!payload) {
        scenehub_control_fill_common_error(out_result, ESP_ERR_INVALID_ARG);
        return ESP_OK;
    }

    err = room_scenario_acquire_scratch(&scenario, &report);
    if (err != ESP_OK) {
        scenehub_control_fill_common_error(out_result, err);
        return ESP_OK;
    }
    memset(scenario, 0, sizeof(*scenario));
    if (report) {
        memset(report, 0, sizeof(*report));
    }
    err = room_scenario_from_json(scenehub_control_scenario_payload_object(payload), scenario);
    if (err == ESP_OK && out_scenario_json) {
        scenario_json = cJSON_CreateObject();
        if (!scenario_json) {
            err = ESP_ERR_NO_MEM;
        } else {
            err = room_scenario_to_json(scenario, scenario_json);
        }
    }
    if (err == ESP_OK && report) {
        err = scenehub_scenario_validate(scenario, report);
    }
    if (err == ESP_OK) {
        scenehub_control_copy(out_result->room_id, sizeof(out_result->room_id), scenario->room_id);
        err = scenehub_control_persistence_enabled() ? room_scenario_add_and_save(scenario)
                                                     : room_scenario_add(scenario);
    }
    if (err == ESP_OK) {
        scenehub_control_finish_success_with_invalidation(out_result,
                                                          SCENEHUB_STATE_SLICE_ROOM_SCENARIOS,
                                                          scenario->room_id,
                                                          "scenario_save");
    } else {
        cJSON_Delete(scenario_json);
        scenario_json = NULL;
        scenehub_control_fill_common_error(out_result, err);
    }
    room_scenario_release_scratch();
    if (out_scenario_json) {
        *out_scenario_json = scenario_json;
    } else {
        cJSON_Delete(scenario_json);
    }
    return ESP_OK;
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
    return scenehub_control_finalize_api_result_with_invalidation(
        out_result,
        scenehub_control_persistence_enabled() ? room_scenario_delete_and_save(scenario_id)
                                               : room_scenario_delete(scenario_id),
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
