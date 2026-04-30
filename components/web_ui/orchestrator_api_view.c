#include "orchestrator_api_view.h"

#include <string.h>

static const char *api_health_str(orch_health_t health)
{
    switch (health) {
    case ORCH_HEALTH_OK:
        return "ok";
    case ORCH_HEALTH_DEGRADED:
        return "degraded";
    case ORCH_HEALTH_FAULT:
        return "fault";
    default:
        return "ok";
    }
}

static const char *api_connectivity_str(orch_connectivity_t connectivity)
{
    switch (connectivity) {
    case ORCH_CONNECTIVITY_ONLINE:
        return "online";
    case ORCH_CONNECTIVITY_OFFLINE:
        return "offline";
    case ORCH_CONNECTIVITY_UNKNOWN:
    default:
        return "unknown";
    }
}

static const char *api_runtime_state_str(orch_runtime_state_t state)
{
    switch (state) {
    case ORCH_RUNTIME_STATE_IDLE:
        return "idle";
    case ORCH_RUNTIME_STATE_ARMED:
        return "armed";
    case ORCH_RUNTIME_STATE_ACTIVE:
        return "active";
    case ORCH_RUNTIME_STATE_PAUSED:
        return "paused";
    case ORCH_RUNTIME_STATE_COMPLETED:
        return "completed";
    case ORCH_RUNTIME_STATE_TIMEOUT:
        return "timeout";
    case ORCH_RUNTIME_STATE_FAILED:
        return "failed";
    case ORCH_RUNTIME_STATE_UNKNOWN:
    default:
        return "unknown";
    }
}

static const char *api_issue_scope_str(orch_issue_scope_t scope)
{
    switch (scope) {
    case ORCH_ISSUE_SCOPE_SYSTEM:
        return "system";
    case ORCH_ISSUE_SCOPE_ROOM:
        return "room";
    case ORCH_ISSUE_SCOPE_DEVICE:
    default:
        return "device";
    }
}

static const char *api_issue_severity_str(orch_issue_severity_t severity)
{
    switch (severity) {
    case ORCH_ISSUE_SEVERITY_INFO:
        return "info";
    case ORCH_ISSUE_SEVERITY_WARNING:
        return "warning";
    case ORCH_ISSUE_SEVERITY_ERROR:
    default:
        return "error";
    }
}

static const char *api_room_scenario_step_type_str(orch_room_scenario_step_type_t type)
{
    switch (type) {
    case ORCH_ROOM_SCENARIO_STEP_WAIT_TIME:
        return "wait_time";
    case ORCH_ROOM_SCENARIO_STEP_OPERATOR_APPROVAL:
        return "operator_approval";
    case ORCH_ROOM_SCENARIO_STEP_DEVICE_COMMAND:
        return "device_command";
    case ORCH_ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT:
        return "wait_device_event";
    case ORCH_ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP:
        return "device_command_group";
    case ORCH_ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE:
        return "show_operator_message";
    case ORCH_ROOM_SCENARIO_STEP_SET_FLAG:
        return "set_flag";
    case ORCH_ROOM_SCENARIO_STEP_WAIT_FLAGS:
        return "wait_flags";
    case ORCH_ROOM_SCENARIO_STEP_WAIT_ANY_DEVICE_EVENT:
        return "wait_any_device_event";
    case ORCH_ROOM_SCENARIO_STEP_WAIT_ALL_DEVICE_EVENTS:
        return "wait_all_device_events";
    case ORCH_ROOM_SCENARIO_STEP_END_GAME:
        return "end_game";
    default:
        return "operator_approval";
    }
}

static esp_err_t api_add_wait_events(cJSON *obj, const orch_room_wait_event_entry_t *events, uint8_t count)
{
    cJSON *array = NULL;
    if (!obj || (!events && count > 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    array = cJSON_CreateArray();
    if (!array) {
        return ESP_ERR_NO_MEM;
    }
    for (uint8_t i = 0; i < count && i < ORCH_ROOM_SCENARIO_MAX_EVENT_REFS; ++i) {
        cJSON *event = cJSON_CreateObject();
        if (!event) {
            cJSON_Delete(array);
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(event, "event_type", events[i].event_type);
        cJSON_AddStringToObject(event, "source_id", events[i].source_id);
        cJSON_AddItemToArray(array, event);
    }
    cJSON_AddItemToObject(obj, "scenario_wait_events", array);
    cJSON_AddNumberToObject(obj, "scenario_wait_event_count", count);
    return ESP_OK;
}

static esp_err_t api_add_flag_refs(cJSON *obj,
                                   const char *array_name,
                                   const char *count_name,
                                   const orch_room_scenario_flag_ref_t *flags,
                                   uint8_t count)
{
    cJSON *array = NULL;
    if (!obj || !array_name || (!flags && count > 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    array = cJSON_CreateArray();
    if (!array) {
        return ESP_ERR_NO_MEM;
    }
    for (uint8_t i = 0; i < count && i < ORCH_ROOM_SCENARIO_MAX_FLAG_REFS; ++i) {
        cJSON *flag = cJSON_CreateObject();
        if (!flag) {
            cJSON_Delete(array);
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(flag, "name", flags[i].name);
        cJSON_AddBoolToObject(flag, "value", flags[i].value);
        cJSON_AddItemToArray(array, flag);
    }
    cJSON_AddItemToObject(obj, array_name, array);
    if (count_name && count_name[0]) {
        cJSON_AddNumberToObject(obj, count_name, count);
    }
    return ESP_OK;
}

static esp_err_t api_add_scenario_flags(cJSON *obj, const orch_room_entry_t *room)
{
    cJSON *flags = NULL;
    if (!obj || !room) {
        return ESP_ERR_INVALID_ARG;
    }
    flags = cJSON_CreateArray();
    if (!flags) {
        return ESP_ERR_NO_MEM;
    }
    for (uint8_t i = 0; i < room->scenario_flag_count && i < GM_ROOM_SCENARIO_MAX_FLAGS; ++i) {
        cJSON *flag = cJSON_CreateObject();
        if (!flag) {
            cJSON_Delete(flags);
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(flag, "name", room->scenario_flags[i].name);
        cJSON_AddBoolToObject(flag, "value", room->scenario_flags[i].value);
        cJSON_AddItemToArray(flags, flag);
    }
    cJSON_AddItemToObject(obj, "scenario_flags", flags);
    cJSON_AddNumberToObject(obj, "scenario_flag_count", room->scenario_flag_count);
    return ESP_OK;
}

static esp_err_t api_add_json_object_string(cJSON *obj, const char *name, const char *json)
{
    cJSON *parsed = NULL;
    if (!obj || !name) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!json || !json[0]) {
        return ESP_OK;
    }
    parsed = cJSON_Parse(json);
    if (!cJSON_IsObject(parsed)) {
        cJSON_Delete(parsed);
        return ESP_ERR_INVALID_ARG;
    }
    cJSON_AddItemToObject(obj, name, parsed);
    return ESP_OK;
}

static const char *api_room_scenario_runtime_state_str(gm_room_scenario_state_t state)
{
    switch (state) {
    case GM_ROOM_SCENARIO_RUNNING:
        return "running";
    case GM_ROOM_SCENARIO_WAITING:
        return "waiting";
    case GM_ROOM_SCENARIO_PAUSED:
        return "paused";
    case GM_ROOM_SCENARIO_DONE:
        return "done";
    case GM_ROOM_SCENARIO_STOPPED:
        return "stopped";
    case GM_ROOM_SCENARIO_COOLDOWN:
        return "cooldown";
    case GM_ROOM_SCENARIO_ERROR:
        return "error";
    case GM_ROOM_SCENARIO_IDLE:
    default:
        return "idle";
    }
}

static const char *api_room_scenario_wait_type_str(gm_room_scenario_wait_type_t wait_type)
{
    switch (wait_type) {
    case GM_ROOM_SCENARIO_WAIT_TIME:
        return "time";
    case GM_ROOM_SCENARIO_WAIT_DEVICE_EVENT:
        return "event";
    case GM_ROOM_SCENARIO_WAIT_ANY_DEVICE_EVENT:
        return "any_events";
    case GM_ROOM_SCENARIO_WAIT_ALL_DEVICE_EVENTS:
        return "all_events";
    case GM_ROOM_SCENARIO_WAIT_OPERATOR:
        return "operator";
    case GM_ROOM_SCENARIO_WAIT_FLAGS:
        return "flags";
    case GM_ROOM_SCENARIO_WAIT_NONE:
    default:
        return "none";
    }
}

static esp_err_t api_add_scenario_branches(cJSON *obj, const orch_room_entry_t *room)
{
    cJSON *branches = cJSON_CreateArray();
    if (!obj || !room || !branches) {
        cJSON_Delete(branches);
        return ESP_ERR_NO_MEM;
    }
    for (uint8_t i = 0;
         i < room->scenario_branch_count && i < ORCH_ROOM_SCENARIO_MAX_BRANCHES;
         ++i) {
        const orch_room_scenario_branch_entry_t *branch = &room->scenario_branches[i];
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            cJSON_Delete(branches);
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddNumberToObject(item, "index", i);
        cJSON_AddStringToObject(item, "id", branch->id);
        cJSON_AddStringToObject(item, "name", branch->name);
        cJSON_AddBoolToObject(item, "active", branch->active);
        cJSON_AddStringToObject(item, "type", room_scenario_branch_type_to_str(branch->type));
        cJSON_AddBoolToObject(item, "required_for_completion", branch->required_for_completion);
        cJSON_AddNumberToObject(item, "cooldown_ms", branch->cooldown_ms);
        cJSON_AddNumberToObject(item, "cooldown_until_ms", branch->cooldown_until_ms);
        cJSON_AddBoolToObject(item, "run_once", branch->run_once);
        cJSON_AddBoolToObject(item, "fired_once", branch->fired_once);
        cJSON_AddNumberToObject(item, "step_start_index", branch->step_start_index);
        cJSON_AddNumberToObject(item, "step_count", branch->step_count);
        cJSON_AddNumberToObject(item, "current_step_index", branch->current_step_index);
        cJSON_AddStringToObject(item, "state", api_room_scenario_runtime_state_str(branch->state));
        cJSON_AddStringToObject(item, "wait_type", api_room_scenario_wait_type_str(branch->wait_type));
        cJSON_AddBoolToObject(item, "wait_operator_skip_allowed", branch->wait_operator_skip_allowed);
        cJSON_AddStringToObject(item, "wait_operator_skip_label", branch->wait_operator_skip_label);
        cJSON_AddItemToArray(branches, item);
    }
    cJSON_AddItemToObject(obj, "scenario_branches", branches);
    cJSON_AddNumberToObject(obj, "scenario_branch_count", room->scenario_branch_count);
    return ESP_OK;
}

static const char *api_room_scenario_validation_level_str(room_scenario_validation_level_t level)
{
    switch (level) {
    case ROOM_SCENARIO_VALIDATION_WARNING:
        return "warning";
    case ROOM_SCENARIO_VALIDATION_ERROR:
    default:
        return "error";
    }
}

static const char *api_control_health_str(const device_control_ingest_device_t *device)
{
    const char *health = NULL;
    if (!device) {
        return "ok";
    }
    health = device->status_health;
    if (health && health[0]) {
        if (strcmp(health, "ok") == 0 || strcmp(health, "normal") == 0) {
            return "ok";
        }
        if (strcmp(health, "warn") == 0 || strcmp(health, "warning") == 0 ||
            strcmp(health, "degraded") == 0) {
            return "degraded";
        }
        if (strcmp(health, "error") == 0 || strcmp(health, "fault") == 0 ||
            strcmp(health, "fatal") == 0) {
            return "fault";
        }
        return health;
    }
    if (device->has_diag && device->diag_level[0]) {
        if (strcmp(device->diag_level, "warn") == 0 || strcmp(device->diag_level, "warning") == 0) {
            return "degraded";
        }
        if (strcmp(device->diag_level, "error") == 0 || strcmp(device->diag_level, "fatal") == 0) {
            return "fault";
        }
    }
    return "ok";
}

static const char *api_control_boot_id(const device_control_ingest_device_t *device)
{
    if (!device) {
        return "";
    }
    if (device->status_boot_id[0]) {
        return device->status_boot_id;
    }
    return device->heartbeat_boot_id;
}

static cJSON *api_build_badges_json(const orch_device_entry_t *device)
{
    cJSON *badges = cJSON_CreateArray();
    if (!badges || !device) {
        return badges;
    }
    if (device->runtime_state != ORCH_RUNTIME_STATE_UNKNOWN &&
        device->runtime_state != ORCH_RUNTIME_STATE_IDLE) {
        cJSON_AddItemToArray(badges, cJSON_CreateString(api_runtime_state_str(device->runtime_state)));
    }
    if (device->health == ORCH_HEALTH_DEGRADED) {
        cJSON_AddItemToArray(badges, cJSON_CreateString("degraded"));
    } else if (device->health == ORCH_HEALTH_FAULT) {
        cJSON_AddItemToArray(badges, cJSON_CreateString("fault"));
    }
    return badges;
}

static cJSON *api_build_rooms_json(const orch_registry_snapshot_t *snapshot)
{
    cJSON *rooms = cJSON_CreateArray();
    if (!rooms || !snapshot) {
        return rooms;
    }
    for (uint8_t i = 0; i < snapshot->room_count; ++i) {
        const orch_room_entry_t *room = &snapshot->rooms[i];
        cJSON *obj = cJSON_CreateObject();
        if (!obj) {
            cJSON_Delete(rooms);
            return NULL;
        }
        cJSON_AddStringToObject(obj, "room_id", room->room_id);
        cJSON_AddStringToObject(obj, "title", room->title);
        cJSON_AddNumberToObject(obj, "sort_order", room->sort_order);
        cJSON_AddStringToObject(obj, "health", api_health_str(room->health));
        cJSON_AddNumberToObject(obj, "device_count", room->device_count);
        cJSON_AddNumberToObject(obj, "active_device_count", room->active_device_count);
        cJSON_AddNumberToObject(obj, "issue_count", room->issue_count);
        cJSON_AddBoolToObject(obj, "session_present", room->session_present);
        cJSON_AddStringToObject(obj, "session_state", room->session_state[0] ? room->session_state : "idle");
        cJSON_AddNumberToObject(obj, "session_started_at_ms", (double)room->session_started_at_ms);
        cJSON_AddStringToObject(obj, "timer_state", room->timer_state[0] ? room->timer_state : "idle");
        cJSON_AddNumberToObject(obj, "timer_duration_ms", room->timer_duration_ms);
        cJSON_AddNumberToObject(obj, "timer_remaining_ms", room->timer_remaining_ms);
        cJSON_AddBoolToObject(obj, "hint_active", room->hint_active);
        cJSON_AddNumberToObject(obj, "hint_sent_count", room->hint_sent_count);
        cJSON_AddStringToObject(obj, "hint_message", room->hint_message);
        cJSON_AddStringToObject(obj, "selected_profile_id", room->selected_profile_id);
        cJSON_AddStringToObject(obj, "selected_profile_name", room->selected_profile_name);
        cJSON_AddStringToObject(obj, "selected_profile_scenario_id", room->selected_profile_scenario_id);
        cJSON_AddNumberToObject(obj, "selected_profile_duration_ms", room->selected_profile_duration_ms);
        cJSON_AddStringToObject(obj, "selected_scenario_id", room->selected_scenario_id);
        cJSON_AddStringToObject(obj, "selected_scenario_name", room->selected_scenario_name);
        cJSON_AddNumberToObject(obj, "selected_scenario_generation", room->selected_scenario_generation);
        cJSON_AddStringToObject(obj, "running_scenario_id", room->running_scenario_id);
        cJSON_AddStringToObject(obj, "running_scenario_name", room->running_scenario_name);
        cJSON_AddNumberToObject(obj, "running_scenario_generation", room->running_scenario_generation);
        cJSON_AddStringToObject(obj,
                                "scenario_runtime_state",
                                api_room_scenario_runtime_state_str(room->scenario_runtime_state));
        cJSON_AddNumberToObject(obj, "scenario_current_step_index", room->scenario_current_step_index);
        cJSON_AddStringToObject(obj,
                                "scenario_wait_type",
                                api_room_scenario_wait_type_str(room->scenario_wait_type));
        cJSON_AddNumberToObject(obj, "scenario_wait_until_ms", room->scenario_wait_until_ms);
        cJSON_AddNumberToObject(obj, "scenario_wait_started_at_ms", room->scenario_wait_started_at_ms);
        cJSON_AddStringToObject(obj, "scenario_wait_event_type", room->scenario_wait_event_type);
        cJSON_AddStringToObject(obj, "scenario_wait_source_id", room->scenario_wait_source_id);
        if (api_add_wait_events(obj, room->scenario_wait_events, room->scenario_wait_event_count) != ESP_OK ||
            api_add_flag_refs(obj,
                              "scenario_wait_flags",
                              "scenario_wait_flag_count",
                              room->scenario_wait_flags,
                              room->scenario_wait_flag_count) != ESP_OK) {
            cJSON_Delete(obj);
            cJSON_Delete(rooms);
            return NULL;
        }
        cJSON_AddStringToObject(obj, "scenario_wait_operator_prompt", room->scenario_wait_operator_prompt);
        cJSON_AddStringToObject(obj, "scenario_wait_operator_label", room->scenario_wait_operator_label);
        cJSON_AddBoolToObject(obj,
                              "scenario_wait_operator_skip_allowed",
                              room->scenario_wait_operator_skip_allowed);
        cJSON_AddStringToObject(obj,
                                "scenario_wait_operator_skip_label",
                                room->scenario_wait_operator_skip_label);
        cJSON_AddStringToObject(obj, "scenario_operator_message", room->scenario_operator_message);
        if (api_add_scenario_flags(obj, room) != ESP_OK ||
            api_add_scenario_branches(obj, room) != ESP_OK) {
            cJSON_Delete(obj);
            cJSON_Delete(rooms);
            return NULL;
        }
        cJSON_AddStringToObject(obj, "scenario_last_error", room->scenario_last_error);
        cJSON_AddItemToArray(rooms, obj);
    }
    return rooms;
}

static cJSON *api_build_devices_json(const orch_registry_snapshot_t *snapshot)
{
    cJSON *devices = cJSON_CreateArray();
    if (!devices || !snapshot) {
        return devices;
    }
    for (uint8_t i = 0; i < snapshot->device_count; ++i) {
        const orch_device_entry_t *device = &snapshot->devices[i];
        cJSON *obj = cJSON_CreateObject();
        cJSON *badges = NULL;
        if (!obj) {
            cJSON_Delete(devices);
            return NULL;
        }
        cJSON_AddStringToObject(obj, "device_id", device->device_id);
        cJSON_AddStringToObject(obj, "client_id", device->client_id);
        cJSON_AddStringToObject(obj, "display_name", device->display_name);
        cJSON_AddStringToObject(obj, "room_id", device->room_id);
        cJSON_AddStringToObject(obj, "kind", "control_contract");
        cJSON_AddStringToObject(obj, "health", api_health_str(device->health));
        cJSON_AddStringToObject(obj, "connectivity", api_connectivity_str(device->connectivity));
        cJSON_AddNumberToObject(obj, "last_seen_ms", (double)device->last_seen_ms);
        cJSON_AddStringToObject(obj, "runtime_state", api_runtime_state_str(device->runtime_state));
        cJSON_AddStringToObject(obj, "state_text", device->state);
        cJSON_AddStringToObject(obj, "fw_version", device->fw_version);
        cJSON_AddStringToObject(obj, "boot_id", device->boot_id);
        cJSON_AddStringToObject(obj, "last_diag_code", device->last_diag_code);
        cJSON_AddStringToObject(obj, "last_diag_message", device->last_diag_message);
        cJSON_AddStringToObject(obj, "last_result_status", device->last_result_status);
        cJSON_AddStringToObject(obj, "last_result_error_code", device->last_result_error_code);
        cJSON_AddBoolToObject(obj, "has_runtime", device->has_runtime);

        badges = api_build_badges_json(device);
        if (!badges) {
            if (badges) {
                cJSON_Delete(badges);
            }
            cJSON_Delete(obj);
            cJSON_Delete(devices);
            return NULL;
        }
        cJSON_AddItemToObject(obj, "badges", badges);
        cJSON_AddItemToArray(devices, obj);
    }
    return devices;
}

static cJSON *api_build_issues_json(const orch_registry_snapshot_t *snapshot)
{
    cJSON *issues = cJSON_CreateArray();
    if (!issues || !snapshot) {
        return issues;
    }
    for (uint8_t i = 0; i < snapshot->issue_count; ++i) {
        const orch_issue_entry_t *issue = &snapshot->issues[i];
        cJSON *obj = cJSON_CreateObject();
        if (!obj) {
            cJSON_Delete(issues);
            return NULL;
        }
        cJSON_AddStringToObject(obj, "issue_id", issue->issue_id);
        cJSON_AddStringToObject(obj, "scope", api_issue_scope_str(issue->scope));
        cJSON_AddStringToObject(obj, "room_id", issue->room_id);
        cJSON_AddStringToObject(obj, "device_id", issue->device_id);
        cJSON_AddStringToObject(obj, "severity", api_issue_severity_str(issue->severity));
        cJSON_AddStringToObject(obj, "code", issue->code);
        cJSON_AddStringToObject(obj, "title", issue->title);
        cJSON_AddStringToObject(obj, "details", issue->details);
        cJSON_AddBoolToObject(obj, "active", issue->active);
        cJSON_AddItemToArray(issues, obj);
    }
    return issues;
}

cJSON *orchestrator_api_view_room_scenarios(const char *room_id,
                                            const orch_room_scenario_detail_t *scenarios,
                                            size_t scenario_count)
{
    if (!room_id || (!scenarios && scenario_count > 0)) {
        return NULL;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON *items = cJSON_CreateArray();
    if (!root || !items) {
        if (root) {
            cJSON_Delete(root);
        }
        if (items) {
            cJSON_Delete(items);
        }
        return NULL;
    }

    cJSON_AddStringToObject(root, "room_id", room_id);
    cJSON_AddNumberToObject(root, "count", (double)scenario_count);
    for (size_t i = 0; i < scenario_count; ++i) {
        const orch_room_scenario_detail_t *scenario = &scenarios[i];
        cJSON *item = cJSON_CreateObject();
        cJSON *steps = cJSON_CreateArray();
        if (!item || !steps) {
            if (item) {
                cJSON_Delete(item);
            }
            if (steps) {
                cJSON_Delete(steps);
            }
            cJSON_Delete(root);
            cJSON_Delete(items);
            return NULL;
        }
        cJSON_AddStringToObject(item, "id", scenario->summary.id);
        cJSON_AddStringToObject(item, "name", scenario->summary.name);
        cJSON_AddNumberToObject(item, "step_count", (double)scenario->summary.step_count);
        cJSON_AddBoolToObject(item, "valid", scenario->summary.valid);
        cJSON_AddNumberToObject(item,
                                "validation_issue_count",
                                (double)scenario->summary.validation_issue_count);
        for (size_t step_index = 0; step_index < scenario->summary.step_count &&
                                    step_index < ORCH_ROOM_SCENARIO_MAX_STEPS; ++step_index) {
            const orch_room_scenario_step_entry_t *step = &scenario->steps[step_index];
            cJSON *step_obj = cJSON_CreateObject();
            if (!step_obj) {
                cJSON_Delete(steps);
                cJSON_Delete(item);
                cJSON_Delete(root);
                cJSON_Delete(items);
                return NULL;
            }
            cJSON_AddStringToObject(step_obj, "id", step->id);
            cJSON_AddStringToObject(step_obj, "label", step->label);
            cJSON_AddStringToObject(step_obj, "type", api_room_scenario_step_type_str(step->type));
            cJSON_AddBoolToObject(step_obj, "enabled", step->enabled);
            cJSON_AddStringToObject(step_obj, "device_id", step->device_id);
            cJSON_AddStringToObject(step_obj, "scenario_id", step->scenario_id);
            cJSON_AddStringToObject(step_obj, "command_id", step->command_id);
            cJSON_AddStringToObject(step_obj, "event_id", step->event_id);
            if (api_add_json_object_string(step_obj, "params", step->params_json) != ESP_OK) {
                cJSON_Delete(step_obj);
                cJSON_Delete(steps);
                cJSON_Delete(item);
                cJSON_Delete(root);
                cJSON_Delete(items);
                return NULL;
            }
            cJSON_AddNumberToObject(step_obj, "duration_ms", step->duration_ms);
            cJSON_AddStringToObject(step_obj, "event_type", step->event_type);
            cJSON_AddStringToObject(step_obj, "source_id", step->source_id);
            cJSON_AddStringToObject(step_obj, "operator_prompt", step->operator_prompt);
            cJSON_AddStringToObject(step_obj, "operator_approve_label", step->operator_approve_label);
            cJSON_AddStringToObject(step_obj, "operator_message", step->operator_message);
            cJSON_AddStringToObject(step_obj, "flag_name", step->flag_name);
            cJSON_AddBoolToObject(step_obj, "flag_value", step->flag_value);
            cJSON *event_refs = cJSON_CreateArray();
            if (!event_refs) {
                cJSON_Delete(step_obj);
                cJSON_Delete(steps);
                cJSON_Delete(item);
                cJSON_Delete(root);
                cJSON_Delete(items);
                return NULL;
            }
            for (uint8_t event_index = 0;
                 event_index < step->event_count && event_index < ORCH_ROOM_SCENARIO_MAX_EVENT_REFS;
                 ++event_index) {
                cJSON *event = cJSON_CreateObject();
                if (!event) {
                    cJSON_Delete(event_refs);
                    cJSON_Delete(step_obj);
                    cJSON_Delete(steps);
                    cJSON_Delete(item);
                    cJSON_Delete(root);
                    cJSON_Delete(items);
                    return NULL;
                }
                cJSON_AddStringToObject(event, "device_id", step->events[event_index].device_id);
                cJSON_AddStringToObject(event, "event_id", step->events[event_index].event_id);
                cJSON_AddItemToArray(event_refs, event);
            }
            cJSON_AddItemToObject(step_obj, "events", event_refs);
            cJSON_AddNumberToObject(step_obj, "event_count", step->event_count);
            if (api_add_flag_refs(step_obj,
                                  "flags",
                                  "flag_count",
                                  step->flags,
                                  step->flag_count) != ESP_OK) {
                cJSON_Delete(step_obj);
                cJSON_Delete(steps);
                cJSON_Delete(item);
                cJSON_Delete(root);
                cJSON_Delete(items);
                return NULL;
            }
            cJSON_AddNumberToObject(step_obj, "command_count", step->command_count);
            cJSON_AddItemToArray(steps, step_obj);
        }
        cJSON_AddItemToObject(item, "steps", steps);
        cJSON *issues = cJSON_CreateArray();
        if (!issues) {
            cJSON_Delete(item);
            cJSON_Delete(root);
            cJSON_Delete(items);
            return NULL;
        }
        for (size_t issue_index = 0;
             issue_index < scenario->summary.validation_issue_count &&
             issue_index < ROOM_SCENARIO_VALIDATION_MAX_ISSUES;
             ++issue_index) {
            const room_scenario_validation_issue_t *issue = &scenario->validation_issues[issue_index];
            cJSON *issue_obj = cJSON_CreateObject();
            if (!issue_obj) {
                cJSON_Delete(issues);
                cJSON_Delete(item);
                cJSON_Delete(root);
                cJSON_Delete(items);
                return NULL;
            }
            cJSON_AddStringToObject(issue_obj,
                                    "level",
                                    api_room_scenario_validation_level_str(issue->level));
            cJSON_AddNumberToObject(issue_obj, "step_index", issue->step_index);
            cJSON_AddStringToObject(issue_obj, "code", issue->code);
            cJSON_AddStringToObject(issue_obj, "message", issue->message);
            cJSON_AddItemToArray(issues, issue_obj);
        }
        cJSON_AddItemToObject(item, "validation_issues", issues);
        cJSON_AddItemToArray(items, item);
    }
    cJSON_AddItemToObject(root, "scenarios", items);
    return root;
}

cJSON *orchestrator_api_view_audit_recent(const orchestrator_audit_entry_t *entries, size_t count)
{
    if (!entries && count > 0) {
        return NULL;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON *items = cJSON_CreateArray();
    if (!root || !items) {
        if (root) {
            cJSON_Delete(root);
        }
        if (items) {
            cJSON_Delete(items);
        }
        return NULL;
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddNumberToObject(root, "count", (double)count);
    for (size_t i = 0; i < count; ++i) {
        const orchestrator_audit_entry_t *entry = &entries[i];
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            cJSON_Delete(root);
            cJSON_Delete(items);
            return NULL;
        }
        cJSON_AddNumberToObject(item, "timestamp_ms", (double)entry->timestamp_ms);
        cJSON_AddStringToObject(item, "source", entry->source);
        cJSON_AddStringToObject(item, "device_id", entry->device_id);
        cJSON_AddStringToObject(item, "action_id", entry->action_id);
        cJSON_AddBoolToObject(item, "success", entry->success);
        cJSON_AddStringToObject(item, "error_code", entry->error_code);
        cJSON_AddItemToArray(items, item);
    }
    cJSON_AddItemToObject(root, "items", items);
    return root;
}

static const char *api_timeline_type_str(orchestrator_timeline_type_t type)
{
    switch (type) {
    case ORCH_TIMELINE_TYPE_DEVICE_STATUS:
        return "device_status";
    case ORCH_TIMELINE_TYPE_RUNTIME_CHANGED:
        return "runtime_changed";
    case ORCH_TIMELINE_TYPE_SCENARIO_TRIGGERED:
        return "scenario_triggered";
    case ORCH_TIMELINE_TYPE_TIMER_CHANGED:
        return "timer_changed";
    case ORCH_TIMELINE_TYPE_DEVICE_ACTION:
        return "device_action";
    case ORCH_TIMELINE_TYPE_ACTION_FAILED:
        return "action_failed";
    case ORCH_TIMELINE_TYPE_CONFIG_CHANGED:
        return "config_changed";
    case ORCH_TIMELINE_TYPE_EVENT:
    default:
        return "event";
    }
}

static const char *api_timeline_severity_str(orchestrator_timeline_severity_t severity)
{
    switch (severity) {
    case ORCH_TIMELINE_SEVERITY_WARNING:
        return "warning";
    case ORCH_TIMELINE_SEVERITY_ERROR:
        return "error";
    case ORCH_TIMELINE_SEVERITY_INFO:
    default:
        return "info";
    }
}

cJSON *orchestrator_api_view_timeline_recent(const orchestrator_timeline_entry_t *entries, size_t count)
{
    if (!entries && count > 0) {
        return NULL;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON *items = cJSON_CreateArray();
    if (!root || !items) {
        if (root) {
            cJSON_Delete(root);
        }
        if (items) {
            cJSON_Delete(items);
        }
        return NULL;
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddNumberToObject(root, "count", (double)count);
    for (size_t i = 0; i < count; ++i) {
        const orchestrator_timeline_entry_t *entry = &entries[i];
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            cJSON_Delete(root);
            cJSON_Delete(items);
            return NULL;
        }
        cJSON_AddNumberToObject(item, "timestamp_ms", (double)entry->timestamp_ms);
        cJSON_AddStringToObject(item, "type", api_timeline_type_str(entry->type));
        cJSON_AddStringToObject(item, "severity", api_timeline_severity_str(entry->severity));
        cJSON_AddStringToObject(item, "source", entry->source);
        cJSON_AddStringToObject(item, "room_id", entry->room_id);
        cJSON_AddStringToObject(item, "device_id", entry->device_id);
        cJSON_AddStringToObject(item, "title", entry->title);
        cJSON_AddStringToObject(item, "details", entry->details);
        cJSON_AddItemToArray(items, item);
    }
    cJSON_AddItemToObject(root, "items", items);
    return root;
}

cJSON *orchestrator_api_view_control_devices(const device_control_ingest_device_t *devices,
                                             size_t count,
                                             uint64_t now_ms)
{
    if (!devices && count > 0) {
        return NULL;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON *items = cJSON_CreateArray();
    if (!root || !items) {
        if (root) {
            cJSON_Delete(root);
        }
        if (items) {
            cJSON_Delete(items);
        }
        return NULL;
    }

    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddNumberToObject(root, "count", (double)count);
    for (size_t i = 0; i < count; ++i) {
        const device_control_ingest_device_t *device = &devices[i];
        cJSON *item = cJSON_CreateObject();
        bool online = device_control_ingest_is_online(device,
                                                       now_ms,
                                                       DEVICE_CONTROL_INGEST_DEFAULT_ONLINE_TIMEOUT_MS);
        if (!item) {
            cJSON_Delete(root);
            cJSON_Delete(items);
            return NULL;
        }
        cJSON_AddStringToObject(item, "device_id", device->device_id);
        cJSON_AddStringToObject(item, "connectivity", online ? "online" : "offline");
        cJSON_AddStringToObject(item, "health", api_control_health_str(device));
        cJSON_AddNumberToObject(item, "last_seen_ms", (double)device->last_seen_ms);
        cJSON_AddStringToObject(item, "fw_version", device->status_fw_version);
        cJSON_AddStringToObject(item, "boot_id", api_control_boot_id(device));
        cJSON_AddStringToObject(item, "mode", device->status_mode);
        cJSON_AddStringToObject(item, "state", device->status_state);
        cJSON_AddBoolToObject(item, "has_heartbeat", device->has_heartbeat);
        cJSON_AddBoolToObject(item, "has_status", device->has_status);
        cJSON_AddBoolToObject(item, "has_diag", device->has_diag);
        cJSON_AddBoolToObject(item, "has_result", device->has_result);
        cJSON_AddItemToArray(items, item);
    }
    cJSON_AddItemToObject(root, "items", items);
    return root;
}

cJSON *orchestrator_api_view_gm_state(const orch_registry_snapshot_t *snapshot)
{
    if (!snapshot) {
        return NULL;
    }
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }
    cJSON *summary = cJSON_AddObjectToObject(root, "summary");
    cJSON *rooms = api_build_rooms_json(snapshot);
    cJSON *devices = api_build_devices_json(snapshot);
    cJSON *issues = api_build_issues_json(snapshot);
    if (!summary || !rooms || !devices || !issues) {
        if (rooms) {
            cJSON_Delete(rooms);
        }
        if (devices) {
            cJSON_Delete(devices);
        }
        if (issues) {
            cJSON_Delete(issues);
        }
        cJSON_Delete(root);
        return NULL;
    }

    uint32_t active_sessions = 0;
    uint32_t active_hints = 0;
    for (uint8_t i = 0; i < snapshot->room_count; ++i) {
        const orch_room_entry_t *room = &snapshot->rooms[i];
        if (strcmp(room->session_state, "running") == 0 || strcmp(room->session_state, "paused") == 0) {
            active_sessions++;
        }
        if (room->hint_active) {
            active_hints++;
        }
    }

    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddNumberToObject(root, "generation", (double)snapshot->generation);
    cJSON_AddStringToObject(root, "active_profile", snapshot->active_profile);
    cJSON_AddNumberToObject(summary, "rooms_total", snapshot->room_count);
    cJSON_AddNumberToObject(summary, "devices_total", snapshot->device_count);
    cJSON_AddNumberToObject(summary, "issues_total", snapshot->issue_count);
    cJSON_AddNumberToObject(summary, "active_sessions", active_sessions);
    cJSON_AddNumberToObject(summary, "active_hints", active_hints);
    cJSON_AddBoolToObject(summary, "has_degraded", snapshot->has_degraded);
    cJSON_AddBoolToObject(summary, "has_fault", snapshot->has_fault);
    cJSON_AddItemToObject(root, "rooms", rooms);
    cJSON_AddItemToObject(root, "devices", devices);
    cJSON_AddItemToObject(root, "issues", issues);
    return root;
}
