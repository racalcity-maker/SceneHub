#include "gm_room_session_wait_internal.h"

#include <string.h>

#include "quest_common_utils.h"

void scenario_branch_clear_wait_fields(gm_room_scenario_branch_runtime_t *branch)
{
    if (!branch) {
        return;
    }
    if (branch->wait_type == GM_ROOM_SCENARIO_WAIT_DEVICE_COMMAND_RESULT &&
        branch->wait_event_type[0]) {
        gm_room_session_defer_cancel_request_locked(branch->wait_event_type);
    }
    branch->wait_type = GM_ROOM_SCENARIO_WAIT_NONE;
    branch->wait_until_ms = 0;
    branch->wait_started_at_ms = 0;
    branch->wait_event_type[0] = '\0';
    branch->wait_source_id[0] = '\0';
    memset(branch->wait_events, 0, sizeof(branch->wait_events));
    memset(branch->wait_event_matched, 0, sizeof(branch->wait_event_matched));
    branch->wait_event_count = 0;
    memset(branch->wait_flags, 0, sizeof(branch->wait_flags));
    branch->wait_flag_count = 0;
    branch->wait_operator_prompt[0] = '\0';
    branch->wait_operator_label[0] = '\0';
    branch->wait_operator_skip_allowed = false;
    branch->wait_operator_skip_label[0] = '\0';
}

esp_err_t gm_room_session_expand_prepared_wait_resolution(
    const gm_room_session_prepared_scenario_t *prepared_scenario,
    const gm_room_session_prepared_event_resolution_t *prepared_resolution,
    gm_room_session_wait_resolution_t *out)
{
    if (!prepared_scenario || !prepared_resolution || !out ||
        !prepared_resolution->present || prepared_resolution->match_count == 0 ||
        prepared_resolution->match_count > ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->present = true;
    out->match_count = prepared_resolution->match_count;
    for (uint8_t i = 0; i < prepared_resolution->match_count; ++i) {
        uint16_t index = prepared_resolution->event_ref_indices[i];
        if (index >= prepared_scenario->event_ref_count ||
            index >= GM_ROOM_SESSION_PREPARED_EVENT_REF_MAX) {
            memset(out, 0, sizeof(*out));
            return ESP_ERR_INVALID_ARG;
        }
        out->matches[i] = prepared_scenario->event_refs[index].match;
    }
    return ESP_OK;
}

esp_err_t scenario_enter_wait_device_event_locked(gm_room_session_t *session,
                                                  const room_scenario_wait_device_event_t *wait,
                                                  const gm_room_session_wait_resolution_t *resolution,
                                                  uint32_t now_ms)
{
    const gm_room_scenario_wait_event_match_t *match = NULL;
    if (!session || !wait || !resolution || !resolution->present || resolution->match_count != 1) {
        return ESP_ERR_INVALID_ARG;
    }
    match = &resolution->matches[0];
    session->scenario_state = GM_ROOM_SCENARIO_WAITING;
    gm_room_session_scenario_clear_wait_locked(session);
    session->wait_type = GM_ROOM_SCENARIO_WAIT_DEVICE_EVENT;
    session->wait_started_at_ms = now_ms;
    session->wait_until_ms = wait->timeout_ms > 0 ? now_ms + wait->timeout_ms : 0;
    quest_str_copy(session->wait_event_type,
                   sizeof(session->wait_event_type),
                   match->event_type);
    quest_str_copy(session->wait_source_id,
                   sizeof(session->wait_source_id),
                   match->source_id);
    session->wait_event_count = 1;
    quest_str_copy(session->wait_events[0].device_id,
                   sizeof(session->wait_events[0].device_id),
                   match->device_id);
    quest_str_copy(session->wait_events[0].event_id,
                   sizeof(session->wait_events[0].event_id),
                   match->event_id);
    quest_str_copy(session->wait_events[0].event_type,
                   sizeof(session->wait_events[0].event_type),
                   match->event_type);
    quest_str_copy(session->wait_events[0].source_id,
                   sizeof(session->wait_events[0].source_id),
                   match->source_id);
    quest_str_copy(session->wait_events[0].match_json,
                   sizeof(session->wait_events[0].match_json),
                   match->match_json);
    gm_room_session_mark_session_changed_locked(session);
    return ESP_OK;
}

esp_err_t scenario_enter_wait_any_device_event_locked(
    gm_room_session_t *session,
    const room_scenario_wait_any_device_event_t *wait_any,
    const gm_room_session_wait_resolution_t *resolution,
    uint32_t now_ms)
{
    if (!resolution || !resolution->present || resolution->match_count != wait_any->event_count) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!session || !wait_any || wait_any->event_count == 0 ||
        wait_any->event_count > ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS) {
        return ESP_ERR_INVALID_ARG;
    }
    session->scenario_state = GM_ROOM_SCENARIO_WAITING;
    gm_room_session_scenario_clear_wait_locked(session);
    session->wait_type = GM_ROOM_SCENARIO_WAIT_ANY_DEVICE_EVENT;
    session->wait_started_at_ms = now_ms;
    session->wait_until_ms = wait_any->timeout_ms > 0 ? now_ms + wait_any->timeout_ms : 0;
    session->wait_event_count = wait_any->event_count;
    for (uint8_t i = 0; i < wait_any->event_count; ++i) {
        session->wait_events[i] = resolution->matches[i];
    }
    quest_str_copy(session->wait_event_type,
                   sizeof(session->wait_event_type),
                   session->wait_events[0].event_type);
    quest_str_copy(session->wait_source_id,
                   sizeof(session->wait_source_id),
                   session->wait_events[0].source_id);
    gm_room_session_mark_session_changed_locked(session);
    return ESP_OK;
}

esp_err_t scenario_enter_wait_all_device_events_locked(
    gm_room_session_t *session,
    const room_scenario_wait_all_device_events_t *wait_all,
    const gm_room_session_wait_resolution_t *resolution,
    uint32_t now_ms)
{
    if (!resolution || !resolution->present || resolution->match_count != wait_all->event_count) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!session || !wait_all || wait_all->event_count == 0 ||
        wait_all->event_count > ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS) {
        return ESP_ERR_INVALID_ARG;
    }
    session->scenario_state = GM_ROOM_SCENARIO_WAITING;
    gm_room_session_scenario_clear_wait_locked(session);
    session->wait_type = GM_ROOM_SCENARIO_WAIT_ALL_DEVICE_EVENTS;
    session->wait_started_at_ms = now_ms;
    session->wait_until_ms = wait_all->timeout_ms > 0 ? now_ms + wait_all->timeout_ms : 0;
    session->wait_event_count = wait_all->event_count;
    for (uint8_t i = 0; i < wait_all->event_count; ++i) {
        session->wait_events[i] = resolution->matches[i];
        session->wait_event_matched[i] = false;
    }
    quest_str_copy(session->wait_event_type,
                   sizeof(session->wait_event_type),
                   session->wait_events[0].event_type);
    quest_str_copy(session->wait_source_id,
                   sizeof(session->wait_source_id),
                   session->wait_events[0].source_id);
    gm_room_session_mark_session_changed_locked(session);
    return ESP_OK;
}

esp_err_t scenario_enter_wait_flags_locked(gm_room_session_t *session,
                                           const room_scenario_wait_flags_t *wait_flags,
                                           uint32_t now_ms)
{
    if (!session || !wait_flags || wait_flags->flag_count == 0 ||
        wait_flags->flag_count > ROOM_SCENARIO_WAIT_FLAGS_MAX_FLAGS) {
        return ESP_ERR_INVALID_ARG;
    }
    session->scenario_state = GM_ROOM_SCENARIO_WAITING;
    gm_room_session_scenario_clear_wait_locked(session);
    session->wait_type = GM_ROOM_SCENARIO_WAIT_FLAGS;
    session->wait_started_at_ms = now_ms;
    session->wait_until_ms = wait_flags->timeout_ms > 0 ? now_ms + wait_flags->timeout_ms : 0;
    session->wait_flag_count = wait_flags->flag_count;
    for (uint8_t i = 0; i < wait_flags->flag_count; ++i) {
        quest_str_copy(session->wait_flags[i].name,
                       sizeof(session->wait_flags[i].name),
                       wait_flags->flags[i].name);
        session->wait_flags[i].value = wait_flags->flags[i].value;
    }
    gm_room_session_mark_session_changed_locked(session);
    return ESP_OK;
}

void scenario_enter_wait_command_result_locked(
    gm_room_session_t *session,
    const gm_room_session_command_dispatch_result_t *dispatch,
    uint32_t now_ms)
{
    if (!session || !dispatch || !dispatch->result_required) {
        return;
    }
    session->scenario_state = GM_ROOM_SCENARIO_WAITING;
    gm_room_session_scenario_clear_wait_locked(session);
    session->wait_type = GM_ROOM_SCENARIO_WAIT_DEVICE_COMMAND_RESULT;
    session->wait_started_at_ms = now_ms;
    session->wait_until_ms = now_ms + (dispatch->timeout_ms ? dispatch->timeout_ms
                                                            : GM_ROOM_SESSION_COMMAND_TIMEOUT_DEFAULT_MS);
    quest_str_copy(session->wait_event_type,
                   sizeof(session->wait_event_type),
                   dispatch->request_id);
    quest_str_copy(session->wait_source_id,
                   sizeof(session->wait_source_id),
                   dispatch->source_id);
    session->wait_event_count = 1;
    quest_str_copy(session->wait_events[0].event_type,
                   sizeof(session->wait_events[0].event_type),
                   dispatch->request_id);
    quest_str_copy(session->wait_events[0].source_id,
                   sizeof(session->wait_events[0].source_id),
                   dispatch->source_id);
    gm_room_session_mark_session_changed_locked(session);
}
