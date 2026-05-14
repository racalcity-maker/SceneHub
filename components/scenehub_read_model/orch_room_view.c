#include "orchestrator_registry_internal.h"

#include <string.h>

static void orch_room_view_add_scenario_device_id(orch_room_entry_t *room, const char *device_id)
{
    if (!room || !device_id || !device_id[0]) {
        return;
    }
    for (uint8_t i = 0;
         i < room->scenario_device_count && i < ORCH_ROOM_SCENARIO_MAX_DEVICE_REFS;
         ++i) {
        if (strcmp(room->scenario_device_ids[i], device_id) == 0) {
            return;
        }
    }
    if (room->scenario_device_count >= ORCH_ROOM_SCENARIO_MAX_DEVICE_REFS) {
        return;
    }
    quest_str_copy(room->scenario_device_ids[room->scenario_device_count],
                   sizeof(room->scenario_device_ids[room->scenario_device_count]),
                   device_id);
    room->scenario_device_count++;
}

bool orch_room_view_has_scenario_device(const orch_room_entry_t *room, const char *device_id)
{
    if (!room || !device_id || !device_id[0]) {
        return false;
    }
    for (uint8_t i = 0;
         i < room->scenario_device_count && i < ORCH_ROOM_SCENARIO_MAX_DEVICE_REFS;
         ++i) {
        if (strcmp(room->scenario_device_ids[i], device_id) == 0) {
            return true;
        }
    }
    return false;
}

static void orch_room_view_collect_scenario_step_devices(orch_room_entry_t *room,
                                                         const room_scenario_step_t *step)
{
    if (!room || !step) {
        return;
    }
    switch (step->type) {
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND:
        orch_room_view_add_scenario_device_id(room, step->data.device_command.device_id);
        break;
    case ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT:
        orch_room_view_add_scenario_device_id(room, step->data.wait_device_event.device_id);
        break;
    default:
        break;
    }
}

static void orch_room_view_collect_scenario_devices(orch_room_entry_t *room,
                                                    const room_scenario_t *scenario)
{
    if (!room || !scenario) {
        return;
    }
    room->scenario_device_count = 0;
    for (size_t i = 0; i < scenario->step_count && i < ROOM_SCENARIO_MAX_STEPS; ++i) {
        orch_room_view_collect_scenario_step_devices(room, &scenario->steps[i]);
    }
}

static void orch_room_view_fill_scenario_devices(orch_room_entry_t *room,
                                                 const gm_room_session_projection_view_t *view)
{
    room_scenario_t *scenario = NULL;
    const char *scenario_id = NULL;

    if (!room || !view) {
        return;
    }
    room->scenario_device_count = 0;
    if (view->scenario_device_count > 0) {
        for (uint8_t i = 0;
             i < view->scenario_device_count && i < ORCH_ROOM_SCENARIO_MAX_DEVICE_REFS;
             ++i) {
            orch_room_view_add_scenario_device_id(room, view->scenario_device_ids[i]);
        }
        return;
    }
    if (view->selected.running_scenario_valid) {
        return;
    }
    scenario_id = view->selected.selected_profile_scenario_id[0]
                      ? view->selected.selected_profile_scenario_id
                      : view->selected.selected_scenario_id;
    if (!scenario_id || !scenario_id[0]) {
        return;
    }
    scenario = orch_scratch_room_scenario();
    if (!scenario) {
        return;
    }
    if (room_scenario_get(scenario_id, scenario) != ESP_OK) {
        return;
    }
    orch_room_view_collect_scenario_devices(room, scenario);
}

static orch_room_scenario_step_runtime_state_t
orch_room_view_step_state_from_gm(gm_room_scenario_step_runtime_state_t state)
{
    switch (state) {
    case GM_ROOM_SCENARIO_STEP_STATE_CURRENT:
        return ORCH_ROOM_SCENARIO_STEP_STATE_CURRENT;
    case GM_ROOM_SCENARIO_STEP_STATE_WAITING:
        return ORCH_ROOM_SCENARIO_STEP_STATE_WAITING;
    case GM_ROOM_SCENARIO_STEP_STATE_DONE:
        return ORCH_ROOM_SCENARIO_STEP_STATE_DONE;
    case GM_ROOM_SCENARIO_STEP_STATE_ERROR:
        return ORCH_ROOM_SCENARIO_STEP_STATE_ERROR;
    case GM_ROOM_SCENARIO_STEP_STATE_SKIPPED:
        return ORCH_ROOM_SCENARIO_STEP_STATE_SKIPPED;
    case GM_ROOM_SCENARIO_STEP_STATE_PENDING:
    default:
        return ORCH_ROOM_SCENARIO_STEP_STATE_PENDING;
    }
}

static void orch_room_view_sync_branch_labels(orch_room_scenario_branch_entry_t *branch)
{
    if (!branch) {
        return;
    }
    quest_str_copy(branch->current_step_state_text,
                   sizeof(branch->current_step_state_text),
                   orch_room_scenario_step_state_str(branch->current_step_state));
    quest_str_copy(branch->state_text,
                   sizeof(branch->state_text),
                   orch_room_scenario_runtime_state_str(branch->state));
    quest_str_copy(branch->wait_type_text,
                   sizeof(branch->wait_type_text),
                   orch_room_scenario_wait_type_str(branch->wait_type));
}

static void orch_room_view_sync_room_labels(orch_room_entry_t *room)
{
    if (!room) {
        return;
    }
    quest_str_copy(room->health_text, sizeof(room->health_text), orch_health_str(room->health));
    quest_str_copy(room->scenario_runtime_state_text,
                   sizeof(room->scenario_runtime_state_text),
                   orch_room_scenario_runtime_state_str(room->scenario_runtime_state));
    quest_str_copy(room->scenario_wait_type_text,
                   sizeof(room->scenario_wait_type_text),
                   orch_room_scenario_wait_type_str(room->scenario_wait_type));
    for (uint8_t i = 0;
         i < room->scenario_branch_count && i < ORCH_ROOM_SCENARIO_MAX_BRANCHES;
         ++i) {
        orch_room_view_sync_branch_labels(&room->scenario_branches[i]);
    }
}

static void orch_room_view_fill_from_projection(orch_registry_snapshot_t *snapshot,
                                                orch_room_entry_t *room,
                                                const gm_room_session_projection_view_t *view,
                                                uint64_t now_ms)
{
    (void)now_ms;
    if (!room || !view) {
        return;
    }

    room->session_present = true;
    room->session_started_at_ms = view->timer.started_at_ms;
    room->timer_duration_ms = view->timer.duration_ms;
    room->timer_remaining_ms = view->timer.remaining_ms;
    room->hint_active = view->timer.hint_active;
    room->hint_sent_count = view->timer.hint_count;
    room->selected_scenario_generation = view->selected.selected_scenario_generation;
    room->selected_profile_duration_ms = view->selected.selected_profile_duration_ms;
    quest_str_copy(room->session_state,
                   sizeof(room->session_state),
                   orch_session_state_str(view->timer.session_state));
    quest_str_copy(room->timer_state,
                   sizeof(room->timer_state),
                   orch_timer_state_str(view->timer.timer_state));
    if (snapshot && view->timer.session_active) {
        snapshot->active_session_count++;
    }
    if (snapshot && view->timer.hint_active) {
        snapshot->active_hint_count++;
    }
    quest_str_copy(room->hint_message, sizeof(room->hint_message), view->timer.hint_text);
    quest_str_copy(room->selected_profile_id,
                   sizeof(room->selected_profile_id),
                   view->selected.selected_profile_id);
    quest_str_copy(room->selected_profile_name,
                   sizeof(room->selected_profile_name),
                   view->selected.selected_profile_name);
    quest_str_copy(room->selected_profile_scenario_id,
                   sizeof(room->selected_profile_scenario_id),
                   view->selected.selected_profile_scenario_id);
    quest_str_copy(room->selected_scenario_id,
                   sizeof(room->selected_scenario_id),
                   view->selected.selected_scenario_id);
    quest_str_copy(room->selected_scenario_name,
                   sizeof(room->selected_scenario_name),
                   view->selected.selected_scenario_name);
    if (view->selected.running_scenario_valid) {
        quest_str_copy(room->running_scenario_id,
                       sizeof(room->running_scenario_id),
                       view->selected.running_scenario_id);
        quest_str_copy(room->running_scenario_name,
                       sizeof(room->running_scenario_name),
                       view->selected.running_scenario_name);
        room->running_scenario_generation = view->selected.running_scenario_generation;
    }
    room->scenario_runtime_state = orch_runtime_state_from_gm(view->runtime.scenario_state);
    room->scenario_current_step_index = view->runtime.current_step_index;
    room->scenario_total_steps = view->runtime.total_steps;
    room->scenario_done_steps = view->runtime.done_steps;
    quest_str_copy(room->scenario_current_step_text,
                   sizeof(room->scenario_current_step_text),
                   view->runtime.current_step_text);
    quest_str_copy(room->scenario_wait_summary,
                   sizeof(room->scenario_wait_summary),
                   view->runtime.wait_summary);
    room->scenario_wait_type = orch_wait_type_from_gm(view->runtime.wait_type);
    room->scenario_wait_until_ms = view->runtime.wait_until_ms;
    room->scenario_wait_started_at_ms = view->runtime.wait_started_at_ms;
    quest_str_copy(room->scenario_wait_event_type,
                   sizeof(room->scenario_wait_event_type),
                   view->runtime.wait_event_type);
    quest_str_copy(room->scenario_wait_source_id,
                   sizeof(room->scenario_wait_source_id),
                   view->runtime.wait_source_id);
    room->scenario_wait_event_count = view->runtime.wait_event_count;
    for (uint8_t event_index = 0;
         event_index < view->runtime.wait_event_count &&
         event_index < ORCH_ROOM_SCENARIO_MAX_EVENT_REFS;
         ++event_index) {
        quest_str_copy(room->scenario_wait_events[event_index].event_type,
                       sizeof(room->scenario_wait_events[event_index].event_type),
                       view->runtime.wait_events[event_index].event_type);
        quest_str_copy(room->scenario_wait_events[event_index].source_id,
                       sizeof(room->scenario_wait_events[event_index].source_id),
                       view->runtime.wait_events[event_index].source_id);
    }
    room->scenario_wait_flag_count = view->runtime.wait_flag_count;
    for (uint8_t flag_index = 0;
         flag_index < view->runtime.wait_flag_count &&
         flag_index < ORCH_ROOM_SCENARIO_MAX_FLAG_REFS;
         ++flag_index) {
        quest_str_copy(room->scenario_wait_flags[flag_index].name,
                       sizeof(room->scenario_wait_flags[flag_index].name),
                       view->runtime.wait_flags[flag_index].name);
        room->scenario_wait_flags[flag_index].value = view->runtime.wait_flags[flag_index].value;
    }
    quest_str_copy(room->scenario_wait_operator_prompt,
                   sizeof(room->scenario_wait_operator_prompt),
                   view->runtime.wait_operator_prompt);
    quest_str_copy(room->scenario_wait_operator_label,
                   sizeof(room->scenario_wait_operator_label),
                   view->runtime.wait_operator_label);
    room->scenario_wait_operator_skip_allowed = view->runtime.wait_operator_skip_allowed;
    quest_str_copy(room->scenario_wait_operator_skip_label,
                   sizeof(room->scenario_wait_operator_skip_label),
                   view->runtime.wait_operator_skip_label);
    quest_str_copy(room->scenario_operator_message,
                   sizeof(room->scenario_operator_message),
                   view->runtime.scenario_operator_message);
    orch_room_view_fill_scenario_devices(room, view);
    room->scenario_flag_count = view->runtime.scenario_flag_count;
    for (uint8_t flag_index = 0;
         flag_index < view->runtime.scenario_flag_count &&
         flag_index < ORCH_ROOM_SCENARIO_MAX_FLAGS;
         ++flag_index) {
        quest_str_copy(room->scenario_flags[flag_index].name,
                       sizeof(room->scenario_flags[flag_index].name),
                       view->runtime.scenario_flags[flag_index].name);
        room->scenario_flags[flag_index].value = view->runtime.scenario_flags[flag_index].value;
    }
    room->scenario_branch_count = view->branch_count;
    for (uint8_t branch_index = 0;
         branch_index < view->branch_count &&
         branch_index < ORCH_ROOM_SCENARIO_MAX_BRANCHES;
         ++branch_index) {
        const gm_room_session_branch_runtime_view_t *branch = &view->branches[branch_index];
        orch_room_scenario_branch_entry_t *out_branch = &room->scenario_branches[branch_index];
        quest_str_copy(out_branch->id,
                       sizeof(out_branch->id),
                       branch->id[0] ? branch->id : "main");
        quest_str_copy(out_branch->name,
                       sizeof(out_branch->name),
                       branch->name[0] ? branch->name : "Main");
        out_branch->active = branch->active;
        out_branch->type = branch->type;
        quest_str_copy(out_branch->type_text,
                       sizeof(out_branch->type_text),
                       room_scenario_branch_type_to_str(out_branch->type));
        out_branch->required_for_completion = branch->required_for_completion;
        out_branch->priority = branch->priority;
        out_branch->cooldown_ms = branch->cooldown_ms;
        out_branch->cooldown_until_ms = branch->cooldown_until_ms;
        out_branch->max_fire_count = branch->max_fire_count;
        out_branch->fire_count = branch->fire_count;
        out_branch->run_once = branch->run_once;
        out_branch->fired_once = branch->fired_once;
        out_branch->reentry_mode = branch->reentry_mode;
        quest_str_copy(out_branch->reentry_mode_text,
                       sizeof(out_branch->reentry_mode_text),
                       room_scenario_reentry_mode_to_str(out_branch->reentry_mode));
        out_branch->pending_trigger = branch->pending_trigger;
        out_branch->step_start_index = branch->step_start_index;
        out_branch->step_count = branch->step_count;
        out_branch->current_step_index = branch->current_step_index;
        out_branch->current_local_step_index = branch->current_local_step_index;
        out_branch->done_steps = branch->done_steps;
        out_branch->total_steps = branch->total_steps;
        out_branch->failed_step_index = branch->failed_step_index;
        out_branch->current_step_state =
            orch_room_view_step_state_from_gm(branch->current_step_state);
        out_branch->state = orch_runtime_state_from_gm(branch->scenario_state);
        out_branch->wait_type = orch_wait_type_from_gm(branch->wait_type);
        quest_str_copy(out_branch->current_step_text,
                       sizeof(out_branch->current_step_text),
                       branch->current_step_text);
        quest_str_copy(out_branch->wait_summary,
                       sizeof(out_branch->wait_summary),
                       branch->wait_summary);
        out_branch->wait_until_ms = branch->wait_until_ms;
        out_branch->wait_started_at_ms = branch->wait_started_at_ms;
        out_branch->wait_operator_skip_allowed = branch->wait_operator_skip_allowed;
        quest_str_copy(out_branch->wait_operator_skip_label,
                       sizeof(out_branch->wait_operator_skip_label),
                       branch->wait_operator_skip_label);
        orch_room_view_sync_branch_labels(out_branch);
    }
    quest_str_copy(room->scenario_last_error,
                   sizeof(room->scenario_last_error),
                   view->runtime.scenario_last_error);
    orch_room_view_sync_room_labels(room);
}

orch_room_entry_t *orch_room_view_find_room(orch_registry_snapshot_t *snapshot, const char *room_id)
{
    if (!snapshot || !room_id || !room_id[0]) {
        return NULL;
    }
    for (uint8_t i = 0; i < snapshot->room_count; ++i) {
        if (strcmp(snapshot->rooms[i].room_id, room_id) == 0) {
            return &snapshot->rooms[i];
        }
    }
    return NULL;
}

orch_room_entry_t *orch_room_view_ensure_room(orch_registry_snapshot_t *snapshot, const char *room_id)
{
    orch_room_entry_t *room = orch_room_view_find_room(snapshot, room_id);
    if (room || !snapshot || snapshot->room_count >= ORCH_REGISTRY_MAX_ROOMS) {
        return room;
    }
    room = &snapshot->rooms[snapshot->room_count++];
    memset(room, 0, sizeof(*room));
    quest_str_copy(room->room_id, sizeof(room->room_id), room_id);
    quest_str_copy(room->title, sizeof(room->title), room_id);
    room->sort_order = (uint16_t)(snapshot->room_count - 1);
    room->health = ORCH_HEALTH_OK;
    orch_room_view_sync_room_labels(room);
    return room;
}

void orch_room_view_collect_rooms(orch_registry_snapshot_t *snapshot)
{
    if (!snapshot) {
        return;
    }
    (void)room_catalog_init();
    (void)room_catalog_refresh();
    size_t catalog_count = room_catalog_count();
    for (size_t i = 0; i < catalog_count; ++i) {
        room_catalog_entry_t room_info = {0};
        if (room_catalog_get(i, &room_info) != ESP_OK) {
            continue;
        }
        orch_room_entry_t *room = orch_room_view_ensure_room(snapshot, room_info.room_id);
        if (!room) {
            continue;
        }
        if (room_info.name[0]) {
            quest_str_copy(room->title, sizeof(room->title), room_info.name);
        }
    }
    for (uint8_t i = 0; i < snapshot->device_count; ++i) {
        orch_device_entry_t *device = &snapshot->devices[i];
        const char *room_id = device->room_id[0] ? device->room_id : orch_default_room_id();
        if (strcmp(room_id, orch_default_room_id()) == 0) {
            continue;
        }
        orch_room_entry_t *room = orch_room_view_ensure_room(snapshot, room_id);
        if (!room) {
            continue;
        }
        room->device_count++;
        if (orch_runtime_is_active(device->runtime_state)) {
            room->active_device_count++;
        }
        if (device->health == ORCH_HEALTH_FAULT) {
            room->health = ORCH_HEALTH_FAULT;
        } else if (device->health == ORCH_HEALTH_DEGRADED && room->health != ORCH_HEALTH_FAULT) {
            room->health = ORCH_HEALTH_DEGRADED;
        }
        quest_str_copy(room->health_text, sizeof(room->health_text), orch_health_str(room->health));
    }
}

void orch_room_view_enrich_from_sessions(orch_registry_snapshot_t *snapshot)
{
    uint64_t now_ms = orch_now_ms();
    gm_room_session_projection_view_t view = {0};
    if (!snapshot) {
        return;
    }
    for (uint8_t i = 0; i < snapshot->room_count; ++i) {
        orch_room_entry_t *room = &snapshot->rooms[i];
        memset(&view, 0, sizeof(view));
        if (gm_room_session_get_projection_view(room->room_id, now_ms, &view) != ESP_OK) {
            quest_str_copy(room->session_state, sizeof(room->session_state), "idle");
            quest_str_copy(room->timer_state, sizeof(room->timer_state), "idle");
            orch_room_view_sync_room_labels(room);
            continue;
        }
        orch_room_view_fill_from_projection(snapshot, room, &view, now_ms);
    }
}

esp_err_t orch_room_view_load_runtime_room_with_session(const char *room_id,
                                                        const gm_room_session_t *session,
                                                        orch_room_entry_t *out)
{
    room_catalog_entry_t room_info = {0};
    gm_room_session_projection_view_t view = {0};

    if (!room_id || !room_id[0] || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (room_catalog_init() != ESP_OK ||
        room_catalog_refresh() != ESP_OK ||
        room_catalog_find(room_id, &room_info) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    memset(out, 0, sizeof(*out));
    quest_str_copy(out->room_id, sizeof(out->room_id), room_info.room_id);
    quest_str_copy(out->title,
                   sizeof(out->title),
                   room_info.name[0] ? room_info.name : room_info.room_id);
    quest_str_copy(out->session_state, sizeof(out->session_state), "idle");
    quest_str_copy(out->timer_state, sizeof(out->timer_state), "idle");
    out->health = ORCH_HEALTH_OK;
    orch_room_view_sync_room_labels(out);
    if (session) {
        gm_room_session_build_projection_view(session, orch_now_ms(), &view);
        orch_room_view_fill_from_projection(NULL, out, &view, orch_now_ms());
    }
    return ESP_OK;
}

esp_err_t orch_room_view_load_runtime_room(const char *room_id, orch_room_entry_t *out)
{
    gm_room_session_projection_view_t view = {0};
    esp_err_t err = ESP_OK;

    if (!room_id || !room_id[0] || !out) {
        return ESP_ERR_INVALID_ARG;
    }

    err = gm_room_session_get_projection_view(room_id, orch_now_ms(), &view);
    if (err == ESP_OK || err == ESP_ERR_NOT_FOUND) {
        err = orch_room_view_load_runtime_room_with_session(room_id,
                                                            NULL,
                                                            out);
        if (err == ESP_OK && view.present) {
            orch_room_view_fill_from_projection(NULL, out, &view, orch_now_ms());
        }
    }
    return err;
}
