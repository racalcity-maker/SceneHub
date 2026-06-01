#include "orchestrator/orchestrator_api_view.h"

#include <stdint.h>
#include <string.h>

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
    for (uint8_t i = 0; i < room->scenario_flag_count && i < ORCH_ROOM_SCENARIO_MAX_FLAGS; ++i) {
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

static esp_err_t api_add_scenario_device_ids(cJSON *obj, const orch_room_entry_t *room)
{
    cJSON *devices = NULL;
    if (!obj || !room) {
        return ESP_ERR_INVALID_ARG;
    }
    devices = cJSON_CreateArray();
    if (!devices) {
        return ESP_ERR_NO_MEM;
    }
    for (uint8_t i = 0;
         i < room->scenario_device_count && i < ORCH_ROOM_SCENARIO_MAX_DEVICE_REFS;
         ++i) {
        cJSON_AddItemToArray(devices, cJSON_CreateString(room->scenario_device_ids[i]));
    }
    cJSON_AddItemToObject(obj, "scenario_device_ids", devices);
    cJSON_AddNumberToObject(obj, "scenario_device_count", room->scenario_device_count);
    return ESP_OK;
}

static esp_err_t api_add_related_issue_ids(cJSON *obj, const orch_room_entry_t *room)
{
    cJSON *issues = NULL;
    if (!obj || !room) {
        return ESP_ERR_INVALID_ARG;
    }
    issues = cJSON_CreateArray();
    if (!issues) {
        return ESP_ERR_NO_MEM;
    }
    for (uint8_t i = 0;
         i < room->related_issue_count && i < ORCH_REGISTRY_MAX_ISSUES;
         ++i) {
        cJSON_AddItemToArray(issues, cJSON_CreateString(room->related_issue_ids[i]));
    }
    cJSON_AddItemToObject(obj, "related_issue_ids", issues);
    cJSON_AddNumberToObject(obj, "related_issue_count", room->related_issue_count);
    return ESP_OK;
}

static esp_err_t api_add_scenario_branches(cJSON *obj,
                                           const orch_room_entry_t *room)
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
        cJSON_AddStringToObject(item, "type", branch->type_text);
        cJSON_AddBoolToObject(item, "required_for_completion", branch->required_for_completion);
        cJSON_AddNumberToObject(item, "priority", branch->priority);
        cJSON_AddNumberToObject(item, "cooldown_ms", branch->cooldown_ms);
        cJSON_AddNumberToObject(item, "cooldown_until_ms", branch->cooldown_until_ms);
        cJSON_AddNumberToObject(item, "max_fire_count", branch->max_fire_count);
        cJSON_AddNumberToObject(item, "fire_count", branch->fire_count);
        cJSON_AddBoolToObject(item, "run_once", branch->run_once);
        cJSON_AddBoolToObject(item, "fired_once", branch->fired_once);
        cJSON_AddStringToObject(item, "reentry_mode", branch->reentry_mode_text);
        cJSON_AddBoolToObject(item, "pending_trigger", branch->pending_trigger);
        cJSON_AddNumberToObject(item,
                                "last_variant_index",
                                branch->last_variant_index == UINT8_MAX ? -1 : branch->last_variant_index);
        cJSON_AddNumberToObject(item, "step_start_index", branch->step_start_index);
        cJSON_AddNumberToObject(item, "step_count", branch->step_count);
        cJSON_AddNumberToObject(item, "total_steps", branch->total_steps);
        cJSON_AddNumberToObject(item, "current_step_index", branch->current_step_index);
        cJSON_AddNumberToObject(item, "current_step_local_index", branch->current_local_step_index);
        cJSON_AddNumberToObject(item, "done_steps", branch->done_steps);
        cJSON_AddNumberToObject(item, "completed_step_count", branch->done_steps);
        cJSON_AddNumberToObject(item, "failed_step_index", branch->failed_step_index);
        cJSON_AddStringToObject(item, "current_step_text", branch->current_step_text);
        cJSON_AddStringToObject(item, "current_step_state", branch->current_step_state_text);
        cJSON_AddStringToObject(item, "state", branch->state_text);
        cJSON_AddStringToObject(item, "wait_type", branch->wait_type_text);
        cJSON_AddStringToObject(item, "wait_summary", branch->wait_summary);
        cJSON_AddNumberToObject(item, "wait_until_ms", branch->wait_until_ms);
        cJSON_AddNumberToObject(item, "wait_started_at_ms", branch->wait_started_at_ms);
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
    for (uint8_t i = 0;
         i < device->badge_count && i < ORCH_REGISTRY_DEVICE_MAX_BADGES;
         ++i) {
        cJSON_AddItemToArray(badges, cJSON_CreateString(device->badges[i]));
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
        cJSON_AddStringToObject(obj, "health", room->health_text);
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
        cJSON_AddStringToObject(obj, "scenario_runtime_state", room->scenario_runtime_state_text);
        cJSON_AddNumberToObject(obj, "scenario_current_step_index", room->scenario_current_step_index);
        cJSON_AddNumberToObject(obj, "scenario_total_steps", room->scenario_total_steps);
        cJSON_AddNumberToObject(obj, "scenario_done_steps", room->scenario_done_steps);
        cJSON_AddStringToObject(obj, "scenario_current_step_text", room->scenario_current_step_text);
        cJSON_AddStringToObject(obj, "scenario_wait_type", room->scenario_wait_type_text);
        cJSON_AddStringToObject(obj, "scenario_wait_summary", room->scenario_wait_summary);
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
            api_add_scenario_device_ids(obj, room) != ESP_OK ||
            api_add_related_issue_ids(obj, room) != ESP_OK ||
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
        cJSON_AddStringToObject(obj, "health", device->health_text);
        cJSON_AddStringToObject(obj, "connectivity", device->connectivity_text);
        cJSON_AddNumberToObject(obj, "last_seen_ms", (double)device->last_seen_ms);
        cJSON_AddStringToObject(obj, "runtime_state", device->runtime_state_text);
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
        cJSON_AddStringToObject(obj, "scope", issue->scope_text);
        cJSON_AddStringToObject(obj, "room_id", issue->room_id);
        cJSON_AddStringToObject(obj, "device_id", issue->device_id);
        cJSON_AddStringToObject(obj, "severity", issue->severity_text);
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

    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddNumberToObject(root, "generation", (double)snapshot->generation);
    cJSON_AddStringToObject(root, "active_profile", snapshot->active_profile);
    cJSON_AddNumberToObject(summary, "rooms_total", snapshot->room_count);
    cJSON_AddNumberToObject(summary, "devices_total", snapshot->device_count);
    cJSON_AddNumberToObject(summary, "issues_total", snapshot->issue_count);
    cJSON_AddNumberToObject(summary, "active_sessions", snapshot->active_session_count);
    cJSON_AddNumberToObject(summary, "active_hints", snapshot->active_hint_count);
    cJSON_AddBoolToObject(summary, "has_degraded", snapshot->has_degraded);
    cJSON_AddBoolToObject(summary, "has_fault", snapshot->has_fault);
    cJSON_AddItemToObject(root, "rooms", rooms);
    cJSON_AddItemToObject(root, "devices", devices);
    cJSON_AddItemToObject(root, "issues", issues);
    return root;
}

cJSON *orchestrator_api_view_gm_system_summary(const orch_gm_system_summary_t *summary)
{
    cJSON *root = NULL;
    cJSON *summary_obj = NULL;
    if (!summary) {
        return NULL;
    }
    root = cJSON_CreateObject();
    summary_obj = cJSON_CreateObject();
    if (!root || !summary_obj) {
        if (summary_obj) {
            cJSON_Delete(summary_obj);
        }
        if (root) {
            cJSON_Delete(root);
        }
        return NULL;
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddNumberToObject(root, "generation", (double)summary->generation);
    cJSON_AddNumberToObject(summary_obj, "rooms_total", summary->room_count);
    cJSON_AddNumberToObject(summary_obj, "devices_total", summary->device_count);
    cJSON_AddNumberToObject(summary_obj, "online_device_count", summary->online_device_count);
    cJSON_AddNumberToObject(summary_obj, "issues_total", summary->issue_count);
    cJSON_AddNumberToObject(summary_obj, "active_sessions", summary->active_session_count);
    cJSON_AddNumberToObject(summary_obj, "active_hints", summary->active_hint_count);
    cJSON_AddNumberToObject(summary_obj, "degraded_count", summary->degraded_count);
    cJSON_AddNumberToObject(summary_obj, "fault_count", summary->fault_count);
    cJSON_AddNumberToObject(summary_obj, "dropped_critical_events", summary->dropped_critical_events);
    cJSON_AddNumberToObject(summary_obj,
                            "dropped_noncritical_events",
                            summary->dropped_noncritical_events);
    cJSON_AddNumberToObject(summary_obj,
                            "dropped_event_queue_events",
                            summary->dropped_event_queue_events);
    cJSON_AddNumberToObject(summary_obj,
                            "dropped_runtime_queue_events",
                            summary->dropped_runtime_queue_events);
    cJSON_AddBoolToObject(summary_obj, "has_degraded", summary->has_degraded);
    cJSON_AddBoolToObject(summary_obj, "has_fault", summary->has_fault);
    cJSON_AddItemToObject(root, "summary", summary_obj);
    return root;
}
