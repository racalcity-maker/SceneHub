#include "orchestrator_registry_internal.h"

#include <string.h>

#include "esp_heap_caps.h"

static void *orch_room_alloc(size_t size)
{
    void *ptr = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
        ptr = heap_caps_calloc(1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return ptr;
}

static void orch_room_view_branch_wait_skip(const gm_room_session_t *session,
                                            const gm_room_scenario_branch_runtime_t *runtime,
                                            bool *out_allowed,
                                            char *out_label,
                                            size_t out_label_len)
{
    const room_scenario_step_t *step = NULL;
    bool allowed = false;

    if (!out_allowed || !out_label || out_label_len == 0) {
        return;
    }
    out_label[0] = '\0';
    if (!session || !runtime) {
        *out_allowed = false;
        return;
    }

    allowed = runtime->wait_operator_skip_allowed;
    if (runtime->wait_operator_skip_label[0]) {
        quest_str_copy(out_label, out_label_len, runtime->wait_operator_skip_label);
    }

    if (runtime->scenario_state == GM_ROOM_SCENARIO_WAITING &&
        runtime->wait_type != GM_ROOM_SCENARIO_WAIT_NONE &&
        runtime->current_step_index < session->running_scenario.step_count) {
        step = &session->running_scenario.steps[runtime->current_step_index];
        if (step->allow_operator_skip) {
            allowed = true;
            if (!out_label[0]) {
                quest_str_copy(out_label,
                            out_label_len,
                            step->operator_skip_label[0] ? step->operator_skip_label : "Skip wait");
            }
        }
    }

    *out_allowed = allowed;
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
    }
}

void orch_room_view_enrich_from_sessions(orch_registry_snapshot_t *snapshot)
{
    uint64_t now_ms = orch_now_ms();
    gm_room_session_t *session = NULL;
    if (!snapshot) {
        return;
    }
    session = orch_room_alloc(sizeof(*session));
    if (!session) {
        return;
    }
    for (uint8_t i = 0; i < snapshot->room_count; ++i) {
        orch_room_entry_t *room = &snapshot->rooms[i];
        memset(session, 0, sizeof(*session));
        if (gm_room_session_get(room->room_id, session) != ESP_OK) {
            quest_str_copy(room->session_state, sizeof(room->session_state), "idle");
            quest_str_copy(room->timer_state, sizeof(room->timer_state), "idle");
            continue;
        }
        room->session_present = true;
        room->session_started_at_ms = session->started_at_ms;
        room->timer_duration_ms = session->timer.duration_ms;
        room->timer_remaining_ms = gm_timer_get_remaining(&session->timer, now_ms);
        room->hint_active = session->hint.active;
        room->hint_sent_count = session->hint.sent_count;
        room->selected_scenario_generation = session->selected_scenario_generation;
        room->selected_profile_duration_ms = session->selected_profile_duration_ms;
        quest_str_copy(room->session_state, sizeof(room->session_state), orch_session_state_str(session->state));
        quest_str_copy(room->timer_state, sizeof(room->timer_state), orch_timer_state_str(session->timer.state));
        quest_str_copy(room->hint_message, sizeof(room->hint_message), session->hint.message);
        quest_str_copy(room->selected_profile_id,
                    sizeof(room->selected_profile_id),
                    session->selected_profile_id);
        quest_str_copy(room->selected_profile_name,
                    sizeof(room->selected_profile_name),
                    session->selected_profile_name);
        quest_str_copy(room->selected_profile_scenario_id,
                    sizeof(room->selected_profile_scenario_id),
                    session->selected_profile_scenario_id);
        quest_str_copy(room->selected_scenario_id,
                    sizeof(room->selected_scenario_id),
                    session->selected_scenario_id);
        quest_str_copy(room->selected_scenario_name,
                    sizeof(room->selected_scenario_name),
                    session->selected_scenario_name);
        if (session->running_scenario_valid) {
            quest_str_copy(room->running_scenario_id,
                        sizeof(room->running_scenario_id),
                        session->running_scenario.id);
            quest_str_copy(room->running_scenario_name,
                        sizeof(room->running_scenario_name),
                        session->running_scenario.name);
            room->running_scenario_generation = session->running_scenario_generation;
        }
        room->scenario_runtime_state = session->scenario_state;
        room->scenario_current_step_index = session->current_step_index;
        room->scenario_wait_type = session->wait_type;
        room->scenario_wait_until_ms = session->wait_until_ms;
        room->scenario_wait_started_at_ms = session->wait_started_at_ms;
        quest_str_copy(room->scenario_wait_event_type,
                    sizeof(room->scenario_wait_event_type),
                    session->wait_event_type);
        quest_str_copy(room->scenario_wait_source_id,
                    sizeof(room->scenario_wait_source_id),
                    session->wait_source_id);
        room->scenario_wait_event_count = session->wait_event_count;
        for (uint8_t event_index = 0;
             event_index < session->wait_event_count &&
             event_index < ORCH_ROOM_SCENARIO_MAX_EVENT_REFS;
             ++event_index) {
            quest_str_copy(room->scenario_wait_events[event_index].event_type,
                        sizeof(room->scenario_wait_events[event_index].event_type),
                        session->wait_events[event_index].event_type);
            quest_str_copy(room->scenario_wait_events[event_index].source_id,
                        sizeof(room->scenario_wait_events[event_index].source_id),
                        session->wait_events[event_index].source_id);
        }
        room->scenario_wait_flag_count = session->wait_flag_count;
        for (uint8_t flag_index = 0;
             flag_index < session->wait_flag_count &&
             flag_index < ORCH_ROOM_SCENARIO_MAX_FLAG_REFS;
             ++flag_index) {
            quest_str_copy(room->scenario_wait_flags[flag_index].name,
                        sizeof(room->scenario_wait_flags[flag_index].name),
                        session->wait_flags[flag_index].name);
            room->scenario_wait_flags[flag_index].value = session->wait_flags[flag_index].value;
        }
        quest_str_copy(room->scenario_wait_operator_prompt,
                    sizeof(room->scenario_wait_operator_prompt),
                    session->wait_operator_prompt);
        quest_str_copy(room->scenario_wait_operator_label,
                    sizeof(room->scenario_wait_operator_label),
                    session->wait_operator_label);
        room->scenario_wait_operator_skip_allowed = session->wait_operator_skip_allowed;
        quest_str_copy(room->scenario_wait_operator_skip_label,
                    sizeof(room->scenario_wait_operator_skip_label),
                    session->wait_operator_skip_label);
        quest_str_copy(room->scenario_operator_message,
                    sizeof(room->scenario_operator_message),
                    session->scenario_operator_message);
        room->scenario_flag_count = session->scenario_flag_count;
        memcpy(room->scenario_flags,
               session->scenario_flags,
               sizeof(room->scenario_flags));
        room->scenario_branch_count = session->branch_runtime_count;
        for (uint8_t branch_index = 0;
             branch_index < session->branch_runtime_count &&
             branch_index < ORCH_ROOM_SCENARIO_MAX_BRANCHES;
             ++branch_index) {
            const gm_room_scenario_branch_runtime_t *runtime = &session->branch_runtimes[branch_index];
            const room_scenario_branch_t *branch =
                (session->running_scenario.branch_count > branch_index)
                    ? &session->running_scenario.branches[branch_index]
                    : NULL;
            orch_room_scenario_branch_entry_t *out_branch = &room->scenario_branches[branch_index];
            quest_str_copy(out_branch->id,
                        sizeof(out_branch->id),
                        branch && branch->id[0] ? branch->id : "main");
            quest_str_copy(out_branch->name,
                        sizeof(out_branch->name),
                        branch && branch->name[0] ? branch->name : "Main");
            out_branch->active = runtime->active;
            out_branch->type = runtime->type;
            out_branch->required_for_completion = runtime->required_for_completion;
            out_branch->priority = runtime->priority;
            out_branch->cooldown_ms = runtime->cooldown_ms;
            out_branch->cooldown_until_ms = runtime->cooldown_until_ms;
            out_branch->max_fire_count = runtime->max_fire_count;
            out_branch->fire_count = runtime->fire_count;
            out_branch->run_once = runtime->run_once;
            out_branch->fired_once = runtime->fired_once;
            out_branch->reentry_mode = runtime->reentry_mode;
            out_branch->pending_trigger = runtime->pending_trigger;
            out_branch->step_start_index = runtime->step_start_index;
            out_branch->step_count = runtime->step_count;
            out_branch->current_step_index = runtime->current_step_index;
            out_branch->state = runtime->scenario_state;
            out_branch->wait_type = runtime->wait_type;
            orch_room_view_branch_wait_skip(session,
                                            runtime,
                                            &out_branch->wait_operator_skip_allowed,
                                            out_branch->wait_operator_skip_label,
                                            sizeof(out_branch->wait_operator_skip_label));
        }
        quest_str_copy(room->scenario_last_error,
                    sizeof(room->scenario_last_error),
                    session->scenario_last_error);
    }
    heap_caps_free(session);
}
