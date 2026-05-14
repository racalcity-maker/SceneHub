#include "gm_room_session_projection_internal.h"
#include "gm_room_session_reactive_internal.h"
#include "gm_room_session_runner_internal.h"

#include <string.h>

#include "command_executor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "scenehub_command_result.h"

#define GM_SCENARIO_MAX_STEPS_PER_TICK 8

static const char *TAG = "gm_room_session";

static bool match_field(const char *expected, const char *actual)
{
    if (!expected || expected[0] == '\0') {
        return true;
    }
    return actual && strcmp(expected, actual) == 0;
}

typedef struct {
    const char *key;
    size_t key_len;
    const char *value;
    size_t value_len;
} gm_json_pair_t;

static bool gm_json_object_contains_expected(const char *expected_json, const char *actual_json);

static const char *gm_json_skip_ws(const char *p, const char *end)
{
    while (p && p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
        ++p;
    }
    return p;
}

static esp_err_t gm_json_skip_string(const char **cursor, const char *end)
{
    const char *p = cursor ? *cursor : NULL;
    if (!p || p >= end || *p != '"') {
        return ESP_ERR_INVALID_ARG;
    }
    for (++p; p < end; ++p) {
        if (*p == '\\') {
            if (p + 1 >= end) {
                return ESP_ERR_INVALID_ARG;
            }
            ++p;
            continue;
        }
        if (*p == '"') {
            *cursor = p + 1;
            return ESP_OK;
        }
    }
    return ESP_ERR_INVALID_ARG;
}

static esp_err_t gm_json_skip_value(const char **cursor, const char *end)
{
    const char *p = gm_json_skip_ws(cursor ? *cursor : NULL, end);
    if (!p || p >= end) {
        return ESP_ERR_INVALID_ARG;
    }
    if (*p == '"') {
        esp_err_t err = gm_json_skip_string(&p, end);
        if (err == ESP_OK) {
            *cursor = p;
        }
        return err;
    }
    if (*p == '{' || *p == '[') {
        char stack[8] = {0};
        int depth = 1;
        stack[0] = *p;
        for (++p; p < end; ++p) {
            if (*p == '"') {
                esp_err_t err = gm_json_skip_string(&p, end);
                if (err != ESP_OK) {
                    return err;
                }
                --p;
                continue;
            }
            if (*p == '{' || *p == '[') {
                if (depth >= (int)(sizeof(stack) / sizeof(stack[0]))) {
                    return ESP_ERR_INVALID_SIZE;
                }
                stack[depth++] = *p;
            } else if (*p == '}' || *p == ']') {
                char expected = stack[depth - 1] == '{' ? '}' : ']';
                if (*p != expected) {
                    return ESP_ERR_INVALID_ARG;
                }
                depth--;
                if (depth == 0) {
                    *cursor = p + 1;
                    return ESP_OK;
                }
            }
        }
        return ESP_ERR_INVALID_ARG;
    }
    while (p < end && *p != ',' && *p != '}') {
        ++p;
    }
    *cursor = p;
    return ESP_OK;
}

static esp_err_t gm_json_next_pair(const char **cursor, const char *end, gm_json_pair_t *out)
{
    const char *p = gm_json_skip_ws(cursor ? *cursor : NULL, end);
    const char *key_start = NULL;
    const char *value_end = NULL;
    if (!out || !p || p > end) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (p >= end) {
        return ESP_ERR_INVALID_ARG;
    }
    if (*p == '}') {
        *cursor = p + 1;
        return ESP_ERR_NOT_FOUND;
    }
    if (*p != '"') {
        return ESP_ERR_INVALID_ARG;
    }
    key_start = p + 1;
    for (++p; p < end; ++p) {
        if (*p == '\\') {
            return ESP_ERR_NOT_SUPPORTED;
        }
        if (*p == '"') {
            out->key = key_start;
            out->key_len = (size_t)(p - key_start);
            p++;
            break;
        }
    }
    if (!out->key) {
        return ESP_ERR_INVALID_ARG;
    }
    p = gm_json_skip_ws(p, end);
    if (p >= end || *p != ':') {
        return ESP_ERR_INVALID_ARG;
    }
    p++;
    p = gm_json_skip_ws(p, end);
    out->value = p;
    if (gm_json_skip_value(&p, end) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    value_end = p;
    out->value_len = (size_t)(value_end - out->value);
    p = gm_json_skip_ws(p, end);
    if (p < end && *p == ',') {
        *cursor = p + 1;
        return ESP_OK;
    }
    if (p < end && *p == '}') {
        *cursor = p;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_ARG;
}

static bool gm_json_key_view_equals(const gm_json_pair_t *pair, const char *key, size_t key_len)
{
    return pair && key && key_len == pair->key_len && strncmp(pair->key, key, pair->key_len) == 0;
}

static bool gm_json_is_empty_object(const char *json)
{
    const char *p = gm_json_skip_ws(json, json ? json + strlen(json) : NULL);
    const char *end = json ? json + strlen(json) : NULL;
    if (!p || p >= end || *p != '{') {
        return false;
    }
    p = gm_json_skip_ws(p + 1, end);
    return p < end && *p == '}';
}

static bool gm_json_find_pair_view(const char *json,
                                   const char *key,
                                   size_t key_len,
                                   const char **out_value,
                                   size_t *out_value_len)
{
    const char *p = gm_json_skip_ws(json, json ? json + strlen(json) : NULL);
    const char *end = json ? json + strlen(json) : NULL;
    if (!json || !key || key_len == 0 || !out_value || !out_value_len || !p || p >= end || *p != '{') {
        return false;
    }
    p++;
    for (;;) {
        gm_json_pair_t pair = {0};
        esp_err_t err = gm_json_next_pair(&p, end, &pair);
        if (err == ESP_ERR_NOT_FOUND) {
            return false;
        }
        if (err != ESP_OK) {
            return false;
        }
        if (gm_json_key_view_equals(&pair, key, key_len)) {
            *out_value = pair.value;
            *out_value_len = pair.value_len;
            return true;
        }
    }
}

static bool gm_json_values_match(const char *expected,
                                 size_t expected_len,
                                 const char *actual,
                                 size_t actual_len)
{
    const char *expected_trimmed = gm_json_skip_ws(expected, expected ? expected + expected_len : NULL);
    const char *actual_trimmed = gm_json_skip_ws(actual, actual ? actual + actual_len : NULL);
    const char *expected_end = expected ? expected + expected_len : NULL;
    const char *actual_end = actual ? actual + actual_len : NULL;
    while (expected_end && expected_end > expected_trimmed &&
           (expected_end[-1] == ' ' || expected_end[-1] == '\t' ||
            expected_end[-1] == '\r' || expected_end[-1] == '\n')) {
        --expected_end;
    }
    while (actual_end && actual_end > actual_trimmed &&
           (actual_end[-1] == ' ' || actual_end[-1] == '\t' ||
            actual_end[-1] == '\r' || actual_end[-1] == '\n')) {
        --actual_end;
    }
    if (!expected_trimmed || !actual_trimmed) {
        return false;
    }
    if ((size_t)(expected_end - expected_trimmed) >= 2 &&
        expected_trimmed[0] == '{' &&
        actual_end > actual_trimmed &&
        actual_trimmed[0] == '{') {
        char expected_copy[QUEST_PAYLOAD_MAX_LEN] = {0};
        char actual_copy[QUEST_PAYLOAD_MAX_LEN] = {0};
        size_t expected_copy_len = (size_t)(expected_end - expected_trimmed);
        size_t actual_copy_len = (size_t)(actual_end - actual_trimmed);
        if (expected_copy_len + 1 > sizeof(expected_copy) ||
            actual_copy_len + 1 > sizeof(actual_copy)) {
            return false;
        }
        memcpy(expected_copy, expected_trimmed, expected_copy_len);
        expected_copy[expected_copy_len] = '\0';
        memcpy(actual_copy, actual_trimmed, actual_copy_len);
        actual_copy[actual_copy_len] = '\0';
        return gm_json_object_contains_expected(expected_copy, actual_copy);
    }
    return (size_t)(expected_end - expected_trimmed) == (size_t)(actual_end - actual_trimmed) &&
           strncmp(expected_trimmed,
                   actual_trimmed,
                   (size_t)(expected_end - expected_trimmed)) == 0;
}

static bool gm_json_object_contains_expected(const char *expected_json, const char *actual_json)
{
    const char *p = gm_json_skip_ws(expected_json, expected_json ? expected_json + strlen(expected_json) : NULL);
    const char *end = expected_json ? expected_json + strlen(expected_json) : NULL;
    if (!expected_json || !actual_json) {
        return false;
    }
    if (gm_json_is_empty_object(expected_json)) {
        return true;
    }
    if (!p || p >= end || *p != '{') {
        return false;
    }
    p++;
    for (;;) {
        gm_json_pair_t pair = {0};
        const char *actual_value = NULL;
        size_t actual_value_len = 0;
        esp_err_t err = gm_json_next_pair(&p, end, &pair);
        if (err == ESP_ERR_NOT_FOUND) {
            return true;
        }
        if (err != ESP_OK) {
            return false;
        }
        if (!gm_json_find_pair_view(actual_json,
                                    pair.key,
                                    pair.key_len,
                                    &actual_value,
                                    &actual_value_len) ||
            !gm_json_values_match(pair.value, pair.value_len, actual_value, actual_value_len)) {
            return false;
        }
    }
}

static bool device_control_event_payload_matches(const gm_room_scenario_wait_event_match_t *expected,
                                                 const scenehub_event_t *message)
{
    if (!expected || !expected->device_id[0] || !expected->event_id[0] || !message) {
        return false;
    }
    if (!scenehub_event_is_device_control_event(message)) {
        return false;
    }
    if (!expected->match_json[0] || gm_json_is_empty_object(expected->match_json)) {
        return true;
    }
    if (!message->data.device_control.args_json[0]) {
        ESP_LOGW(TAG,
                 "device event args missing: device=%s event=%s action=%s source=%s",
                 expected->device_id,
                 expected->event_id,
                 message->data.device_control.action_id,
                 message->data.device_control.source);
        return false;
    }
    if (!gm_json_object_contains_expected(expected->match_json, message->data.device_control.args_json)) {
        ESP_LOGW(TAG,
                 "device event args mismatch: device=%s event=%s expected=%s actual=%s",
                 expected->device_id,
                 expected->event_id,
                 expected->match_json,
                 message->data.device_control.args_json);
        return false;
    }
    return true;
}

static bool wait_event_type_matches(const char *expected, const scenehub_event_t *message)
{
    if (!message) {
        return false;
    }
    if (match_field(expected, scenehub_event_type_to_string(message->type)) ||
        match_field(expected, message->topic) ||
        match_field(expected, message->payload)) {
        return true;
    }
    switch (message->payload_type) {
    case SCENEHUB_EVENT_PAYLOAD_DEVICE_STATUS:
        return match_field(expected, message->data.device_status.state) ||
               match_field(expected, message->data.device_status.health) ||
               match_field(expected, message->data.device_status.connectivity);
    case SCENEHUB_EVENT_PAYLOAD_DEVICE_RUNTIME:
        return match_field(expected, message->data.device_runtime.runtime_type) ||
               match_field(expected, message->data.device_runtime.state);
    case SCENEHUB_EVENT_PAYLOAD_DEVICE_CONTROL:
        return match_field(expected, message->data.device_control.action_id) ||
               match_field(expected, message->data.device_control.source);
    case SCENEHUB_EVENT_PAYLOAD_TEXT:
    default:
        return false;
    }
}

static int wait_event_match_index(const gm_room_session_t *session, const scenehub_event_t *message)
{
    const char *source_id = scenehub_event_source_id(message);
    if (!session || !message) {
        return -1;
    }
    if (session->wait_event_count > 0) {
        for (uint8_t i = 0; i < session->wait_event_count &&
                            i < ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS; ++i) {
            if (gm_room_session_wait_event_matches_message(&session->wait_events[i], message)) {
                if (scenehub_event_is_device_control_event(message)) {
                    ESP_LOGI(TAG,
                             "wait event matched: room=%s device=%s event=%s action=%s args=%s",
                             session->room_id,
                             session->wait_events[i].device_id,
                             session->wait_events[i].event_id,
                             message->data.device_control.action_id,
                             message->data.device_control.args_json);
                }
                return i;
            }
        }
        return -1;
    }
    return (wait_event_type_matches(session->wait_event_type, message) &&
            match_field(session->wait_source_id, source_id))
               ? 0
               : -1;
}

bool gm_room_session_wait_event_matches_message(const gm_room_scenario_wait_event_match_t *expected,
                                                const scenehub_event_t *message)
{
    const char *source_id = scenehub_event_source_id(message);
    bool source_matches = false;
    if (!expected || !message) {
        return false;
    }
    source_matches = match_field(expected->source_id, source_id);
    if (!source_matches && expected->device_id[0]) {
        source_matches = match_field(expected->device_id, source_id);
    }
    if (!wait_event_type_matches(expected->event_type, message)) {
        if (scenehub_event_is_device_control_event(message) &&
            source_matches) {
            ESP_LOGW(TAG,
                     "device event type mismatch: device=%s event=%s expected_type=%s action=%s source=%s",
                     expected->device_id,
                     expected->event_id,
                     expected->event_type,
                     message->data.device_control.action_id,
                     message->data.device_control.source);
        }
        return false;
    }
    if (!source_matches) {
        if (scenehub_event_is_device_control_event(message)) {
            ESP_LOGW(TAG,
                     "device event source mismatch: expected_source=%s expected_device=%s actual_source=%s action=%s",
                     expected->source_id,
                     expected->device_id,
                     source_id ? source_id : "",
                     message->data.device_control.action_id);
        }
        return false;
    }
    if (!expected->device_id[0] || !expected->event_id[0]) {
        return true;
    }
    return device_control_event_payload_matches(expected, message);
}

static bool wait_all_device_events_met(const gm_room_session_t *session)
{
    if (!session || session->wait_event_count == 0) {
        return false;
    }
    for (uint8_t i = 0; i < session->wait_event_count &&
                        i < ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS; ++i) {
        if (!session->wait_event_matched[i]) {
            return false;
        }
    }
    return true;
}

static bool command_result_is_success(const scenehub_event_t *message)
{
    return message && scenehub_command_result_is_success(message->payload);
}

static bool command_result_is_failure(const scenehub_event_t *message)
{
    return message && scenehub_command_result_is_failure(message->payload);
}

static bool command_result_is_pending(const scenehub_event_t *message)
{
    return message && scenehub_command_result_is_pending(message->payload);
}

static int command_result_wait_branch_index(const gm_room_session_t *session,
                                            const scenehub_event_t *message)
{
    const char *request_id = NULL;
    const char *source_id = NULL;

    if (!session || !message || !scenehub_event_is_device_control_result(message)) {
        return -1;
    }

    request_id = message->data.device_control.action_id;
    source_id = scenehub_event_source_id(message);
    if (!request_id || !request_id[0]) {
        return -1;
    }

    for (uint8_t branch_index = 0;
         branch_index < session->branch_runtime_count &&
         branch_index < ROOM_SCENARIO_MAX_BRANCHES;
         ++branch_index) {
        const gm_room_scenario_branch_runtime_t *branch = &session->branch_runtimes[branch_index];
        if (!branch->active ||
            branch->scenario_state != GM_ROOM_SCENARIO_WAITING ||
            branch->wait_type != GM_ROOM_SCENARIO_WAIT_DEVICE_COMMAND_RESULT) {
            continue;
        }
        if (strcmp(branch->wait_event_type, request_id) != 0) {
            continue;
        }
        if (!match_field(branch->wait_source_id, source_id)) {
            continue;
        }
        return branch_index;
    }

    return -1;
}

static bool branch_wait_matches_message(gm_room_session_t *session,
                                        gm_room_scenario_branch_runtime_t *branch,
                                        const scenehub_event_t *message)
{
    bool matches = false;
    if (!session || !branch || !message ||
        !branch->active ||
        branch->scenario_state != GM_ROOM_SCENARIO_WAITING ||
        (branch->wait_type != GM_ROOM_SCENARIO_WAIT_DEVICE_EVENT &&
         branch->wait_type != GM_ROOM_SCENARIO_WAIT_ANY_DEVICE_EVENT &&
         branch->wait_type != GM_ROOM_SCENARIO_WAIT_ALL_DEVICE_EVENTS &&
         branch->wait_type != GM_ROOM_SCENARIO_WAIT_DEVICE_COMMAND_RESULT)) {
        return false;
    }
    gm_room_session_scenario_branch_load_into_session(session, branch);
    matches = wait_event_match_index(session, message) >= 0;
    gm_room_session_scenario_branch_save_from_session(branch, session);
    return matches;
}

esp_err_t gm_room_session_scenario_on_event(const scenehub_event_t *message)
{
    uint32_t now_ms = gm_room_session_scenario_now_ms();
    bool matched_any = false;
    bool command_result_event = scenehub_event_is_device_control_result(message);
    gm_room_session_command_plan_t plan = {0};
    esp_err_t dispatch_err = ESP_OK;
    if (!message) {
        return ESP_ERR_INVALID_ARG;
    }
    command_executor_on_event(message);
    if (gm_room_session_sessions_lock() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }
    for (size_t i = 0; i < GM_SESSION_MAX_ROOMS; ++i) {
        gm_room_session_t *session = &g_gm_room_sessions[i];
        int targeted_branch_index = -1;
        uint8_t branch_start = 0;
        uint8_t branch_end = 0;
        if (!session->in_use || !session->running_scenario_valid) {
            continue;
        }
        bool reactive_matches[ROOM_SCENARIO_MAX_BRANCHES] = {0};
        uint8_t reactive_match_count = 0;
        if (command_result_event) {
            targeted_branch_index = command_result_wait_branch_index(session, message);
            if (targeted_branch_index < 0) {
                continue;
            }
            branch_start = (uint8_t)targeted_branch_index;
            branch_end = (uint8_t)(targeted_branch_index + 1);
        } else {
            branch_end = session->branch_runtime_count > ROOM_SCENARIO_MAX_BRANCHES
                             ? ROOM_SCENARIO_MAX_BRANCHES
                             : session->branch_runtime_count;
            for (uint8_t branch_index = 0; branch_index < branch_end; ++branch_index) {
                gm_room_scenario_branch_runtime_t *branch = &session->branch_runtimes[branch_index];
                if (branch->type != ROOM_SCENARIO_BRANCH_REACTIVE) {
                    continue;
                }
                if ((gm_room_session_reactive_v2_matches_event(session, branch, message) &&
                     branch->scenario_state == GM_ROOM_SCENARIO_WAITING &&
                     branch->wait_type == GM_ROOM_SCENARIO_WAIT_NONE) ||
                    branch_wait_matches_message(session, branch, message)) {
                    reactive_matches[branch_index] = true;
                    reactive_match_count++;
                }
            }
        }
        if (reactive_match_count > 1) {
            ESP_LOGW(TAG,
                     "reactive trigger conflict: room=%s event=%s source=%s matches=%u",
                     session->room_id,
                     scenehub_event_type_to_string(message->type),
                     scenehub_event_source_id(message),
                     (unsigned)reactive_match_count);
            matched_any = true;
        }
        for (uint8_t branch_index = branch_start; branch_index < branch_end; ++branch_index) {
            gm_room_scenario_branch_runtime_t *branch = &session->branch_runtimes[branch_index];
            if (reactive_match_count > 1 && reactive_matches[branch_index]) {
                continue;
            }
            if (!command_result_event &&
                gm_room_session_reactive_v2_matches_event(session, branch, message)) {
                esp_err_t err = gm_room_session_reactive_v2_fire_locked(session, branch, now_ms, &plan);
                if (err == ESP_OK) {
                    matched_any = true;
                    if (gm_room_session_command_plan_present(&plan)) {
                        gm_room_session_scenario_update_summary_from_branches_locked(session);
                        goto dispatch_planned_command;
                    }
                }
                continue;
            }
            if (!branch->active ||
                branch->scenario_state != GM_ROOM_SCENARIO_WAITING ||
                (branch->wait_type != GM_ROOM_SCENARIO_WAIT_DEVICE_EVENT &&
                 branch->wait_type != GM_ROOM_SCENARIO_WAIT_ANY_DEVICE_EVENT &&
                 branch->wait_type != GM_ROOM_SCENARIO_WAIT_ALL_DEVICE_EVENTS &&
                 branch->wait_type != GM_ROOM_SCENARIO_WAIT_DEVICE_COMMAND_RESULT)) {
                continue;
            }
            gm_room_session_scenario_branch_load_into_session(session, branch);
            int match_index = wait_event_match_index(session, message);
            if (match_index < 0) {
                gm_room_session_scenario_branch_save_from_session(branch, session);
                continue;
            }

            if (session->wait_type == GM_ROOM_SCENARIO_WAIT_DEVICE_COMMAND_RESULT) {
                if (command_result_is_pending(message)) {
                    matched_any = true;
                    gm_room_session_scenario_branch_save_from_session(branch, session);
                    continue;
                }
                if (command_result_is_failure(message)) {
                    if (gm_room_session_branch_is_reactive_v2(session, branch)) {
                        matched_any = true;
                        (void)gm_room_session_reactive_v2_handle_result_failure_locked(session,
                                                                                       branch,
                                                                                       message->payload,
                                                                                       now_ms,
                                                                                       &plan);
                        if (gm_room_session_command_plan_present(&plan)) {
                            gm_room_session_scenario_update_summary_from_branches_locked(session);
                            goto dispatch_planned_command;
                        }
                        continue;
                    }
                    scenario_set_error_locked(session, "device_command_result_failed");
                    matched_any = true;
                    gm_room_session_scenario_branch_save_from_session(branch, session);
                    continue;
                }
                if (!command_result_is_success(message)) {
                    gm_room_session_scenario_branch_save_from_session(branch, session);
                    continue;
                }
                if (gm_room_session_branch_is_reactive_v2(session, branch)) {
                    matched_any = true;
                    (void)gm_room_session_reactive_v2_handle_result_success_locked(session,
                                                                                   branch,
                                                                                   now_ms,
                                                                                   &plan);
                    if (gm_room_session_command_plan_present(&plan)) {
                        gm_room_session_scenario_update_summary_from_branches_locked(session);
                        goto dispatch_planned_command;
                    }
                    continue;
                }
            }
            if (session->wait_type == GM_ROOM_SCENARIO_WAIT_ALL_DEVICE_EVENTS) {
                for (uint8_t event_index = 0;
                     event_index < session->wait_event_count &&
                     event_index < ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS;
                     ++event_index) {
                    if (gm_room_session_wait_event_matches_message(&session->wait_events[event_index],
                                                                   message)) {
                        session->wait_event_matched[event_index] = true;
                    }
                }
                gm_room_session_mark_session_changed_locked(session);
                matched_any = true;
                if (!wait_all_device_events_met(session)) {
                    gm_room_session_scenario_branch_save_from_session(branch, session);
                    continue;
                }
            }
            session->current_step_index++;
            session->scenario_state = GM_ROOM_SCENARIO_RUNNING;
            gm_room_session_scenario_clear_wait_locked(session);
            gm_room_session_mark_session_changed_locked(session);
            matched_any = true;
            gm_room_session_scenario_branch_save_from_session(branch, session);
            (void)gm_room_session_execute_branch_locked(session,
                                                        branch,
                                                        now_ms,
                                                        GM_SCENARIO_MAX_STEPS_PER_TICK,
                                                        &plan);
            if (gm_room_session_command_plan_present(&plan)) {
                gm_room_session_scenario_update_summary_from_branches_locked(session);
                goto dispatch_planned_command;
            }
        }
        gm_room_session_scenario_update_summary_from_branches_locked(session);
    }
dispatch_planned_command:
    gm_room_session_sessions_unlock();
    if (gm_room_session_command_plan_present(&plan)) {
        dispatch_err = gm_room_session_dispatch_planned_command(&plan);
        if (dispatch_err != ESP_OK) {
            return dispatch_err;
        }
    }
    return matched_any ? ESP_OK : ESP_ERR_NOT_FOUND;
}

void gm_room_session_event_handler(const scenehub_event_t *message)
{
    scenehub_event_t copy = {0};
    BaseType_t queued = pdFALSE;

    if (!message) {
        return;
    }

    copy = *message;

    if (!s_event_queue) {
        return;
    }

    queued = xQueueSend(s_event_queue, &copy, pdMS_TO_TICKS(5));
    if (queued != pdTRUE) {
        if (scenehub_event_is_device_control(message)) {
            ESP_LOGW(TAG,
                     "event queue full, dropped: type=%s device=%s action=%s source=%s",
                     scenehub_event_type_to_string(message->type),
                     message->data.device_control.device_id,
                     message->data.device_control.action_id,
                     message->data.device_control.source);
        } else {
            ESP_LOGW(TAG,
                     "event queue full, dropped: type=%s topic=%s payload=%s",
                     scenehub_event_type_to_string(message->type),
                     message->topic,
                     message->payload);
        }
    }
}


void gm_room_session_event_task(void *ctx)
{
    (void)ctx;

    scenehub_event_t message = {0};

    while (true) {
        if (xQueueReceive(s_event_queue, &message, portMAX_DELAY) == pdTRUE) {
            esp_err_t err = gm_room_session_runtime_post_event(&message);
            if (err != ESP_OK) {
                if (scenehub_event_is_device_control(&message)) {
                    ESP_LOGW(TAG,
                             "runtime queue full, dropped: type=%s device=%s action=%s source=%s",
                             scenehub_event_type_to_string(message.type),
                             message.data.device_control.device_id,
                             message.data.device_control.action_id,
                             message.data.device_control.source);
                } else {
                    ESP_LOGW(TAG,
                             "runtime queue full, dropped: type=%s topic=%s payload=%s",
                             scenehub_event_type_to_string(message.type),
                             message.topic,
                             message.payload);
                }
            }
        }
    }
}
