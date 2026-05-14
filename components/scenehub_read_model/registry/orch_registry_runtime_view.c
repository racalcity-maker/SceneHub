#include "orchestrator_registry_internal.h"

#include <stdio.h>
#include <string.h>

static orch_room_scenario_step_runtime_state_t
orch_room_runtime_branch_step_state(const orch_room_scenario_branch_entry_t *branch,
                                    uint16_t local_index)
{
    if (!branch) {
        return ORCH_ROOM_SCENARIO_STEP_STATE_PENDING;
    }
    if (branch->state == ORCH_ROOM_SCENARIO_RUNTIME_DONE) {
        return ORCH_ROOM_SCENARIO_STEP_STATE_DONE;
    }
    if (local_index < branch->done_steps) {
        return ORCH_ROOM_SCENARIO_STEP_STATE_DONE;
    }
    if (local_index == branch->current_local_step_index) {
        switch (branch->state) {
        case ORCH_ROOM_SCENARIO_RUNTIME_WAITING:
            return ORCH_ROOM_SCENARIO_STEP_STATE_WAITING;
        case ORCH_ROOM_SCENARIO_RUNTIME_ERROR:
            return ORCH_ROOM_SCENARIO_STEP_STATE_ERROR;
        case ORCH_ROOM_SCENARIO_RUNTIME_RUNNING:
        case ORCH_ROOM_SCENARIO_RUNTIME_PAUSED:
            return ORCH_ROOM_SCENARIO_STEP_STATE_CURRENT;
        case ORCH_ROOM_SCENARIO_RUNTIME_DONE:
            return ORCH_ROOM_SCENARIO_STEP_STATE_DONE;
        case ORCH_ROOM_SCENARIO_RUNTIME_IDLE:
        case ORCH_ROOM_SCENARIO_RUNTIME_STOPPED:
        case ORCH_ROOM_SCENARIO_RUNTIME_COOLDOWN:
        default:
            break;
        }
    }
    return ORCH_ROOM_SCENARIO_STEP_STATE_PENDING;
}

static void orch_room_runtime_format_step_text(const room_scenario_step_t *step,
                                               char *out_text,
                                               size_t out_text_len)
{
    if (!out_text || out_text_len == 0) {
        return;
    }
    out_text[0] = '\0';
    if (!step) {
        return;
    }
    if (step->label[0]) {
        quest_str_copy(out_text, out_text_len, step->label);
        return;
    }
    switch (step->type) {
    case ROOM_SCENARIO_STEP_WAIT_TIME:
        quest_str_copy(out_text, out_text_len, "Wait");
        break;
    case ROOM_SCENARIO_STEP_OPERATOR_APPROVAL:
        quest_str_copy(out_text, out_text_len, "Operator approval");
        break;
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND:
        snprintf(out_text,
                 out_text_len,
                 "Command %s",
                 step->data.device_command.device_id[0] ? step->data.device_command.device_id : "device");
        break;
    case ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT:
        snprintf(out_text,
                 out_text_len,
                 "Wait %s",
                 step->data.wait_device_event.device_id[0] ? step->data.wait_device_event.device_id : "event");
        break;
    case ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP:
        quest_str_copy(out_text, out_text_len, "Command group");
        break;
    case ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE:
        quest_str_copy(out_text, out_text_len, "Operator message");
        break;
    case ROOM_SCENARIO_STEP_SET_FLAG:
        snprintf(out_text,
                 out_text_len,
                 "Set flag %s",
                 step->data.set_flag.name[0] ? step->data.set_flag.name : "flag");
        break;
    case ROOM_SCENARIO_STEP_WAIT_FLAGS:
        quest_str_copy(out_text, out_text_len, "Wait flags");
        break;
    case ROOM_SCENARIO_STEP_WAIT_ANY_DEVICE_EVENT:
        quest_str_copy(out_text, out_text_len, "Wait any event");
        break;
    case ROOM_SCENARIO_STEP_WAIT_ALL_DEVICE_EVENTS:
        quest_str_copy(out_text, out_text_len, "Wait all events");
        break;
    case ROOM_SCENARIO_STEP_END_GAME:
        quest_str_copy(out_text, out_text_len, "End game");
        break;
    default:
        quest_str_copy(out_text, out_text_len, "Step");
        break;
    }
}

static void orch_room_runtime_view_fill_branch_steps(orch_room_runtime_view_t *out,
                                                     const gm_room_session_t *session)
{
    uint8_t branch_count = 0;
    if (!out) {
        return;
    }
    branch_count = out->room.scenario_branch_count;
    if (branch_count > ORCH_ROOM_SCENARIO_MAX_BRANCHES) {
        branch_count = ORCH_ROOM_SCENARIO_MAX_BRANCHES;
    }
    for (uint8_t branch_index = 0; branch_index < branch_count; ++branch_index) {
        const orch_room_scenario_branch_entry_t *branch = &out->room.scenario_branches[branch_index];
        uint16_t total = branch->total_steps;
        if (total > ORCH_ROOM_SCENARIO_MAX_STEPS) {
            total = ORCH_ROOM_SCENARIO_MAX_STEPS;
        }
        out->scenario_branch_steps[branch_index].step_count = (uint8_t)total;
        for (uint16_t local_index = 0; local_index < total; ++local_index) {
            uint16_t global_index = (uint16_t)(branch->step_start_index + local_index);
            out->scenario_branch_steps[branch_index].steps[local_index].index = local_index;
            out->scenario_branch_steps[branch_index].steps[local_index].global_index = global_index;
            out->scenario_branch_steps[branch_index].steps[local_index].state =
                orch_room_runtime_branch_step_state(branch, local_index);
            quest_str_copy(out->scenario_branch_steps[branch_index].steps[local_index].state_text,
                           sizeof(out->scenario_branch_steps[branch_index].steps[local_index].state_text),
                           orch_room_scenario_step_state_str(
                               out->scenario_branch_steps[branch_index].steps[local_index].state));
            out->scenario_branch_steps[branch_index].steps[local_index].enabled = true;
            out->scenario_branch_steps[branch_index].steps[local_index].text[0] = '\0';
            if (session &&
                session->running_scenario_valid &&
                global_index < session->running_scenario.step_count) {
                const room_scenario_step_t *step = &session->running_scenario.steps[global_index];
                out->scenario_branch_steps[branch_index].steps[local_index].enabled = step->enabled;
                orch_room_runtime_format_step_text(
                    step,
                    out->scenario_branch_steps[branch_index].steps[local_index].text,
                    sizeof(out->scenario_branch_steps[branch_index].steps[local_index].text));
            }
        }
    }
}

static esp_err_t orch_room_runtime_view_synthesize_idle(const char *room_id, orch_room_runtime_view_t *out)
{
    room_catalog_entry_t room = {0};

    if (!room_id || !room_id[0] || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (room_catalog_init() != ESP_OK || room_catalog_find(room_id, &room) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    memset(out, 0, sizeof(*out));
    quest_str_copy(out->room.room_id, sizeof(out->room.room_id), room.room_id);
    quest_str_copy(out->room.title,
                   sizeof(out->room.title),
                   room.name[0] ? room.name : room.room_id);
    quest_str_copy(out->room.session_state, sizeof(out->room.session_state), "idle");
    quest_str_copy(out->room.timer_state, sizeof(out->room.timer_state), "idle");
    out->room.scenario_runtime_state = ORCH_ROOM_SCENARIO_RUNTIME_IDLE;
    out->room.scenario_wait_type = ORCH_ROOM_SCENARIO_WAIT_NONE;
    quest_str_copy(out->room.health_text,
                   sizeof(out->room.health_text),
                   orch_health_str(out->room.health));
    quest_str_copy(out->room.scenario_runtime_state_text,
                   sizeof(out->room.scenario_runtime_state_text),
                   orch_room_scenario_runtime_state_str(out->room.scenario_runtime_state));
    quest_str_copy(out->room.scenario_wait_type_text,
                   sizeof(out->room.scenario_wait_type_text),
                   orch_room_scenario_wait_type_str(out->room.scenario_wait_type));
    out->runtime_now_ms = orch_now_ms();
    quest_str_copy(out->asset_prepare_state, sizeof(out->asset_prepare_state), "none");
    return ESP_OK;
}

esp_err_t orchestrator_registry_get_room_runtime_view(const char *room_id,
                                                      orch_room_runtime_view_t *out)
{
    esp_err_t err = ESP_OK;
    gm_room_session_t *session = NULL;
    bool have_session = false;
    orch_room_asset_summary_t asset_summary = {0};
    bool have_asset_summary = false;
    bool need_asset_summary = false;
    uint32_t scenario_generation = room_scenario_generation();
    uint32_t device_generation = quest_device_generation();
    uint32_t asset_generation = orch_room_runtime_assets_generation();

    if (!room_id || !room_id[0] || !out) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    err = orch_scratch_lock();
    if (err != ESP_OK) {
        return err;
    }
    session = orch_scratch_session();
    if (session) {
        memset(session, 0, sizeof(*session));
        if (gm_room_session_get(room_id, session) == ESP_OK) {
            have_session = true;
        }
    }
    err = orch_room_view_load_runtime_room_with_session(room_id,
                                                        have_session ? session : NULL,
                                                        &out->room);
    if (err == ESP_OK) {
        out->runtime_now_ms = orch_now_ms();
        if (out->room.running_scenario_id[0]) {
            scenario_generation = out->room.running_scenario_generation
                                      ? out->room.running_scenario_generation
                                      : scenario_generation;
            need_asset_summary = true;
        }
        orch_room_runtime_view_fill_branch_steps(out, have_session ? session : NULL);
    }
    orch_scratch_unlock();
    if (err != ESP_OK) {
        return err == ESP_ERR_NOT_FOUND ? orch_room_runtime_view_synthesize_idle(room_id, out) : err;
    }

    if (!need_asset_summary) {
        orch_room_runtime_assets_apply_summary(out, &(orch_room_asset_summary_t){0});
        return ESP_OK;
    }

    if (orch_room_runtime_assets_load_cached(out,
                                             scenario_generation,
                                             device_generation,
                                             asset_generation)) {
        return ESP_OK;
    }

    err = orch_scratch_lock();
    if (err == ESP_OK) {
        session = orch_scratch_session();
        have_session = false;
        if (session) {
            memset(session, 0, sizeof(*session));
            if (gm_room_session_get(room_id, session) == ESP_OK) {
                have_session = true;
            }
        }
        have_asset_summary = orch_room_runtime_assets_collect(out->room.running_scenario_id,
                                                              have_session ? session : NULL,
                                                              orch_scratch_room_scenario(),
                                                              &asset_summary);
        orch_scratch_unlock();
    }

    if (have_asset_summary) {
        orch_room_runtime_assets_apply_summary(out, &asset_summary);
        orch_room_runtime_assets_store_cached(out,
                                              scenario_generation,
                                              device_generation,
                                              asset_generation,
                                              &asset_summary);
    } else {
        orch_room_runtime_assets_apply_summary(out, &(orch_room_asset_summary_t){0});
    }
    if (!out->asset_prepare_state[0]) {
        quest_str_copy(out->asset_prepare_state, sizeof(out->asset_prepare_state), "none");
    }

    return ESP_OK;
}
