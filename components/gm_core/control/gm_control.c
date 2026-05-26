#include "gm_control.h"

#include <stdio.h>
#include <string.h>

#include "gm_api.h"
#include "orchestrator_audit.h"
#include "orchestrator_timeline.h"

typedef struct {
    const char *action_id;
    const char *label;
} gm_room_action_static_t;

static const gm_room_action_static_t k_room_actions[GM_ROOM_ACTION_COUNT] = {
    {.action_id = "start_game", .label = "Start game"},
    {.action_id = "stop_game", .label = "Stop game"},
    {.action_id = "reset_game", .label = "Reset game"},
    {.action_id = "reset_room_timer", .label = "Reset room timer"},
    {.action_id = "pause_room_timer", .label = "Pause room timer"},
    {.action_id = "resume_room_timer", .label = "Resume room timer"},
    {.action_id = "finish_room_session", .label = "Finish room session"},
    {.action_id = "clear_room_hint", .label = "Clear room hint"},
};

static const char *gm_ctrl_error_code(esp_err_t err)
{
    switch (err) {
    case ESP_OK:
        return "ok";
    case ESP_ERR_INVALID_ARG:
        return "invalid_request";
    case GM_CTRL_ERR_ROOM_NOT_FOUND:
        return "room_not_found";
    case GM_CTRL_ERR_ACTION_NOT_FOUND:
        return "action_not_found";
    case GM_CTRL_ERR_ACTION_DISABLED:
        return "action_disabled";
    case GM_CTRL_ERR_NOT_SUPPORTED:
        return "not_supported";
    case GM_CTRL_ERR_ROOM_UNHEALTHY:
        return "room_unhealthy";
    case GM_CTRL_ERR_EXECUTION_FAILED:
    default:
        return "execution_failed";
    }
}

static void gm_ctrl_copy(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    if (!src) {
        return;
    }
    snprintf(dst, dst_size, "%s", src);
}

static esp_err_t gm_ctrl_fill_actions_from_state(const gm_room_state_view_t *state,
                                                 gm_room_action_desc_t *out_actions,
                                                 size_t max_actions,
                                                 size_t *out_count)
{
    if (!state || !out_actions || !out_count || max_actions == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t emit = GM_ROOM_ACTION_COUNT;
    if (emit > max_actions) {
        emit = max_actions;
    }

    for (size_t i = 0; i < emit; ++i) {
        gm_room_action_desc_t *dst = &out_actions[i];
        memset(dst, 0, sizeof(*dst));
        gm_ctrl_copy(dst->action_id, sizeof(dst->action_id), k_room_actions[i].action_id);
        gm_ctrl_copy(dst->label, sizeof(dst->label), k_room_actions[i].label);
        switch (i) {
        case 0:
            dst->enabled = state->selected_profile_id[0] &&
                           state->session_state != GM_SESSION_RUNNING;
            break;
        case 1:
            dst->enabled = state->session_present &&
                           state->session_state != GM_SESSION_FINISHED &&
                           state->selected_profile_id[0];
            break;
        case 2:
            dst->enabled = state->session_present &&
                           (state->selected_profile_id[0] || state->duration_ms > 0);
            break;
        case 3:
            dst->enabled = state->duration_ms > 0;
            break;
        case 4:
            dst->enabled = state->timer_state == GM_TIMER_RUNNING;
            break;
        case 5:
            dst->enabled = state->timer_state == GM_TIMER_PAUSED;
            break;
        case 6:
            dst->enabled = state->session_present &&
                           state->session_state != GM_SESSION_FINISHED &&
                           !state->selected_profile_id[0];
            break;
        case 7:
            dst->enabled = state->hint_active;
            break;
        default:
            dst->enabled = false;
            break;
        }
    }
    *out_count = emit;
    return ESP_OK;
}

static const gm_room_action_desc_t *gm_ctrl_find_action(const gm_room_action_desc_t *actions,
                                                        size_t count,
                                                        const char *action_id)
{
    if (!actions || !action_id || !action_id[0]) {
        return NULL;
    }
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(actions[i].action_id, action_id) == 0) {
            return &actions[i];
        }
    }
    return NULL;
}

static esp_err_t gm_ctrl_dispatch_room_action(const char *room_id, const char *action_id)
{
    if (!room_id || !room_id[0] || !action_id || !action_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(action_id, "start_game") == 0) {
        return gm_api_game_start(room_id);
    }
    if (strcmp(action_id, "stop_game") == 0) {
        return gm_api_game_stop(room_id);
    }
    if (strcmp(action_id, "reset_game") == 0) {
        return gm_api_game_reset(room_id);
    }
    if (strcmp(action_id, "reset_room_timer") == 0) {
        return gm_api_timer_reset(room_id, false, 0);
    }
    if (strcmp(action_id, "pause_room_timer") == 0) {
        return gm_api_timer_pause(room_id);
    }
    if (strcmp(action_id, "resume_room_timer") == 0) {
        return gm_api_timer_resume(room_id);
    }
    if (strcmp(action_id, "finish_room_session") == 0) {
        return gm_api_room_session_finish(room_id);
    }
    if (strcmp(action_id, "clear_room_hint") == 0) {
        return gm_api_hint_clear(room_id);
    }
    return ESP_ERR_NOT_SUPPORTED;
}

static void gm_ctrl_audit(const char *source, const char *room_id, const char *action_id, esp_err_t result)
{
    const char *audit_source = (source && source[0]) ? source : "internal";
    const char *audit_room = (room_id && room_id[0]) ? room_id : "-";
    const char *audit_action = (action_id && action_id[0]) ? action_id : "-";
    const char *error_code = gm_ctrl_error_code(result);
    const bool timer_action = strstr(audit_action, "timer") != NULL ||
                              strstr(audit_action, "session") != NULL ||
                              strstr(audit_action, "game") != NULL;
    (void)orchestrator_audit_log_device_action(audit_source,
                                               audit_room,
                                               audit_action,
                                               "",
                                               result == ESP_OK,
                                               error_code);
    (void)orchestrator_timeline_log(result == ESP_OK ? (timer_action ? ORCH_TIMELINE_TYPE_TIMER_CHANGED : ORCH_TIMELINE_TYPE_EVENT) : ORCH_TIMELINE_TYPE_ACTION_FAILED,
                                    result == ESP_OK ? ORCH_TIMELINE_SEVERITY_INFO : ORCH_TIMELINE_SEVERITY_ERROR,
                                    audit_source,
                                    audit_room,
                                    "",
                                    "",
                                    result == ESP_OK ? "Room action" : "Room action failed",
                                    audit_action);
}

esp_err_t gm_control_list_room_actions(const char *room_id,
                                       gm_room_action_desc_t *out_actions,
                                       size_t max_actions,
                                       size_t *out_count)
{
    gm_room_state_view_t state = {0};
    esp_err_t err;
    if (!room_id || !room_id[0] || !out_actions || max_actions == 0 || !out_count) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;

    err = gm_api_get_room_state(room_id, &state);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NOT_FOUND) {
            return GM_CTRL_ERR_ROOM_NOT_FOUND;
        }
        if (err == ESP_ERR_INVALID_ARG) {
            return ESP_ERR_INVALID_ARG;
        }
        return GM_CTRL_ERR_EXECUTION_FAILED;
    }

    return gm_ctrl_fill_actions_from_state(&state, out_actions, max_actions, out_count);
}

esp_err_t gm_control_execute_room_action_with_source(const char *source,
                                                     const char *room_id,
                                                     const char *action_id)
{
    gm_room_action_desc_t actions[GM_ROOM_ACTION_COUNT] = {0};
    size_t action_count = 0;
    const gm_room_action_desc_t *action = NULL;
    esp_err_t err;

    if (!room_id || !room_id[0] || !action_id || !action_id[0]) {
        gm_ctrl_audit(source, room_id, action_id, ESP_ERR_INVALID_ARG);
        return ESP_ERR_INVALID_ARG;
    }

    err = gm_control_list_room_actions(room_id, actions, GM_ROOM_ACTION_COUNT, &action_count);
    if (err != ESP_OK) {
        gm_ctrl_audit(source, room_id, action_id, err);
        return err;
    }
    action = gm_ctrl_find_action(actions, action_count, action_id);
    if (!action) {
        err = GM_CTRL_ERR_ACTION_NOT_FOUND;
        gm_ctrl_audit(source, room_id, action_id, err);
        return err;
    }
    if (!action->enabled) {
        err = GM_CTRL_ERR_ACTION_DISABLED;
        gm_ctrl_audit(source, room_id, action_id, err);
        return err;
    }

    err = gm_ctrl_dispatch_room_action(room_id, action_id);
    if (err == ESP_OK) {
        gm_ctrl_audit(source, room_id, action_id, ESP_OK);
        return ESP_OK;
    }
    if (err == ESP_ERR_NOT_FOUND) {
        err = GM_CTRL_ERR_ROOM_NOT_FOUND;
    } else if (err == ESP_ERR_INVALID_STATE) {
        err = GM_CTRL_ERR_ACTION_DISABLED;
    } else if (err == ESP_ERR_NOT_SUPPORTED) {
        err = GM_CTRL_ERR_NOT_SUPPORTED;
    } else if (err == ESP_ERR_INVALID_ARG) {
    } else {
        err = GM_CTRL_ERR_EXECUTION_FAILED;
    }
    gm_ctrl_audit(source, room_id, action_id, err);
    return err;
}

esp_err_t gm_control_execute_room_action(const char *room_id, const char *action_id)
{
    return gm_control_execute_room_action_with_source("internal", room_id, action_id);
}
