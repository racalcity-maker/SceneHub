#include "orchestrator_registry_internal.h"

#include <string.h>

static orch_room_scenario_step_runtime_state_t
orch_runtime_step_state_from_gm(gm_room_scenario_step_runtime_state_t state)
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

static void orch_room_runtime_summary_fill_idle(const char *room_id,
                                                orch_room_runtime_summary_view_t *out)
{
    if (!room_id || !out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    quest_str_copy(out->room_id, sizeof(out->room_id), room_id);
    quest_str_copy(out->session_state, sizeof(out->session_state), "idle");
    quest_str_copy(out->timer_state, sizeof(out->timer_state), "idle");
    quest_str_copy(out->scenario_runtime_state_text,
                   sizeof(out->scenario_runtime_state_text),
                   orch_room_scenario_runtime_state_str(ORCH_ROOM_SCENARIO_RUNTIME_IDLE));
    quest_str_copy(out->scenario_wait_type_text,
                   sizeof(out->scenario_wait_type_text),
                   orch_room_scenario_wait_type_str(ORCH_ROOM_SCENARIO_WAIT_NONE));
    out->runtime_now_ms = orch_now_ms();
}

static void orch_room_runtime_summary_copy_from_read_views(
    const char *room_id,
    uint64_t runtime_now_ms,
    const gm_room_session_timer_view_t *timer,
    const gm_room_session_selected_view_t *selected,
    const gm_room_session_runtime_summary_t *runtime,
    orch_room_runtime_summary_view_t *out)
{
    orch_room_scenario_runtime_state_t runtime_state = ORCH_ROOM_SCENARIO_RUNTIME_IDLE;
    orch_room_scenario_wait_type_t wait_type = ORCH_ROOM_SCENARIO_WAIT_NONE;

    if (!room_id || !timer || !selected || !runtime || !out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    quest_str_copy(out->room_id, sizeof(out->room_id), room_id);
    out->session_present = timer->present;
    quest_str_copy(out->session_state,
                   sizeof(out->session_state),
                   orch_session_state_str(timer->session_state));
    quest_str_copy(out->timer_state,
                   sizeof(out->timer_state),
                   orch_timer_state_str(timer->timer_state));
    out->timer_duration_ms = timer->duration_ms;
    out->timer_remaining_ms = timer->remaining_ms;
    out->hint_active = timer->hint_active;
    out->hint_sent_count = timer->hint_count;
    quest_str_copy(out->hint_message, sizeof(out->hint_message), timer->hint_text);
    quest_str_copy(out->selected_profile_id,
                   sizeof(out->selected_profile_id),
                   selected->selected_profile_id);
    quest_str_copy(out->selected_profile_name,
                   sizeof(out->selected_profile_name),
                   selected->selected_profile_name);
    quest_str_copy(out->selected_profile_scenario_id,
                   sizeof(out->selected_profile_scenario_id),
                   selected->selected_profile_scenario_id);
    quest_str_copy(out->selected_scenario_id,
                   sizeof(out->selected_scenario_id),
                   selected->selected_scenario_id);
    quest_str_copy(out->selected_scenario_name,
                   sizeof(out->selected_scenario_name),
                   selected->selected_scenario_name);
    if (selected->running_scenario_valid) {
        quest_str_copy(out->running_scenario_id,
                       sizeof(out->running_scenario_id),
                       selected->running_scenario_id);
        quest_str_copy(out->running_scenario_name,
                       sizeof(out->running_scenario_name),
                       selected->running_scenario_name);
        out->running_scenario_generation = selected->running_scenario_generation;
    }
    out->runtime_now_ms = runtime_now_ms;
    runtime_state = orch_runtime_state_from_gm(runtime->scenario_state);
    quest_str_copy(out->scenario_runtime_state_text,
                   sizeof(out->scenario_runtime_state_text),
                   orch_room_scenario_runtime_state_str(runtime_state));
    out->scenario_total_steps = runtime->total_steps;
    out->scenario_done_steps = runtime->done_steps;
    quest_str_copy(out->scenario_current_step_text,
                   sizeof(out->scenario_current_step_text),
                   runtime->current_step_text);
    wait_type = orch_wait_type_from_gm(runtime->wait_type);
    quest_str_copy(out->scenario_wait_type_text,
                   sizeof(out->scenario_wait_type_text),
                   orch_room_scenario_wait_type_str(wait_type));
    quest_str_copy(out->scenario_wait_summary,
                   sizeof(out->scenario_wait_summary),
                   runtime->wait_summary);
    out->scenario_wait_until_ms = runtime->wait_until_ms;
    out->scenario_wait_started_at_ms = runtime->wait_started_at_ms;
    quest_str_copy(out->scenario_wait_operator_prompt,
                   sizeof(out->scenario_wait_operator_prompt),
                   runtime->wait_operator_prompt);
    quest_str_copy(out->scenario_wait_operator_label,
                   sizeof(out->scenario_wait_operator_label),
                   runtime->wait_operator_label);
    out->scenario_wait_operator_skip_allowed = runtime->wait_operator_skip_allowed;
    quest_str_copy(out->scenario_wait_operator_skip_label,
                   sizeof(out->scenario_wait_operator_skip_label),
                   runtime->wait_operator_skip_label);
    quest_str_copy(out->scenario_operator_message,
                   sizeof(out->scenario_operator_message),
                   runtime->scenario_operator_message);
    out->scenario_device_count = runtime->scenario_device_count;
    quest_str_copy(out->scenario_last_error,
                   sizeof(out->scenario_last_error),
                   runtime->scenario_last_error);
}

static void orch_room_runtime_detail_copy_from_projection(
    const char *room_id,
    uint64_t runtime_now_ms,
    const gm_room_session_projection_view_t *projection,
    orch_room_runtime_detail_view_t *out)
{
    if (!room_id || !projection || !out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    orch_room_runtime_summary_copy_from_read_views(room_id,
                                                   runtime_now_ms,
                                                   &projection->timer,
                                                   &projection->selected,
                                                   &projection->runtime,
                                                   &out->summary);
    out->scenario_wait_event_count = projection->runtime.wait_event_count;
    for (uint8_t i = 0;
         i < projection->runtime.wait_event_count && i < ORCH_ROOM_SCENARIO_MAX_EVENT_REFS;
         ++i) {
        quest_str_copy(out->scenario_wait_events[i].event_type,
                       sizeof(out->scenario_wait_events[i].event_type),
                       projection->runtime.wait_events[i].event_type);
        quest_str_copy(out->scenario_wait_events[i].source_id,
                       sizeof(out->scenario_wait_events[i].source_id),
                       projection->runtime.wait_events[i].source_id);
    }
    out->scenario_wait_flag_count = projection->runtime.wait_flag_count;
    for (uint8_t i = 0;
         i < projection->runtime.wait_flag_count && i < ORCH_ROOM_SCENARIO_MAX_FLAG_REFS;
         ++i) {
        quest_str_copy(out->scenario_wait_flags[i].name,
                       sizeof(out->scenario_wait_flags[i].name),
                       projection->runtime.wait_flags[i].name);
        out->scenario_wait_flags[i].value = projection->runtime.wait_flags[i].value;
    }
    for (uint8_t i = 0;
         i < projection->scenario_device_count && i < ORCH_ROOM_SCENARIO_MAX_DEVICE_REFS;
         ++i) {
        quest_str_copy(out->scenario_device_ids[i],
                       sizeof(out->scenario_device_ids[i]),
                       projection->scenario_device_ids[i]);
    }
    out->summary.scenario_device_count = projection->scenario_device_count;
    out->scenario_flag_count = projection->runtime.scenario_flag_count;
    for (uint8_t i = 0;
         i < projection->runtime.scenario_flag_count && i < ORCH_ROOM_SCENARIO_MAX_FLAGS;
         ++i) {
        quest_str_copy(out->scenario_flags[i].name,
                       sizeof(out->scenario_flags[i].name),
                       projection->runtime.scenario_flags[i].name);
        out->scenario_flags[i].value = projection->runtime.scenario_flags[i].value;
    }
    out->scenario_branch_count = projection->branch_count;
    for (uint8_t i = 0;
         i < projection->branch_count && i < ORCH_ROOM_SCENARIO_MAX_BRANCHES;
         ++i) {
        const gm_room_session_branch_runtime_view_t *branch = &projection->branches[i];
        orch_room_scenario_branch_entry_t *out_branch = &out->scenario_branches[i];
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
        out_branch->last_variant_index = branch->last_variant_index;
        out_branch->step_start_index = branch->step_start_index;
        out_branch->step_count = branch->step_count;
        out_branch->current_step_index = branch->current_step_index;
        out_branch->current_local_step_index = branch->current_local_step_index;
        out_branch->done_steps = branch->done_steps;
        out_branch->total_steps = branch->total_steps;
        out_branch->failed_step_index = branch->failed_step_index;
        out_branch->current_step_state =
            orch_runtime_step_state_from_gm(branch->current_step_state);
        quest_str_copy(out_branch->current_step_state_text,
                       sizeof(out_branch->current_step_state_text),
                       orch_room_scenario_step_state_str(out_branch->current_step_state));
        out_branch->state = orch_runtime_state_from_gm(branch->scenario_state);
        quest_str_copy(out_branch->state_text,
                       sizeof(out_branch->state_text),
                       orch_room_scenario_runtime_state_str(out_branch->state));
        out_branch->wait_type = orch_wait_type_from_gm(branch->wait_type);
        quest_str_copy(out_branch->wait_type_text,
                       sizeof(out_branch->wait_type_text),
                       orch_room_scenario_wait_type_str(out_branch->wait_type));
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
    }
}

esp_err_t orchestrator_registry_get_room_runtime_summary_view(const char *room_id,
                                                              orch_room_runtime_summary_view_t *out)
{
    esp_err_t err = ESP_OK;
    uint64_t runtime_now_ms = 0;
    room_catalog_entry_t room = {0};
    gm_room_session_timer_view_t timer = {0};
    gm_room_session_selected_view_t selected = {0};
    gm_room_session_runtime_summary_t runtime = {0};

    if (!room_id || !room_id[0] || !out) {
        return ESP_ERR_INVALID_ARG;
    }

    if (room_catalog_init() != ESP_OK ||
        room_catalog_refresh() != ESP_OK ||
        room_catalog_find(room_id, &room) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    runtime_now_ms = orch_now_ms();
    err = gm_room_session_get_read_views(room_id, runtime_now_ms, &timer, &selected, &runtime);
    if (err == ESP_ERR_NOT_FOUND) {
        orch_room_runtime_summary_fill_idle(room.room_id, out);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    orch_room_runtime_summary_copy_from_read_views(room.room_id,
                                                   runtime_now_ms,
                                                   &timer,
                                                   &selected,
                                                   &runtime,
                                                   out);
    return ESP_OK;
}

esp_err_t orchestrator_registry_get_room_runtime_detail_view(const char *room_id,
                                                             bool include_assets,
                                                             orch_room_runtime_detail_view_t *out)
{
    esp_err_t err = ESP_OK;
    gm_room_session_t *session = NULL;
    bool have_session = false;
    room_catalog_entry_t room = {0};
    gm_room_session_projection_view_t projection = {0};
    orch_room_asset_summary_t asset_summary = {0};
    bool have_asset_summary = false;
    bool asset_summary_matches_session = false;
    bool need_asset_summary = false;
    uint64_t runtime_now_ms = 0;
    uint32_t scenario_generation = room_scenario_generation();
    uint32_t device_generation = quest_device_generation();
    uint32_t asset_generation = orch_room_runtime_assets_generation();

    if (!room_id || !room_id[0] || !out) {
        return ESP_ERR_INVALID_ARG;
    }

    if (room_catalog_init() != ESP_OK ||
        room_catalog_refresh() != ESP_OK ||
        room_catalog_find(room_id, &room) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    runtime_now_ms = orch_now_ms();
    err = gm_room_session_get_projection_view(room.room_id, runtime_now_ms, &projection);
    if (err == ESP_ERR_NOT_FOUND) {
        memset(out, 0, sizeof(*out));
        orch_room_runtime_summary_fill_idle(room.room_id, &out->summary);
        orch_room_runtime_detail_assets_apply_summary(out, &(orch_room_asset_summary_t){0});
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    orch_room_runtime_detail_copy_from_projection(room.room_id, runtime_now_ms, &projection, out);
    if (include_assets && out->summary.running_scenario_id[0]) {
        scenario_generation = out->summary.running_scenario_generation
                                  ? out->summary.running_scenario_generation
                                  : scenario_generation;
        need_asset_summary = true;
    }

    if (!need_asset_summary) {
        orch_room_runtime_detail_assets_apply_summary(out, &(orch_room_asset_summary_t){0});
        return ESP_OK;
    }

    if (orch_room_runtime_detail_assets_load_cached(out,
                                                    scenario_generation,
                                                    device_generation,
                                                    asset_generation)) {
        return ESP_OK;
    }

    err = orch_scratch_lock();
    if (err == ESP_OK) {
        char collected_scenario_id[ROOM_SCENARIO_ID_MAX_LEN] = {0};
        uint32_t collected_scenario_generation = 0;

        session = orch_scratch_session();
        have_session = false;
        if (session) {
            memset(session, 0, sizeof(*session));
            if (gm_room_session_get(room_id, session) == ESP_OK) {
                have_session = true;
            }
        }
        have_asset_summary = orch_room_runtime_assets_collect(out->summary.running_scenario_id,
                                                              have_session ? session : NULL,
                                                              orch_scratch_room_scenario(),
                                                              &asset_summary);
        if (have_session && session && session->running_scenario_valid) {
            quest_str_copy(collected_scenario_id,
                           sizeof(collected_scenario_id),
                           session->running_scenario.id);
            collected_scenario_generation = session->running_scenario_generation;
        }
        orch_scratch_unlock();
        asset_summary_matches_session = collected_scenario_id[0] &&
                                        strcmp(collected_scenario_id,
                                               out->summary.running_scenario_id) == 0 &&
                                        collected_scenario_generation == scenario_generation;
    }

    if (have_asset_summary) {
        orch_room_runtime_detail_assets_apply_summary(out, &asset_summary);
        if (have_session) {
            if (asset_summary_matches_session) {
                orch_room_runtime_detail_assets_store_cached(out,
                                                             scenario_generation,
                                                             device_generation,
                                                             asset_generation,
                                                             &asset_summary);
            }
        } else if (room_scenario_generation() == scenario_generation &&
                   quest_device_generation() == device_generation &&
                   orch_room_runtime_assets_generation() == asset_generation) {
            orch_room_runtime_detail_assets_store_cached(out,
                                                         scenario_generation,
                                                         device_generation,
                                                         asset_generation,
                                                         &asset_summary);
        }
    } else {
        orch_room_runtime_detail_assets_apply_summary(out, &(orch_room_asset_summary_t){0});
    }
    if (!out->asset_prepare_state[0]) {
        quest_str_copy(out->asset_prepare_state, sizeof(out->asset_prepare_state), "none");
    }

    return ESP_OK;
}
