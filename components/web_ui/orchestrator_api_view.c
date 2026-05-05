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
    case GM_ROOM_SCENARIO_WAIT_DEVICE_COMMAND_RESULT:
        return "command_result";
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
        cJSON_AddNumberToObject(item, "priority", branch->priority);
        cJSON_AddNumberToObject(item, "cooldown_ms", branch->cooldown_ms);
        cJSON_AddNumberToObject(item, "cooldown_until_ms", branch->cooldown_until_ms);
        cJSON_AddNumberToObject(item, "max_fire_count", branch->max_fire_count);
        cJSON_AddNumberToObject(item, "fire_count", branch->fire_count);
        cJSON_AddBoolToObject(item, "run_once", branch->run_once);
        cJSON_AddBoolToObject(item, "fired_once", branch->fired_once);
        cJSON_AddStringToObject(item, "reentry_mode", room_scenario_reentry_mode_to_str(branch->reentry_mode));
        cJSON_AddBoolToObject(item, "pending_trigger", branch->pending_trigger);
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
