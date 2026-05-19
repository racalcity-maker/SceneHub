#include "scenehub_control_internal.h"

#include <stdio.h>
#include <string.h>

#include "gm_api.h"
#include "gm_control.h"
#include "orch_room_view.h"
#include "orchestrator_timeline.h"
#include "scenehub_state.h"

static bool s_persistence_enabled = true;

void scenehub_control_set_persistence_enabled_for_test(bool enabled)
{
    s_persistence_enabled = enabled;
}

bool scenehub_control_persistence_enabled(void)
{
    return s_persistence_enabled;
}

void scenehub_control_copy(char *dst, size_t dst_size, const char *src)
{
    size_t len = 0;

    if (!dst || dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    if (!src) {
        return;
    }

    len = strlen(src);
    if (len >= dst_size) {
        len = dst_size - 1;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

esp_err_t scenehub_control_prepare_result(const char *room_id,
                                          const char *action_id,
                                          scenehub_control_result_t *out_result)
{
    if (!out_result) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_result, 0, sizeof(*out_result));
    out_result->status = SCENEHUB_CONTROL_STATUS_FAILED;
    out_result->err = ESP_FAIL;
    scenehub_control_copy(out_result->room_id, sizeof(out_result->room_id), room_id);
    scenehub_control_copy(out_result->action_id, sizeof(out_result->action_id), action_id);
    return ESP_OK;
}

void scenehub_control_set_result(scenehub_control_result_t *result,
                                 scenehub_control_status_t status,
                                 esp_err_t err,
                                 bool state_changed,
                                 const char *error_code,
                                 const char *message)
{
    if (!result) {
        return;
    }
    result->status = status;
    result->err = err;
    result->state_changed = state_changed;
    scenehub_control_copy(result->error_code, sizeof(result->error_code), error_code);
    scenehub_control_copy(result->message, sizeof(result->message), message);
}

void scenehub_control_finish_success_with_invalidation(scenehub_control_result_t *result,
                                                       scenehub_state_slice_t slice,
                                                       const char *target_id,
                                                       const char *reason)
{
    scenehub_control_set_result(result, SCENEHUB_CONTROL_STATUS_DONE, ESP_OK, true, "", "");
    scenehub_state_notify_invalidation(slice, target_id, reason);
}

void scenehub_control_finish_success_no_state_change(scenehub_control_result_t *result)
{
    scenehub_control_set_result(result, SCENEHUB_CONTROL_STATUS_DONE, ESP_OK, false, "", "");
}

void scenehub_control_fill_common_error(scenehub_control_result_t *result, esp_err_t err)
{
    if (!result) {
        return;
    }

    switch (err) {
    case ESP_ERR_INVALID_ARG:
        scenehub_control_set_result(result,
                                    SCENEHUB_CONTROL_STATUS_REJECTED,
                                    err,
                                    false,
                                    "invalid_request",
                                    "Invalid request");
        break;
    case ESP_ERR_NOT_FOUND:
        scenehub_control_set_result(result,
                                    SCENEHUB_CONTROL_STATUS_REJECTED,
                                    err,
                                    false,
                                    "room_not_found",
                                    "Room not found");
        break;
    case ESP_ERR_INVALID_STATE:
        scenehub_control_set_result(result,
                                    SCENEHUB_CONTROL_STATUS_REJECTED,
                                    err,
                                    false,
                                    "action_disabled",
                                    "Operation not allowed in current state");
        break;
    default:
        scenehub_control_set_result(result,
                                    SCENEHUB_CONTROL_STATUS_FAILED,
                                    err,
                                    false,
                                    "execution_failed",
                                    "Execution failed");
        break;
    }
}

void scenehub_control_log_timer(const char *source,
                                const char *room_id,
                                const char *title,
                                const char *details)
{
    (void)orchestrator_timeline_log(ORCH_TIMELINE_TYPE_TIMER_CHANGED,
                                    ORCH_TIMELINE_SEVERITY_INFO,
                                    (source && source[0]) ? source : "internal",
                                    room_id ? room_id : "",
                                    "",
                                    title ? title : "Timer changed",
                                    details ? details : "");
}

void scenehub_control_log_device_action(const char *source,
                                        const char *device_id,
                                        bool warning,
                                        const char *command_id)
{
    orchestrator_timeline_severity_t severity = ORCH_TIMELINE_SEVERITY_INFO;
    if (warning) {
        severity = ORCH_TIMELINE_SEVERITY_WARNING;
    }
    (void)orchestrator_timeline_log(ORCH_TIMELINE_TYPE_DEVICE_ACTION,
                                    severity,
                                    (source && source[0]) ? source : "internal",
                                    "",
                                    device_id ? device_id : "",
                                    "Quest device command",
                                    command_id ? command_id : "");
}

esp_err_t scenehub_control_finalize_api_result_with_invalidation(scenehub_control_result_t *result,
                                                                 esp_err_t err,
                                                                 scenehub_state_slice_t slice,
                                                                 const char *target_id,
                                                                 const char *reason)
{
    if (!result) {
        return ESP_ERR_INVALID_ARG;
    }
    if (err == ESP_OK) {
        scenehub_control_finish_success_with_invalidation(result, slice, target_id, reason);
        return ESP_OK;
    }
    scenehub_control_fill_common_error(result, err);
    return ESP_OK;
}

esp_err_t scenehub_control_finalize_no_state_change_result(scenehub_control_result_t *result,
                                                           esp_err_t err)
{
    if (!result) {
        return ESP_ERR_INVALID_ARG;
    }
    if (err == ESP_OK) {
        scenehub_control_finish_success_no_state_change(result);
        return ESP_OK;
    }
    scenehub_control_fill_common_error(result, err);
    return ESP_OK;
}

const char *scenehub_control_status_str(scenehub_control_status_t status)
{
    switch (status) {
    case SCENEHUB_CONTROL_STATUS_DONE:
        return "done";
    case SCENEHUB_CONTROL_STATUS_ACCEPTED:
        return "accepted";
    case SCENEHUB_CONTROL_STATUS_REJECTED:
        return "rejected";
    case SCENEHUB_CONTROL_STATUS_TIMEOUT:
        return "timeout";
    case SCENEHUB_CONTROL_STATUS_FAILED:
    default:
        return "failed";
    }
}

static esp_err_t scenehub_control_preflight_room_action(const char *room_id,
                                                        const char *action_id)
{
    orch_room_entry_t room = {0};
    esp_err_t err = ESP_OK;

    if (!room_id || !room_id[0] || !action_id || !action_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(action_id, "start_game") != 0) {
        return ESP_OK;
    }
    err = orchestrator_registry_get_room(room_id, &room);
    if (err != ESP_OK) {
        return err;
    }
    if (room.health == ORCH_HEALTH_FAULT) {
        return GM_CTRL_ERR_ROOM_UNHEALTHY;
    }
    return ESP_OK;
}

static bool scenehub_control_try_capture_start_game_validation_error(const char *room_id,
                                                                     const char *action_id,
                                                                     esp_err_t err,
                                                                     scenehub_control_result_t *out_result)
{
    gm_room_state_view_t state = {0};
    if (!out_result || err != ESP_ERR_INVALID_ARG || !room_id || !room_id[0] || !action_id ||
        strcmp(action_id, "start_game") != 0) {
        return false;
    }
    if (gm_api_get_room_state(room_id, &state) != ESP_OK) {
        return false;
    }
    if (!state.scenario_last_error[0]) {
        return false;
    }
    scenehub_control_set_result(out_result,
                                SCENEHUB_CONTROL_STATUS_REJECTED,
                                err,
                                false,
                                "scenario_invalid",
                                state.scenario_last_error);
    return true;
}

esp_err_t scenehub_control_execute_room_action(const char *source,
                                               const char *room_id,
                                               const char *action_id,
                                               scenehub_control_result_t *out_result)
{
    esp_err_t err = scenehub_control_prepare_result(room_id, action_id, out_result);
    if (err != ESP_OK) {
        return err;
    }

    err = scenehub_control_preflight_room_action(room_id, action_id);
    if (err == ESP_OK) {
        err = gm_control_execute_room_action_with_source(source, room_id, action_id);
    }
    switch (err) {
    case ESP_OK:
        scenehub_control_finish_success_with_invalidation(out_result,
                                                          SCENEHUB_STATE_SLICE_ROOM_RUNTIME,
                                                          room_id,
                                                          "room_action");
        return ESP_OK;
    case ESP_ERR_INVALID_ARG:
        if (scenehub_control_try_capture_start_game_validation_error(room_id,
                                                                     action_id,
                                                                     err,
                                                                     out_result)) {
            return ESP_OK;
        }
        scenehub_control_fill_common_error(out_result, err);
        return ESP_OK;
    case GM_CTRL_ERR_ROOM_NOT_FOUND:
        scenehub_control_set_result(out_result,
                                    SCENEHUB_CONTROL_STATUS_REJECTED,
                                    err,
                                    false,
                                    "room_not_found",
                                    "Room not found");
        return ESP_OK;
    case GM_CTRL_ERR_ACTION_NOT_FOUND:
        scenehub_control_set_result(out_result,
                                    SCENEHUB_CONTROL_STATUS_REJECTED,
                                    err,
                                    false,
                                    "action_not_found",
                                    "Action not found");
        return ESP_OK;
    case GM_CTRL_ERR_ACTION_DISABLED:
        scenehub_control_set_result(out_result,
                                    SCENEHUB_CONTROL_STATUS_REJECTED,
                                    err,
                                    false,
                                    "action_disabled",
                                    "Action disabled");
        return ESP_OK;
    case GM_CTRL_ERR_NOT_SUPPORTED:
        scenehub_control_set_result(out_result,
                                    SCENEHUB_CONTROL_STATUS_REJECTED,
                                    err,
                                    false,
                                    "not_supported",
                                    "Action not supported");
        return ESP_OK;
    case GM_CTRL_ERR_ROOM_UNHEALTHY:
        scenehub_control_set_result(out_result,
                                    SCENEHUB_CONTROL_STATUS_REJECTED,
                                    err,
                                    false,
                                    "room_unhealthy",
                                    "Room has active device or system issues");
        return ESP_OK;
    default:
        scenehub_control_set_result(out_result,
                                    SCENEHUB_CONTROL_STATUS_FAILED,
                                    err,
                                    false,
                                    "execution_failed",
                                    "Room action execution failed");
        return ESP_OK;
    }
}

esp_err_t scenehub_control_timer_start(const char *source,
                                       const char *room_id,
                                       uint32_t duration_ms,
                                       scenehub_control_result_t *out_result)
{
    char details[64] = {0};
    esp_err_t err = scenehub_control_prepare_result(room_id, "timer_start", out_result);
    if (err != ESP_OK) {
        return err;
    }

    err = gm_api_timer_start(room_id, duration_ms);
    if (err != ESP_OK) {
        scenehub_control_fill_common_error(out_result, err);
        return ESP_OK;
    }

    snprintf(details, sizeof(details), "duration_ms=%lu", (unsigned long)duration_ms);
    scenehub_control_log_timer(source, room_id, "Timer started", details);
    scenehub_control_finish_success_with_invalidation(out_result,
                                                      SCENEHUB_STATE_SLICE_ROOM_RUNTIME,
                                                      room_id,
                                                      "timer_start");
    return ESP_OK;
}

esp_err_t scenehub_control_timer_pause(const char *source,
                                       const char *room_id,
                                       scenehub_control_result_t *out_result)
{
    esp_err_t err = scenehub_control_prepare_result(room_id, "timer_pause", out_result);
    if (err != ESP_OK) {
        return err;
    }

    err = gm_api_timer_pause(room_id);
    if (err != ESP_OK) {
        scenehub_control_fill_common_error(out_result, err);
        return ESP_OK;
    }

    scenehub_control_log_timer(source, room_id, "Timer paused", "");
    scenehub_control_finish_success_with_invalidation(out_result,
                                                      SCENEHUB_STATE_SLICE_ROOM_RUNTIME,
                                                      room_id,
                                                      "timer_pause");
    return ESP_OK;
}

esp_err_t scenehub_control_timer_resume(const char *source,
                                        const char *room_id,
                                        scenehub_control_result_t *out_result)
{
    esp_err_t err = scenehub_control_prepare_result(room_id, "timer_resume", out_result);
    if (err != ESP_OK) {
        return err;
    }

    err = gm_api_timer_resume(room_id);
    if (err != ESP_OK) {
        scenehub_control_fill_common_error(out_result, err);
        return ESP_OK;
    }

    scenehub_control_log_timer(source, room_id, "Timer resumed", "");
    scenehub_control_finish_success_with_invalidation(out_result,
                                                      SCENEHUB_STATE_SLICE_ROOM_RUNTIME,
                                                      room_id,
                                                      "timer_resume");
    return ESP_OK;
}

esp_err_t scenehub_control_timer_reset(const char *source,
                                       const char *room_id,
                                       bool has_duration,
                                       uint32_t duration_ms,
                                       scenehub_control_result_t *out_result)
{
    char details[32] = {0};
    esp_err_t err = scenehub_control_prepare_result(room_id, "timer_reset", out_result);
    if (err != ESP_OK) {
        return err;
    }

    err = gm_api_timer_reset(room_id, has_duration, duration_ms);
    if (err != ESP_OK) {
        scenehub_control_fill_common_error(out_result, err);
        return ESP_OK;
    }

    if (has_duration) {
        snprintf(details, sizeof(details), "%lu", (unsigned long)duration_ms);
    }
    scenehub_control_log_timer(source, room_id, "Timer reset", details);
    scenehub_control_finish_success_with_invalidation(out_result,
                                                      SCENEHUB_STATE_SLICE_ROOM_RUNTIME,
                                                      room_id,
                                                      "timer_reset");
    return ESP_OK;
}

esp_err_t scenehub_control_timer_add(const char *source,
                                     const char *room_id,
                                     int32_t delta_ms,
                                     scenehub_control_result_t *out_result)
{
    char details[32] = {0};
    esp_err_t err = scenehub_control_prepare_result(room_id, "timer_add", out_result);
    if (err != ESP_OK) {
        return err;
    }

    err = gm_api_timer_add(room_id, delta_ms);
    if (err != ESP_OK) {
        scenehub_control_fill_common_error(out_result, err);
        return ESP_OK;
    }

    snprintf(details, sizeof(details), "%ld", (long)delta_ms);
    scenehub_control_log_timer(source, room_id, "Timer adjusted", details);
    scenehub_control_finish_success_with_invalidation(out_result,
                                                      SCENEHUB_STATE_SLICE_ROOM_RUNTIME,
                                                      room_id,
                                                      "timer_add");
    return ESP_OK;
}

esp_err_t scenehub_control_session_finish(const char *source,
                                          const char *room_id,
                                          scenehub_control_result_t *out_result)
{
    esp_err_t err = scenehub_control_prepare_result(room_id, "session_finish", out_result);
    if (err != ESP_OK) {
        return err;
    }

    err = gm_api_room_session_finish(room_id);
    if (err != ESP_OK) {
        scenehub_control_fill_common_error(out_result, err);
        return ESP_OK;
    }

    scenehub_control_log_timer(source, room_id, "Session finished", "");
    scenehub_control_finish_success_with_invalidation(out_result,
                                                      SCENEHUB_STATE_SLICE_ROOM_RUNTIME,
                                                      room_id,
                                                      "session_finish");
    return ESP_OK;
}

esp_err_t scenehub_control_hint_send(const char *source,
                                     const char *room_id,
                                     const char *message,
                                     scenehub_control_result_t *out_result)
{
    (void)source;
    esp_err_t err = scenehub_control_prepare_result(room_id, "hint_send", out_result);
    if (err != ESP_OK) {
        return err;
    }

    err = gm_api_hint_send(room_id, message);
    if (err != ESP_OK) {
        scenehub_control_fill_common_error(out_result, err);
        return ESP_OK;
    }

    scenehub_control_finish_success_with_invalidation(out_result,
                                                      SCENEHUB_STATE_SLICE_ROOM_RUNTIME,
                                                      room_id,
                                                      "hint_send");
    return ESP_OK;
}

esp_err_t scenehub_control_hint_clear(const char *source,
                                      const char *room_id,
                                      scenehub_control_result_t *out_result)
{
    (void)source;
    esp_err_t err = scenehub_control_prepare_result(room_id, "hint_clear", out_result);
    if (err != ESP_OK) {
        return err;
    }

    err = gm_api_hint_clear(room_id);
    if (err != ESP_OK) {
        scenehub_control_fill_common_error(out_result, err);
        return ESP_OK;
    }

    scenehub_control_finish_success_with_invalidation(out_result,
                                                      SCENEHUB_STATE_SLICE_ROOM_RUNTIME,
                                                      room_id,
                                                      "hint_clear");
    return ESP_OK;
}
