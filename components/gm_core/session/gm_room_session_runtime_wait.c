#include "gm_room_session_wait_internal.h"

#include <string.h>

#include "esp_attr.h"
#include "freertos/semphr.h"
#include "quest_common_utils.h"
#include "quest_device.h"

static EXT_RAM_BSS_ATTR quest_device_t s_wait_device;
static SemaphoreHandle_t s_wait_resolve_mutex = NULL;
static StaticSemaphore_t s_wait_resolve_mutex_storage;
static portMUX_TYPE s_wait_resolve_mutex_init_lock = portMUX_INITIALIZER_UNLOCKED;

static esp_err_t scenario_wait_ensure_resolve_mutex(void)
{
    if (s_wait_resolve_mutex) {
        return ESP_OK;
    }
    portENTER_CRITICAL(&s_wait_resolve_mutex_init_lock);
    if (!s_wait_resolve_mutex) {
        s_wait_resolve_mutex = xSemaphoreCreateMutexStatic(&s_wait_resolve_mutex_storage);
    }
    portEXIT_CRITICAL(&s_wait_resolve_mutex_init_lock);
    return s_wait_resolve_mutex ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t scenario_wait_resolve_lock(void)
{
    esp_err_t err = scenario_wait_ensure_resolve_mutex();
    if (err != ESP_OK) {
        return err;
    }
    return xSemaphoreTake(s_wait_resolve_mutex, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void scenario_wait_resolve_unlock(void)
{
    if (s_wait_resolve_mutex) {
        xSemaphoreGive(s_wait_resolve_mutex);
    }
}

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

static esp_err_t scenario_resolve_wait_event_match(const room_scenario_wait_device_event_t *wait,
                                                   gm_room_scenario_wait_event_match_t *out)
{
    quest_device_event_t event = {0};
    quest_device_t *device = &s_wait_device;
    esp_err_t err = ESP_OK;
    if (!wait || !out || !wait->device_id[0] || !wait->event_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    err = scenario_wait_resolve_lock();
    if (err != ESP_OK) {
        return err;
    }
    memset(device, 0, sizeof(*device));
    err = quest_device_get(wait->device_id, device);
    if (err != ESP_OK) {
        scenario_wait_resolve_unlock();
        return err;
    }
    if (!device->enabled) {
        scenario_wait_resolve_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    err = quest_device_get_event(wait->device_id, wait->event_id, &event);
    if (err != ESP_OK) {
        scenario_wait_resolve_unlock();
        return err;
    }
    memset(out, 0, sizeof(*out));
    quest_str_copy(out->device_id, sizeof(out->device_id), wait->device_id);
    quest_str_copy(out->event_id, sizeof(out->event_id), wait->event_id);
    quest_str_copy(out->event_type,
                   sizeof(out->event_type),
                   event.event[0] ? event.event : event.id);
    quest_str_copy(out->match_json, sizeof(out->match_json), event.match_json);
    if (strcmp(wait->device_id, QUEST_DEVICE_SYSTEM_AUDIO_ID) == 0) {
        out->source_id[0] = '\0';
    } else {
        quest_str_copy(out->source_id,
                       sizeof(out->source_id),
                       device->client_id);
    }
    scenario_wait_resolve_unlock();
    return ESP_OK;
}

esp_err_t scenario_resolve_wait_device_event_unlocked(
    const room_scenario_wait_device_event_t *wait,
    gm_room_session_wait_resolution_t *out)
{
    if (!wait || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->present = true;
    out->match_count = 1;
    return scenario_resolve_wait_event_match(wait, &out->matches[0]);
}

esp_err_t scenario_resolve_wait_any_device_event_unlocked(
    const room_scenario_wait_any_device_event_t *wait_any,
    gm_room_session_wait_resolution_t *out)
{
    if (!wait_any || !out || wait_any->event_count == 0 ||
        wait_any->event_count > ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->present = true;
    out->match_count = wait_any->event_count;
    for (uint8_t i = 0; i < wait_any->event_count; ++i) {
        esp_err_t err = scenario_resolve_wait_event_match(&wait_any->events[i], &out->matches[i]);
        if (err != ESP_OK) {
            memset(out, 0, sizeof(*out));
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t scenario_resolve_wait_all_device_events_unlocked(
    const room_scenario_wait_all_device_events_t *wait_all,
    gm_room_session_wait_resolution_t *out)
{
    if (!wait_all || !out || wait_all->event_count == 0 ||
        wait_all->event_count > ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->present = true;
    out->match_count = wait_all->event_count;
    for (uint8_t i = 0; i < wait_all->event_count; ++i) {
        esp_err_t err = scenario_resolve_wait_event_match(&wait_all->events[i], &out->matches[i]);
        if (err != ESP_OK) {
            memset(out, 0, sizeof(*out));
            return err;
        }
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
    const gm_room_session_command_dispatch_t *dispatch,
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
                                                            : QUEST_DEVICE_COMMAND_TIMEOUT_DEFAULT_MS);
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
