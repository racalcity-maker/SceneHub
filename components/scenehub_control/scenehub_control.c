#include "scenehub_control_internal.h"

#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_timer.h"
#include "event_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "gm_game_profile.h"
#include "orchestrator_audit.h"
#include "orch_room_view.h"
#include "orchestrator_timeline.h"
#include "quest_common_utils.h"
#include "quest_device.h"
#include "room_scenario.h"
#include "scenehub_state.h"
#include "service_status.h"

#define SCENEHUB_CONTROL_ERR_BASE             0x7600
#define SCENEHUB_CONTROL_ERR_ROOM_NOT_FOUND   (SCENEHUB_CONTROL_ERR_BASE + 1)
#define SCENEHUB_CONTROL_ERR_ACTION_NOT_FOUND (SCENEHUB_CONTROL_ERR_BASE + 2)
#define SCENEHUB_CONTROL_ERR_ACTION_DISABLED  (SCENEHUB_CONTROL_ERR_BASE + 3)
#define SCENEHUB_CONTROL_ERR_NOT_SUPPORTED    (SCENEHUB_CONTROL_ERR_BASE + 4)
#define SCENEHUB_CONTROL_ERR_EXECUTION_FAILED (SCENEHUB_CONTROL_ERR_BASE + 5)
#define SCENEHUB_CONTROL_ERR_ROOM_UNHEALTHY   (SCENEHUB_CONTROL_ERR_BASE + 6)

static bool s_persistence_enabled = true;
static EXT_RAM_BSS_ATTR quest_device_t s_event_resolver_device;
static EXT_RAM_BSS_ATTR gm_room_session_prepared_scenario_t s_prepared_scenario_scratch;
static SemaphoreHandle_t s_event_resolver_mutex = NULL;
static StaticSemaphore_t s_event_resolver_mutex_storage;
static portMUX_TYPE s_event_resolver_mutex_init_lock = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t s_prepared_scenario_mutex = NULL;
static StaticSemaphore_t s_prepared_scenario_mutex_storage;
static portMUX_TYPE s_prepared_scenario_mutex_init_lock = portMUX_INITIALIZER_UNLOCKED;

static esp_err_t scenehub_control_event_resolver_lock(void)
{
    if (!s_event_resolver_mutex) {
        portENTER_CRITICAL(&s_event_resolver_mutex_init_lock);
        if (!s_event_resolver_mutex) {
            s_event_resolver_mutex = xSemaphoreCreateMutexStatic(&s_event_resolver_mutex_storage);
        }
        portEXIT_CRITICAL(&s_event_resolver_mutex_init_lock);
        if (!s_event_resolver_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }
    return xSemaphoreTake(s_event_resolver_mutex, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void scenehub_control_event_resolver_unlock(void)
{
    if (s_event_resolver_mutex) {
        xSemaphoreGive(s_event_resolver_mutex);
    }
}

static esp_err_t scenehub_control_prepared_scenario_lock(void)
{
    if (!s_prepared_scenario_mutex) {
        portENTER_CRITICAL(&s_prepared_scenario_mutex_init_lock);
        if (!s_prepared_scenario_mutex) {
            s_prepared_scenario_mutex =
                xSemaphoreCreateMutexStatic(&s_prepared_scenario_mutex_storage);
        }
        portEXIT_CRITICAL(&s_prepared_scenario_mutex_init_lock);
        if (!s_prepared_scenario_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }
    return xSemaphoreTake(s_prepared_scenario_mutex, portMAX_DELAY) == pdTRUE
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
}

static esp_err_t scenehub_control_resolve_device_event_ref(
    const room_scenario_device_event_ref_t *event_ref,
    bool require_enabled_device,
    gm_room_scenario_wait_event_match_t *out_match,
    char *out_alternate_event_type,
    size_t out_alternate_event_type_size)
{
    quest_device_event_t event = {0};
    quest_device_t *device = &s_event_resolver_device;
    esp_err_t err = ESP_OK;

    if (!event_ref || !out_match || !event_ref->device_id[0] || !event_ref->event_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_match, 0, sizeof(*out_match));
    if (out_alternate_event_type && out_alternate_event_type_size > 0) {
        out_alternate_event_type[0] = '\0';
    }

    err = scenehub_control_event_resolver_lock();
    if (err != ESP_OK) {
        return err;
    }
    memset(device, 0, sizeof(*device));
    err = quest_device_get(event_ref->device_id, device);
    if (err == ESP_OK && require_enabled_device && !device->enabled) {
        err = ESP_ERR_INVALID_STATE;
    }
    if (err == ESP_OK) {
        err = quest_device_get_event(event_ref->device_id, event_ref->event_id, &event);
    }
    if (err == ESP_OK) {
        quest_str_copy(out_match->device_id, sizeof(out_match->device_id), event_ref->device_id);
        quest_str_copy(out_match->event_id, sizeof(out_match->event_id), event_ref->event_id);
        quest_str_copy(out_match->event_type,
                       sizeof(out_match->event_type),
                       event.event[0] ? event.event : event.id);
        quest_str_copy(out_match->match_json, sizeof(out_match->match_json), event.match_json);
        if (out_alternate_event_type && out_alternate_event_type_size > 0 &&
            event.event[0] && strcmp(event.event, event.id) != 0) {
            quest_str_copy(out_alternate_event_type, out_alternate_event_type_size, event.id);
        }
        if (strcmp(event_ref->device_id, QUEST_DEVICE_SYSTEM_AUDIO_ID) == 0) {
            out_match->source_id[0] = '\0';
        } else {
            quest_str_copy(out_match->source_id,
                           sizeof(out_match->source_id),
                           device->client_id);
        }
    }
    scenehub_control_event_resolver_unlock();
    return err;
}

static esp_err_t scenehub_control_prepare_event_ref(
    gm_room_session_prepared_scenario_t *prepared,
    const room_scenario_device_event_ref_t *event_ref,
    bool require_enabled_device,
    uint16_t *out_index)
{
    gm_room_session_prepared_event_ref_t *dst = NULL;
    esp_err_t err = ESP_OK;

    if (!prepared || !event_ref || !out_index ||
        !event_ref->device_id[0] || !event_ref->event_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    for (uint16_t i = 0; i < prepared->event_ref_count; ++i) {
        dst = &prepared->event_refs[i];
        if (strcmp(dst->match.device_id, event_ref->device_id) != 0 ||
            strcmp(dst->match.event_id, event_ref->event_id) != 0) {
            continue;
        }
        if (require_enabled_device && !dst->enabled_device_validated) {
            err = scenehub_control_resolve_device_event_ref(
                event_ref,
                true,
                &dst->match,
                dst->alternate_event_type,
                sizeof(dst->alternate_event_type));
            if (err != ESP_OK) {
                return err;
            }
            dst->enabled_device_validated = true;
        }
        *out_index = i;
        return ESP_OK;
    }
    if (prepared->event_ref_count >= GM_ROOM_SESSION_PREPARED_EVENT_REF_MAX) {
        return ESP_ERR_NO_MEM;
    }
    dst = &prepared->event_refs[prepared->event_ref_count];
    err = scenehub_control_resolve_device_event_ref(event_ref,
                                                    require_enabled_device,
                                                    &dst->match,
                                                    dst->alternate_event_type,
                                                    sizeof(dst->alternate_event_type));
    if (err != ESP_OK) {
        return err;
    }
    dst->enabled_device_validated = require_enabled_device;
    *out_index = prepared->event_ref_count++;
    return ESP_OK;
}

static esp_err_t scenehub_control_prepare_event_resolution(
    gm_room_session_prepared_scenario_t *prepared,
    const room_scenario_device_event_ref_t *event_refs,
    uint8_t event_count,
    bool require_enabled_device,
    gm_room_session_prepared_event_resolution_t *out)
{
    if (!prepared || !event_refs || !out || event_count == 0 ||
        event_count > ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->present = true;
    out->match_count = event_count;
    for (uint8_t i = 0; i < event_count; ++i) {
        esp_err_t err = scenehub_control_prepare_event_ref(prepared,
                                                          &event_refs[i],
                                                          require_enabled_device,
                                                          &out->event_ref_indices[i]);
        if (err != ESP_OK) {
            memset(out, 0, sizeof(*out));
            return err;
        }
    }
    return ESP_OK;
}

static esp_err_t scenehub_control_prepare_wait(
    gm_room_session_prepared_scenario_t *prepared,
    room_scenario_step_type_t type,
    const room_scenario_wait_device_event_t *wait_device_event,
    const room_scenario_wait_any_device_event_t *wait_any_device_event,
    const room_scenario_wait_all_device_events_t *wait_all_device_events,
    gm_room_session_prepared_event_resolution_t *out)
{
    switch (type) {
    case ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT:
        return scenehub_control_prepare_event_resolution(
            prepared,
            (const room_scenario_device_event_ref_t *)wait_device_event,
            1,
            true,
            out);
    case ROOM_SCENARIO_STEP_WAIT_ANY_DEVICE_EVENT:
        return scenehub_control_prepare_event_resolution(prepared,
                                                         wait_any_device_event->events,
                                                         wait_any_device_event->event_count,
                                                         true,
                                                         out);
    case ROOM_SCENARIO_STEP_WAIT_ALL_DEVICE_EVENTS:
        return scenehub_control_prepare_event_resolution(prepared,
                                                         wait_all_device_events->events,
                                                         wait_all_device_events->event_count,
                                                         true,
                                                         out);
    default:
        return ESP_OK;
    }
}

static esp_err_t scenehub_control_prepare_reactive_trigger(
    gm_room_session_prepared_scenario_t *prepared,
    const room_scenario_reactive_trigger_t *trigger,
    gm_room_session_prepared_event_resolution_t *out)
{
    room_scenario_device_event_ref_t event_ref = {0};

    switch (trigger->kind) {
    case ROOM_SCENARIO_REACTIVE_TRIGGER_DEVICE_EVENT:
        quest_str_copy(event_ref.device_id, sizeof(event_ref.device_id), trigger->device_id);
        quest_str_copy(event_ref.event_id, sizeof(event_ref.event_id), trigger->event_id);
        return scenehub_control_prepare_event_resolution(prepared, &event_ref, 1, false, out);
    case ROOM_SCENARIO_REACTIVE_TRIGGER_ANY_DEVICE_EVENTS:
    case ROOM_SCENARIO_REACTIVE_TRIGGER_ALL_DEVICE_EVENTS:
        return scenehub_control_prepare_event_resolution(prepared,
                                                         trigger->events,
                                                         trigger->event_count,
                                                         false,
                                                         out);
    default:
        return ESP_OK;
    }
}

esp_err_t scenehub_control_prepare_session_scenario(
    const room_scenario_t *scenario,
    gm_room_session_prepared_scenario_t *out)
{
    esp_err_t err = ESP_OK;

    if (!scenario || !out ||
        scenario->step_count > ROOM_SCENARIO_MAX_STEPS ||
        scenario->reactive_action_count > ROOM_SCENARIO_MAX_REACTIVE_ACTIONS ||
        scenario->branch_count > ROOM_SCENARIO_MAX_BRANCHES) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    for (size_t i = 0; i < scenario->step_count; ++i) {
        const room_scenario_step_t *step = &scenario->steps[i];
        if (!step->enabled) {
            continue;
        }
        err = scenehub_control_prepare_wait(out,
                                            step->type,
                                            &step->data.wait_device_event,
                                            &step->data.wait_any_device_event,
                                            &step->data.wait_all_device_events,
                                            &out->step_waits[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    for (size_t i = 0; i < scenario->reactive_action_count; ++i) {
        const room_scenario_reactive_action_t *action = &scenario->reactive_actions[i];
        err = scenehub_control_prepare_wait(out,
                                            action->type,
                                            &action->data.wait_device_event,
                                            &action->data.wait_any_device_event,
                                            &action->data.wait_all_device_events,
                                            &out->reactive_action_waits[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    for (size_t i = 0; i < scenario->branch_count; ++i) {
        err = scenehub_control_prepare_reactive_trigger(out,
                                                        &scenario->branches[i].trigger,
                                                        &out->reactive_triggers[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t scenehub_control_acquire_prepared_session_scenario(
    const room_scenario_t *scenario,
    const gm_room_session_prepared_scenario_t **out)
{
    esp_err_t err = ESP_OK;

    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    err = scenehub_control_prepared_scenario_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = scenehub_control_prepare_session_scenario(scenario, &s_prepared_scenario_scratch);
    if (err != ESP_OK) {
        xSemaphoreGive(s_prepared_scenario_mutex);
        return err;
    }
    *out = &s_prepared_scenario_scratch;
    return ESP_OK;
}

void scenehub_control_release_prepared_session_scenario(void)
{
    if (s_prepared_scenario_mutex) {
        xSemaphoreGive(s_prepared_scenario_mutex);
    }
}

static void scenehub_control_session_event_handler(const scenehub_event_t *message)
{
    gm_room_session_route_event(message);
}

static void scenehub_control_stop_audio(void)
{
    (void)command_executor_execute_device_command(QUEST_DEVICE_SYSTEM_AUDIO_ID,
                                                  "stop",
                                                  "{\"channel\":\"all\"}");
}

esp_err_t scenehub_control_init(void)
{
    esp_err_t err = ESP_OK;

    gm_room_session_set_stop_audio_handler(scenehub_control_stop_audio);
    scenehub_control_register_session_command_dispatcher();
    err = gm_room_session_init();
    if (err != ESP_OK) {
        return err;
    }
    err = gm_room_session_start_async_runtime();
    if (err != ESP_OK) {
        return err;
    }
    return event_bus_register_handler(scenehub_control_session_event_handler);
}

void scenehub_control_set_persistence_enabled_for_test(bool enabled)
{
    s_persistence_enabled = enabled;
}

bool scenehub_control_persistence_enabled(void)
{
    return s_persistence_enabled;
}

static uint64_t scenehub_control_now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
}

static const char *scenehub_control_room_action_error_code(esp_err_t err)
{
    switch (err) {
    case ESP_OK:
        return "ok";
    case ESP_ERR_INVALID_ARG:
        return "invalid_request";
    case SCENEHUB_CONTROL_ERR_ROOM_NOT_FOUND:
        return "room_not_found";
    case SCENEHUB_CONTROL_ERR_ACTION_NOT_FOUND:
        return "action_not_found";
    case SCENEHUB_CONTROL_ERR_ACTION_DISABLED:
        return "action_disabled";
    case SCENEHUB_CONTROL_ERR_NOT_SUPPORTED:
        return "not_supported";
    case SCENEHUB_CONTROL_ERR_ROOM_UNHEALTHY:
        return "room_unhealthy";
    case SCENEHUB_CONTROL_ERR_EXECUTION_FAILED:
    default:
        return "execution_failed";
    }
}

static esp_err_t scenehub_control_normalize_room_action_error(esp_err_t err)
{
    switch (err) {
    case ESP_OK:
    case ESP_ERR_INVALID_ARG:
    case SCENEHUB_CONTROL_ERR_ROOM_NOT_FOUND:
    case SCENEHUB_CONTROL_ERR_ACTION_NOT_FOUND:
    case SCENEHUB_CONTROL_ERR_ACTION_DISABLED:
    case SCENEHUB_CONTROL_ERR_NOT_SUPPORTED:
    case SCENEHUB_CONTROL_ERR_ROOM_UNHEALTHY:
    case SCENEHUB_CONTROL_ERR_EXECUTION_FAILED:
        return err;
    case ESP_ERR_NOT_FOUND:
        return SCENEHUB_CONTROL_ERR_ROOM_NOT_FOUND;
    case ESP_ERR_INVALID_STATE:
        return SCENEHUB_CONTROL_ERR_ACTION_DISABLED;
    case ESP_ERR_NOT_SUPPORTED:
        return SCENEHUB_CONTROL_ERR_NOT_SUPPORTED;
    default:
        return SCENEHUB_CONTROL_ERR_EXECUTION_FAILED;
    }
}

static void scenehub_control_log_room_action(const char *source,
                                             const char *room_id,
                                             const char *action_id,
                                             esp_err_t result)
{
    const char *audit_source = (source && source[0]) ? source : "internal";
    const char *audit_room = (room_id && room_id[0]) ? room_id : "-";
    const char *audit_action = (action_id && action_id[0]) ? action_id : "-";
    const char *error_code = scenehub_control_room_action_error_code(result);
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

void scenehub_control_build_session_profile(const gm_game_profile_t *profile,
                                            gm_room_session_profile_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (!profile) {
        return;
    }
    scenehub_control_copy(out->id, sizeof(out->id), profile->id);
    scenehub_control_copy(out->name, sizeof(out->name), profile->name);
    scenehub_control_copy(out->room_id, sizeof(out->room_id), profile->room_id);
    scenehub_control_copy(out->scenario_id, sizeof(out->scenario_id), profile->scenario_id);
    out->duration_ms = profile->duration_ms;
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

esp_err_t scenehub_control_require_room(const char *room_id)
{
    esp_err_t err = ESP_OK;

    if (!room_id || !room_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    err = room_catalog_init();
    if (err != ESP_OK) {
        return err;
    }
    return room_catalog_exists(room_id) ? ESP_OK : ESP_ERR_NOT_FOUND;
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

void scenehub_control_set_request_id(scenehub_control_result_t *result, const char *request_id)
{
    if (!result) {
        return;
    }
    result->has_request_id = request_id && request_id[0];
    scenehub_control_copy(result->request_id, sizeof(result->request_id), request_id);
}

void scenehub_control_set_remote_status(scenehub_control_result_t *result, const char *remote_status)
{
    if (!result) {
        return;
    }
    result->has_remote_status = remote_status && remote_status[0];
    scenehub_control_copy(result->remote_status, sizeof(result->remote_status), remote_status);
}

void scenehub_control_finish_success_with_invalidation(scenehub_control_result_t *result,
                                                       scenehub_state_slice_t slice,
                                                       const char *target_id,
                                                       const char *reason)
{
    scenehub_control_set_result(result, SCENEHUB_CONTROL_STATUS_DONE, ESP_OK, true, "", "");
    scenehub_state_notify_invalidation(slice, target_id, reason);
}

void scenehub_control_finish_accepted_with_invalidation(scenehub_control_result_t *result,
                                                        scenehub_state_slice_t slice,
                                                        const char *target_id,
                                                        const char *reason)
{
    scenehub_control_set_result(result, SCENEHUB_CONTROL_STATUS_ACCEPTED, ESP_OK, true, "", "");
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
                                    "",
                                    title ? title : "Timer changed",
                                    details ? details : "");
}

void scenehub_control_log_device_action(const char *source,
                                        const char *device_id,
                                        bool warning,
                                        const char *command_id,
                                        const char *request_id)
{
    orchestrator_timeline_severity_t severity = ORCH_TIMELINE_SEVERITY_INFO;
    (void)orchestrator_audit_log_device_action((source && source[0]) ? source : "internal",
                                               device_id ? device_id : "",
                                               command_id ? command_id : "",
                                               request_id ? request_id : "",
                                               true,
                                               "");
    if (warning) {
        severity = ORCH_TIMELINE_SEVERITY_WARNING;
    }
    (void)orchestrator_timeline_log(ORCH_TIMELINE_TYPE_DEVICE_ACTION,
                                    severity,
                                    (source && source[0]) ? source : "internal",
                                    "",
                                    device_id ? device_id : "",
                                    request_id ? request_id : "",
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
        return SCENEHUB_CONTROL_ERR_ROOM_UNHEALTHY;
    }
    return ESP_OK;
}

static esp_err_t scenehub_control_validate_room_action_enabled(const char *room_id,
                                                               const char *action_id)
{
    gm_room_session_timer_view_t timer = {0};
    gm_room_session_selected_view_t selected = {0};
    bool enabled = false;
    bool known_action = true;
    bool session_present = false;
    esp_err_t err = ESP_OK;

    if (!room_id || !room_id[0] || !action_id || !action_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    err = scenehub_control_require_room(room_id);
    if (err == ESP_ERR_NOT_FOUND) {
        return SCENEHUB_CONTROL_ERR_ROOM_NOT_FOUND;
    }
    if (err != ESP_OK) {
        return err;
    }
    err = gm_room_session_get_read_views(room_id,
                                         scenehub_control_now_ms(),
                                         &timer,
                                         &selected,
                                         NULL);
    if (err == ESP_OK) {
        session_present = true;
    } else if (err != ESP_ERR_NOT_FOUND) {
        return SCENEHUB_CONTROL_ERR_EXECUTION_FAILED;
    }

    if (strcmp(action_id, "start_game") == 0) {
        enabled = selected.selected_profile_id[0] &&
                  timer.session_state != GM_SESSION_RUNNING;
    } else if (strcmp(action_id, "stop_game") == 0) {
        enabled = session_present &&
                  timer.session_state != GM_SESSION_FINISHED &&
                  selected.selected_profile_id[0];
    } else if (strcmp(action_id, "reset_game") == 0) {
        enabled = session_present &&
                  (selected.selected_profile_id[0] || timer.duration_ms > 0);
    } else if (strcmp(action_id, "reset_room_timer") == 0) {
        enabled = timer.duration_ms > 0;
    } else if (strcmp(action_id, "pause_room_timer") == 0) {
        enabled = timer.timer_state == GM_TIMER_RUNNING;
    } else if (strcmp(action_id, "resume_room_timer") == 0) {
        enabled = timer.timer_state == GM_TIMER_PAUSED;
    } else if (strcmp(action_id, "finish_room_session") == 0) {
        enabled = session_present &&
                  timer.session_state != GM_SESSION_FINISHED &&
                  !selected.selected_profile_id[0];
    } else if (strcmp(action_id, "clear_room_hint") == 0) {
        enabled = timer.hint_active;
    } else {
        known_action = false;
    }
    if (!known_action) {
        return SCENEHUB_CONTROL_ERR_ACTION_NOT_FOUND;
    }
    return enabled ? ESP_OK : SCENEHUB_CONTROL_ERR_ACTION_DISABLED;
}

static bool scenehub_control_owns_game_action(const char *action_id)
{
    return action_id &&
           (strcmp(action_id, "start_game") == 0 ||
            strcmp(action_id, "stop_game") == 0 ||
            strcmp(action_id, "reset_game") == 0);
}

static esp_err_t scenehub_control_safe_off_hardware_optional(void)
{
    esp_err_t err = ESP_OK;

    if (!hardware_io_is_available()) {
        return ESP_OK;
    }
    err = hardware_io_safe_off_all();
    service_status_mark_fault(SERVICE_STATUS_HARDWARE_IO, err);
    return err;
}

static esp_err_t scenehub_control_execute_start_game(const char *source,
                                                     const char *room_id,
                                                     const char *action_id)
{
    gm_room_session_selected_view_t selected = {0};
    gm_room_session_profile_t session_profile = {0};
    gm_game_profile_t profile = {0};
    room_scenario_t *scenario = NULL;
    const gm_room_session_prepared_scenario_t *prepared_scenario = NULL;
    gm_room_session_command_plan_t plan = {0};
    esp_err_t err = scenehub_control_validate_room_action_enabled(room_id, action_id);

    scenehub_control_register_session_command_dispatcher();
    if (err == ESP_OK) {
        err = gm_room_session_get_selected_view(room_id, &selected);
    }
    if (err == ESP_OK && !selected.selected_profile_id[0]) {
        err = ESP_ERR_INVALID_STATE;
    }
    if (err == ESP_OK) {
        err = gm_game_profile_get(selected.selected_profile_id, &profile);
    }
    if (err == ESP_OK && strcmp(profile.room_id, room_id) != 0) {
        err = ESP_ERR_INVALID_STATE;
    }
    if (err == ESP_OK) {
        err = gm_game_profile_validate_reference(&profile);
    }
    if (err == ESP_OK) {
        err = room_scenario_acquire_scratch(&scenario, NULL);
    }
    if (err == ESP_OK) {
        memset(scenario, 0, sizeof(*scenario));
        err = room_scenario_get(profile.scenario_id, scenario);
    }
    if (err == ESP_OK) {
        err = scenehub_control_acquire_prepared_session_scenario(scenario, &prepared_scenario);
    }
    if (err == ESP_OK) {
        scenehub_control_build_session_profile(&profile, &session_profile);
        err = gm_room_session_game_start_prepared(
            room_id,
            scenehub_control_now_ms(),
            &(gm_room_session_game_start_prepared_t) {
                .profile = &session_profile,
                .scenario = scenario,
                .prepared_scenario = prepared_scenario,
                .duration_ms = profile.duration_ms,
            },
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
    err = scenehub_control_normalize_room_action_error(err);
    scenehub_control_log_room_action(source, room_id, action_id, err);
    return err;
}

static esp_err_t scenehub_control_execute_stop_game(const char *source,
                                                    const char *room_id,
                                                    const char *action_id)
{
    esp_err_t err = scenehub_control_validate_room_action_enabled(room_id, action_id);
    esp_err_t safe_err = ESP_OK;

    if (err == ESP_OK) {
        err = gm_room_session_scenario_stop(room_id);
    }
    if (err == ESP_OK) {
        err = gm_room_session_finish(room_id, scenehub_control_now_ms());
    }
    if (err == ESP_OK) {
        safe_err = scenehub_control_safe_off_hardware_optional();
        err = safe_err == ESP_OK ? ESP_OK : safe_err;
    }
    err = scenehub_control_normalize_room_action_error(err);
    scenehub_control_log_room_action(source, room_id, action_id, err);
    return err;
}

static esp_err_t scenehub_control_execute_reset_game(const char *source,
                                                     const char *room_id,
                                                     const char *action_id)
{
    gm_room_session_timer_view_t timer_view = {0};
    gm_room_session_selected_view_t selected = {0};
    gm_room_session_profile_t session_profile = {0};
    gm_game_profile_t profile = {0};
    room_scenario_t *scenario = NULL;
    uint32_t duration_ms = 0;
    esp_err_t err = scenehub_control_validate_room_action_enabled(room_id, action_id);
    esp_err_t safe_err = ESP_OK;

    if (err == ESP_OK) {
        err = gm_room_session_get_read_views(room_id,
                                             scenehub_control_now_ms(),
                                             &timer_view,
                                             &selected,
                                             NULL);
    }
    if (err == ESP_OK && selected.selected_profile_id[0]) {
        duration_ms = selected.selected_profile_duration_ms;
        err = gm_game_profile_get(selected.selected_profile_id, &profile);
        if (err == ESP_OK && strcmp(profile.room_id, room_id) != 0) {
            err = ESP_ERR_INVALID_STATE;
        }
        if (err == ESP_OK) {
            err = gm_game_profile_validate_reference(&profile);
        }
        if (err == ESP_OK) {
            err = room_scenario_acquire_scratch(&scenario, NULL);
        }
        if (err == ESP_OK) {
            memset(scenario, 0, sizeof(*scenario));
            err = room_scenario_get(profile.scenario_id, scenario);
        }
        if (err == ESP_OK) {
            scenehub_control_build_session_profile(&profile, &session_profile);
            if (duration_ms == 0) {
                duration_ms = profile.duration_ms;
            }
            err = gm_room_session_select_profile_prepared(room_id,
                                                          &session_profile,
                                                          scenario,
                                                          duration_ms);
        }
        if (scenario) {
            room_scenario_release_scratch();
        }
    } else if (err == ESP_OK) {
        duration_ms = timer_view.duration_ms;
        err = gm_room_session_scenario_reset(room_id);
    }
    if (err == ESP_OK) {
        gm_room_session_stop_audio();
        err = gm_room_session_reset(room_id, duration_ms, scenehub_control_now_ms());
    }
    if (err == ESP_OK) {
        safe_err = scenehub_control_safe_off_hardware_optional();
        err = safe_err == ESP_OK ? ESP_OK : safe_err;
    }
    err = scenehub_control_normalize_room_action_error(err);
    scenehub_control_log_room_action(source, room_id, action_id, err);
    return err;
}

static esp_err_t scenehub_control_execute_session_action(const char *source,
                                                         const char *room_id,
                                                         const char *action_id)
{
    gm_room_session_timer_view_t timer_view = {0};
    esp_err_t err = scenehub_control_validate_room_action_enabled(room_id, action_id);

    if (err == ESP_OK && strcmp(action_id, "reset_room_timer") == 0) {
        err = gm_room_session_get_timer_view(room_id, scenehub_control_now_ms(), &timer_view);
        if (err == ESP_OK) {
            err = gm_room_session_reset(room_id, timer_view.duration_ms, scenehub_control_now_ms());
        }
    } else if (err == ESP_OK && strcmp(action_id, "pause_room_timer") == 0) {
        err = gm_room_session_pause(room_id, scenehub_control_now_ms());
    } else if (err == ESP_OK && strcmp(action_id, "resume_room_timer") == 0) {
        err = gm_room_session_resume(room_id, scenehub_control_now_ms());
    } else if (err == ESP_OK && strcmp(action_id, "finish_room_session") == 0) {
        err = gm_room_session_finish(room_id, scenehub_control_now_ms());
    } else if (err == ESP_OK && strcmp(action_id, "clear_room_hint") == 0) {
        err = gm_room_session_clear_hint(room_id, scenehub_control_now_ms());
    } else if (err == ESP_OK) {
        err = SCENEHUB_CONTROL_ERR_ACTION_NOT_FOUND;
    }

    err = scenehub_control_normalize_room_action_error(err);
    scenehub_control_log_room_action(source, room_id, action_id, err);
    return err;
}

static bool scenehub_control_try_capture_start_game_validation_error(const char *room_id,
                                                                     const char *action_id,
                                                                     esp_err_t err,
                                                                     scenehub_control_result_t *out_result)
{
    gm_room_session_runtime_summary_t runtime = {0};
    if (!out_result || err != ESP_ERR_INVALID_ARG || !room_id || !room_id[0] || !action_id ||
        strcmp(action_id, "start_game") != 0) {
        return false;
    }
    if (gm_room_session_get_runtime_summary(room_id, &runtime) != ESP_OK) {
        return false;
    }
    if (!runtime.scenario_last_error[0]) {
        return false;
    }
    scenehub_control_set_result(out_result,
                                SCENEHUB_CONTROL_STATUS_REJECTED,
                                err,
                                false,
                                "scenario_invalid",
                                runtime.scenario_last_error);
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
        if (action_id && strcmp(action_id, "start_game") == 0) {
            err = scenehub_control_execute_start_game(source, room_id, action_id);
        } else if (action_id && strcmp(action_id, "stop_game") == 0) {
            err = scenehub_control_execute_stop_game(source, room_id, action_id);
        } else if (action_id && strcmp(action_id, "reset_game") == 0) {
            err = scenehub_control_execute_reset_game(source, room_id, action_id);
        } else {
            err = scenehub_control_execute_session_action(source, room_id, action_id);
        }
    } else if (scenehub_control_owns_game_action(action_id)) {
        err = scenehub_control_normalize_room_action_error(err);
        scenehub_control_log_room_action(source, room_id, action_id, err);
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
    case SCENEHUB_CONTROL_ERR_ROOM_NOT_FOUND:
        scenehub_control_set_result(out_result,
                                    SCENEHUB_CONTROL_STATUS_REJECTED,
                                    err,
                                    false,
                                    "room_not_found",
                                    "Room not found");
        return ESP_OK;
    case SCENEHUB_CONTROL_ERR_ACTION_NOT_FOUND:
        scenehub_control_set_result(out_result,
                                    SCENEHUB_CONTROL_STATUS_REJECTED,
                                    err,
                                    false,
                                    "action_not_found",
                                    "Action not found");
        return ESP_OK;
    case SCENEHUB_CONTROL_ERR_ACTION_DISABLED:
        scenehub_control_set_result(out_result,
                                    SCENEHUB_CONTROL_STATUS_REJECTED,
                                    err,
                                    false,
                                    "action_disabled",
                                    "Action disabled");
        return ESP_OK;
    case SCENEHUB_CONTROL_ERR_NOT_SUPPORTED:
        scenehub_control_set_result(out_result,
                                    SCENEHUB_CONTROL_STATUS_REJECTED,
                                    err,
                                    false,
                                    "not_supported",
                                    "Action not supported");
        return ESP_OK;
    case SCENEHUB_CONTROL_ERR_ROOM_UNHEALTHY:
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

    err = scenehub_control_require_room(room_id);
    if (err == ESP_OK) {
        err = gm_room_session_start(room_id, duration_ms, scenehub_control_now_ms());
    }
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

    err = scenehub_control_require_room(room_id);
    if (err == ESP_OK) {
        err = gm_room_session_pause(room_id, scenehub_control_now_ms());
    }
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

    err = scenehub_control_require_room(room_id);
    if (err == ESP_OK) {
        err = gm_room_session_resume(room_id, scenehub_control_now_ms());
    }
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

    err = scenehub_control_require_room(room_id);
    if (err == ESP_OK && !has_duration) {
        gm_room_session_timer_view_t timer_view = {0};
        err = gm_room_session_get_timer_view(room_id, scenehub_control_now_ms(), &timer_view);
        if (err == ESP_OK) {
            duration_ms = timer_view.duration_ms;
        }
    }
    if (err == ESP_OK) {
        err = gm_room_session_reset(room_id, duration_ms, scenehub_control_now_ms());
    }
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

    err = scenehub_control_require_room(room_id);
    if (err == ESP_OK) {
        err = gm_room_session_add_time(room_id, delta_ms, scenehub_control_now_ms());
    }
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

    err = scenehub_control_require_room(room_id);
    if (err == ESP_OK) {
        err = gm_room_session_finish(room_id, scenehub_control_now_ms());
    }
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

    err = scenehub_control_require_room(room_id);
    if (err == ESP_OK) {
        err = gm_room_session_set_hint(room_id, message, scenehub_control_now_ms());
    }
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

    err = scenehub_control_require_room(room_id);
    if (err == ESP_OK) {
        err = gm_room_session_clear_hint(room_id, scenehub_control_now_ms());
    }
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
