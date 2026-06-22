#include "scenehub_control_internal.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "gm_room_session.h"
#include "room_scenario.h"
#include "scenehub_scenario_validation.h"

static const char *TAG = "scenehub_scenarios";

static const cJSON *scenehub_control_scenario_payload_object(const cJSON *payload)
{
    const cJSON *scenario = cJSON_GetObjectItemCaseSensitive(payload, "scenario");
    if (cJSON_IsObject(scenario)) {
        return scenario;
    }
    return payload;
}

static esp_err_t scenehub_control_load_selected_scenario(const char *room_id,
                                                         room_scenario_t *scenario,
                                                         uint32_t *out_generation)
{
    gm_room_session_selected_view_t selected = {0};
    esp_err_t err = ESP_OK;

    if (!room_id || !room_id[0] || !scenario) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(scenario, 0, sizeof(*scenario));
    err = gm_room_session_get_selected_view(room_id, &selected);
    if (err != ESP_OK) {
        return err;
    }
    if (!selected.selected_scenario_id[0]) {
        return ESP_ERR_INVALID_STATE;
    }
    err = room_scenario_get(selected.selected_scenario_id, scenario);
    if (err != ESP_OK) {
        return err;
    }
    if (strcmp(scenario->room_id, room_id) != 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (out_generation) {
        *out_generation = room_scenario_generation();
    }
    return ESP_OK;
}

static void scenehub_control_scenario_dispatch_from_executor(
    gm_room_session_command_dispatch_result_t *out,
    const command_executor_dispatch_t *in)
{
    if (!out || !in) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->result_required = in->result_required;
    out->timeout_ms = in->timeout_ms;
    scenehub_control_copy(out->request_id, sizeof(out->request_id), in->request_id);
    scenehub_control_copy(out->source_id, sizeof(out->source_id), in->source_id);
    scenehub_control_copy(out->command, sizeof(out->command), in->command);
}

void scenehub_control_register_session_command_dispatcher(void)
{
    gm_room_session_set_command_plan_dispatcher(scenehub_control_dispatch_session_command_plan);
    gm_room_session_set_command_lifecycle_hooks(command_executor_on_event,
                                                command_executor_poll_timeouts,
                                                command_executor_next_timeout_deadline_ms,
                                                command_executor_cancel_request,
                                                command_executor_reset_pending);
}

esp_err_t scenehub_control_dispatch_session_command_plan(
    const char *source,
    gm_room_session_command_plan_t *plan)
{
    (void)source;
    scenehub_control_register_session_command_dispatcher();
    while (gm_room_session_command_plan_present(plan)) {
        room_scenario_device_command_t command = {0};
        command_executor_dispatch_t executor_dispatch = {0};
        gm_room_session_command_dispatch_result_t dispatch = {0};
        char error[96] = {0};
        const char *result_required_error = NULL;
        bool has_more = false;
        esp_err_t err = ESP_OK;

        err = gm_room_session_command_plan_next_command(plan,
                                                        &command,
                                                        &has_more,
                                                        error,
                                                        sizeof(error));
        if (err != ESP_OK) {
            if (err == ESP_ERR_NOT_FOUND || err == ESP_ERR_INVALID_STATE) {
                gm_room_session_command_plan_clear(plan);
                return ESP_OK;
            }
            (void)gm_room_session_command_plan_fail(plan,
                                                    error[0] ? error : "device_command_failed");
            return err;
        }

        if (plan->kind == GM_ROOM_SESSION_COMMAND_PLAN_SCENARIO_STEP_GROUP) {
            result_required_error = "device_command_group_result_required_unsupported";
        } else if (plan->kind == GM_ROOM_SESSION_COMMAND_PLAN_REACTIVE_ACTION_GROUP) {
            result_required_error = "reactive_group_result_required_unsupported";
        }
        err = scenehub_control_dispatch_scenario_command(command.device_id,
                                                         command.command_id,
                                                         command.params_json,
                                                         result_required_error,
                                                         &executor_dispatch,
                                                         error,
                                                         sizeof(error));
        if (err != ESP_OK) {
            ESP_LOGW(TAG,
                     "scenario command failed: device=%s command=%s err=%s code=%s",
                     command.device_id,
                     command.command_id,
                     esp_err_to_name(err),
                     error[0] ? error : "-");
            (void)gm_room_session_command_plan_fail(plan, error);
            return err;
        }

        scenehub_control_scenario_dispatch_from_executor(&dispatch, &executor_dispatch);
        err = gm_room_session_command_plan_apply_dispatch_result(plan, &dispatch, has_more);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t scenehub_control_select_scenario(const char *source,
                                           const char *room_id,
                                           const char *scenario_id,
                                           scenehub_control_result_t *out_result)
{
    room_scenario_t *scenario = NULL;
    (void)source;
    esp_err_t err = scenehub_control_prepare_result(room_id, "scenario_select", out_result);
    if (err != ESP_OK) {
        return err;
    }
    if (!room_id || !room_id[0] || !scenario_id || !scenario_id[0]) {
        scenehub_control_fill_common_error(out_result, ESP_ERR_INVALID_ARG);
        return ESP_OK;
    }
    err = scenehub_control_require_room(room_id);
    if (err == ESP_OK) {
        err = room_scenario_acquire_scratch(&scenario, NULL);
    }
    if (err == ESP_OK) {
        memset(scenario, 0, sizeof(*scenario));
        err = room_scenario_get(scenario_id, scenario);
    }
    if (err == ESP_OK && strcmp(scenario->room_id, room_id) != 0) {
        err = ESP_ERR_INVALID_STATE;
    }
    if (err == ESP_OK) {
        err = gm_room_session_select_scenario_prepared(room_id, scenario);
    }
    if (scenario) {
        room_scenario_release_scratch();
    }
    return scenehub_control_finalize_api_result_with_invalidation(out_result,
                                                                  err,
                                                                  SCENEHUB_STATE_SLICE_ROOM_RUNTIME,
                                                                  room_id,
                                                                  "scenario_select");
}

esp_err_t scenehub_control_scenario_start(const char *source,
                                          const char *room_id,
                                          scenehub_control_result_t *out_result)
{
    room_scenario_t *scenario = NULL;
    const gm_room_session_prepared_scenario_t *prepared_scenario = NULL;
    gm_room_session_command_plan_t plan = {0};
    uint32_t scenario_generation = 0;
    esp_err_t err = scenehub_control_prepare_result(room_id, "scenario_start", out_result);
    if (err != ESP_OK) {
        return err;
    }
    scenehub_control_register_session_command_dispatcher();
    err = scenehub_control_require_room(room_id);
    if (err == ESP_OK) {
        err = room_scenario_acquire_scratch(&scenario, NULL);
    }
    if (err == ESP_OK) {
        err = scenehub_control_load_selected_scenario(room_id, scenario, &scenario_generation);
    }
    if (err == ESP_OK) {
        err = scenehub_control_acquire_prepared_session_scenario(scenario, &prepared_scenario);
    }
    if (err == ESP_OK) {
        err = gm_room_session_scenario_start_prepared_plan(room_id,
                                                           scenario,
                                                           prepared_scenario,
                                                           scenario_generation,
                                                           &plan);
    }
    if (prepared_scenario) {
        scenehub_control_release_prepared_session_scenario();
    }
    if (scenario) {
        room_scenario_release_scratch();
    }
    if (err == ESP_OK) {
        err = scenehub_control_dispatch_session_command_plan(source, &plan);
    }
    return scenehub_control_finalize_api_result_with_invalidation(out_result,
                                                                  err,
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
    err = scenehub_control_require_room(room_id);
    if (err == ESP_OK) {
        err = gm_room_session_scenario_stop(room_id);
    }
    return scenehub_control_finalize_api_result_with_invalidation(out_result,
                                                                  err,
                                                                  SCENEHUB_STATE_SLICE_ROOM_RUNTIME,
                                                                  room_id,
                                                                  "scenario_stop");
}

esp_err_t scenehub_control_scenario_next(const char *source,
                                         const char *room_id,
                                         const char *branch_id,
                                         scenehub_control_result_t *out_result)
{
    gm_room_session_command_plan_t plan = {0};
    esp_err_t err = scenehub_control_prepare_result(room_id, "scenario_next", out_result);
    if (err != ESP_OK) {
        return err;
    }
    scenehub_control_register_session_command_dispatcher();
    err = scenehub_control_require_room(room_id);
    if (err == ESP_OK && branch_id && branch_id[0]) {
        err = gm_room_session_scenario_next_branch_plan(room_id, branch_id, &plan);
    } else if (err == ESP_OK) {
        err = gm_room_session_scenario_next_plan(room_id, &plan);
    }
    if (err == ESP_OK) {
        err = scenehub_control_dispatch_session_command_plan(source, &plan);
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
    gm_room_session_command_plan_t plan = {0};
    esp_err_t err = scenehub_control_prepare_result(room_id, "scenario_approve", out_result);
    if (err != ESP_OK) {
        return err;
    }
    scenehub_control_register_session_command_dispatcher();
    err = scenehub_control_require_room(room_id);
    if (err == ESP_OK) {
        err = gm_room_session_scenario_approve_plan(room_id, &plan);
    }
    if (err == ESP_OK) {
        err = scenehub_control_dispatch_session_command_plan(source, &plan);
    }
    return scenehub_control_finalize_api_result_with_invalidation(out_result,
                                                                  err,
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
    err = scenehub_control_require_room(room_id);
    if (err == ESP_OK) {
        err = gm_room_session_scenario_reset(room_id);
    }
    return scenehub_control_finalize_api_result_with_invalidation(out_result,
                                                                  err,
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
    room_scenario_t *scenario = NULL;
    const char *room_id = "";
    (void)source;
    esp_err_t err = scenehub_control_prepare_result("", "scenario_delete", out_result);
    if (err != ESP_OK) {
        return err;
    }
    if (scenario_id) {
        if (room_scenario_acquire_scratch(&scenario, NULL) == ESP_OK) {
            memset(scenario, 0, sizeof(*scenario));
            if (room_scenario_get(scenario_id, scenario) == ESP_OK) {
                room_id = scenario->room_id;
            }
            room_scenario_release_scratch();
        }
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
