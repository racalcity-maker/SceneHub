#include "gm_room_session_commands_internal.h"

#include <string.h>

#include "command_executor.h"
#include "quest_common_utils.h"
#include "quest_device.h"
#include "gm_room_session_projection_internal.h"
#include "gm_room_session_reactive_internal.h"
#include "gm_room_session_runner_internal.h"
#include "gm_room_session_wait_internal.h"

#define GM_ROOM_SESSION_DISPATCH_PENDING_ID "__dispatch_pending__"
#define GM_ROOM_SESSION_DISPATCH_BUDGET 8

static void dispatch_from_executor(gm_room_session_command_dispatch_t *out,
                                   const command_executor_dispatch_t *in)
{
    if (!out || !in) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->result_required = in->result_required;
    out->timeout_ms = in->timeout_ms;
    quest_str_copy(out->request_id, sizeof(out->request_id), in->request_id);
    quest_str_copy(out->source_id, sizeof(out->source_id), in->source_id);
    quest_str_copy(out->command, sizeof(out->command), in->command);
}

static bool scenario_waiting_for_command_dispatch(const gm_room_session_t *session)
{
    return session &&
           session->scenario_state == GM_ROOM_SCENARIO_WAITING &&
           session->wait_type == GM_ROOM_SCENARIO_WAIT_DEVICE_COMMAND_RESULT &&
           strcmp(session->wait_event_type, GM_ROOM_SESSION_DISPATCH_PENDING_ID) == 0;
}

static void scenario_enter_wait_command_dispatch_locked(gm_room_session_t *session, uint32_t now_ms)
{
    if (!session) {
        return;
    }
    session->scenario_state = GM_ROOM_SCENARIO_WAITING;
    gm_room_session_scenario_clear_wait_locked(session);
    session->wait_type = GM_ROOM_SCENARIO_WAIT_DEVICE_COMMAND_RESULT;
    session->wait_started_at_ms = now_ms;
    session->wait_until_ms = 0;
    quest_str_copy(session->wait_event_type,
                   sizeof(session->wait_event_type),
                   GM_ROOM_SESSION_DISPATCH_PENDING_ID);
}

static esp_err_t command_group_validate_immediate_only(const room_scenario_device_command_t *command,
                                                       char *error,
                                                       size_t error_size)
{
    quest_device_command_t command_meta = {0};
    esp_err_t err = ESP_OK;

    if (!command || !command->device_id[0] || !command->command_id[0]) {
        if (error && error_size > 0) {
            quest_str_copy(error, error_size, "device_command_invalid");
        }
        return ESP_ERR_INVALID_ARG;
    }
    err = quest_device_get_command(command->device_id,
                                   command->command_id,
                                   &command_meta);
    if (err != ESP_OK) {
        if (error && error_size > 0) {
            quest_str_copy(error, error_size, "device_command_group_command_not_found");
        }
        return err;
    }
    if (command_meta.result_required) {
        if (error && error_size > 0) {
            quest_str_copy(error, error_size, "device_command_group_result_required_unsupported");
        }
        return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

static void command_group_entry_to_device_command(
    room_scenario_device_command_t *out,
    const room_scenario_device_command_group_t *group,
    uint8_t index)
{
    if (!out || !group || index >= group->command_count ||
        index >= ROOM_SCENARIO_COMMAND_GROUP_MAX_COMMANDS) {
        return;
    }
    memset(out, 0, sizeof(*out));
    quest_str_copy(out->device_id,
                   sizeof(out->device_id),
                   group->commands[index].device_id);
    quest_str_copy(out->command_id,
                   sizeof(out->command_id),
                   group->commands[index].command_id);
    quest_str_copy(out->params_json,
                   sizeof(out->params_json),
                   group->commands[index].params_json);
}

bool gm_room_session_command_plan_present(const gm_room_session_command_plan_t *plan)
{
    return plan && plan->kind != GM_ROOM_SESSION_COMMAND_PLAN_NONE;
}

void gm_room_session_command_plan_clear(gm_room_session_command_plan_t *plan)
{
    if (!plan) {
        return;
    }
    memset(plan, 0, sizeof(*plan));
}

esp_err_t gm_room_session_execute_quest_device_command_internal(
    const room_scenario_device_command_t *step_command,
    char *error,
    size_t error_size,
    gm_room_session_command_dispatch_t *out_dispatch)
{
    command_executor_request_t request = {0};
    command_executor_dispatch_t dispatch = {0};
    esp_err_t err = ESP_OK;
    if (!step_command || !step_command->device_id[0] || !step_command->command_id[0]) {
        if (error && error_size > 0) {
            quest_str_copy(error, error_size, "device_command_invalid");
        }
        return ESP_ERR_INVALID_ARG;
    }
    quest_str_copy(request.source, sizeof(request.source), "scenario");
    quest_str_copy(request.device_id, sizeof(request.device_id), step_command->device_id);
    quest_str_copy(request.command_id, sizeof(request.command_id), step_command->command_id);
    quest_str_copy(request.params_json, sizeof(request.params_json), step_command->params_json);
    request.require_scenario_allowed = true;
    err = command_executor_execute(&request, &dispatch, error, error_size);
    if (err == ESP_OK && out_dispatch) {
        dispatch_from_executor(out_dispatch, &dispatch);
    }
    return err;
}

esp_err_t gm_room_session_plan_scenario_command_locked(
    gm_room_session_t *session,
    uint8_t branch_index,
    const room_scenario_device_command_t *command,
    uint32_t now_ms,
    gm_room_session_command_plan_t *out_plan)
{
    if (!session || !command || !out_plan) {
        return ESP_ERR_INVALID_ARG;
    }
    gm_room_session_command_plan_clear(out_plan);
    quest_str_copy(out_plan->room_id, sizeof(out_plan->room_id), session->room_id);
    out_plan->branch_index = branch_index;
    out_plan->kind = GM_ROOM_SESSION_COMMAND_PLAN_SCENARIO_STEP;
    out_plan->expected_step_index = session->current_step_index;
    scenario_enter_wait_command_dispatch_locked(session, now_ms);
    gm_room_session_mark_session_changed_locked(session);
    return ESP_OK;
}

esp_err_t gm_room_session_plan_scenario_command_group_locked(
    gm_room_session_t *session,
    uint8_t branch_index,
    const room_scenario_device_command_group_t *group,
    uint32_t now_ms,
    char *error,
    size_t error_size,
    gm_room_session_command_plan_t *out_plan)
{
    if (!session || !group || !out_plan ||
        group->command_count == 0 ||
        group->command_count > ROOM_SCENARIO_COMMAND_GROUP_MAX_COMMANDS) {
        if (error && error_size > 0) {
            quest_str_copy(error, error_size, "device_command_group_invalid");
        }
        return ESP_ERR_INVALID_ARG;
    }
    for (uint8_t i = 0; i < group->command_count; ++i) {
        room_scenario_device_command_t command = {0};
        command_group_entry_to_device_command(&command, group, i);
        esp_err_t err = command_group_validate_immediate_only(&command, error, error_size);
        if (err != ESP_OK) {
            return err;
        }
    }
    gm_room_session_command_plan_clear(out_plan);
    quest_str_copy(out_plan->room_id, sizeof(out_plan->room_id), session->room_id);
    out_plan->branch_index = branch_index;
    out_plan->kind = GM_ROOM_SESSION_COMMAND_PLAN_SCENARIO_STEP_GROUP;
    out_plan->expected_step_index = session->current_step_index;
    out_plan->command_index = 0;
    scenario_enter_wait_command_dispatch_locked(session, now_ms);
    gm_room_session_mark_session_changed_locked(session);
    return ESP_OK;
}

esp_err_t gm_room_session_plan_reactive_action_command_locked(
    gm_room_session_t *session,
    gm_room_scenario_branch_runtime_t *branch,
    const room_scenario_reactive_action_t *action,
    uint32_t now_ms,
    char *error,
    size_t error_size,
    gm_room_session_command_plan_t *out_plan)
{
    if (!session || !branch || !action || !out_plan) {
        if (error && error_size > 0) {
            quest_str_copy(error, error_size, "reactive_action_invalid");
        }
        return ESP_ERR_INVALID_ARG;
    }
    gm_room_session_command_plan_clear(out_plan);
    quest_str_copy(out_plan->room_id, sizeof(out_plan->room_id), session->room_id);
    out_plan->branch_index = branch->branch_index;
    out_plan->expected_reactive_action = branch->reactive_current_action;
    out_plan->command_index = 0;

    if (action->type == ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP) {
        if (action->group_mode == ROOM_SCENARIO_COMMAND_GROUP_PARALLEL) {
            if (error && error_size > 0) {
                quest_str_copy(error, error_size, "reactive_group_unsupported");
            }
            return ESP_ERR_NOT_SUPPORTED;
        }
        if (action->group_command_count == 0 ||
            action->group_command_count > ROOM_SCENARIO_MAX_REACTIVE_GROUP_COMMANDS ||
            (size_t)action->group_command_start_index + action->group_command_count >
                session->running_scenario.reactive_group_command_count) {
            if (error && error_size > 0) {
                quest_str_copy(error, error_size, "reactive_device_command_group_failed");
            }
            return ESP_ERR_INVALID_ARG;
        }
        for (uint8_t i = 0; i < action->group_command_count; ++i) {
            const room_scenario_device_command_t *command =
                &session->running_scenario.reactive_group_commands[action->group_command_start_index + i];
            esp_err_t err = command_group_validate_immediate_only(command, error, error_size);
            if (err != ESP_OK) {
                return err;
            }
        }
        out_plan->kind = GM_ROOM_SESSION_COMMAND_PLAN_REACTIVE_ACTION_GROUP;
    } else if (action->type == ROOM_SCENARIO_STEP_DEVICE_COMMAND) {
        out_plan->kind = GM_ROOM_SESSION_COMMAND_PLAN_REACTIVE_ACTION;
    } else {
        if (error && error_size > 0) {
            quest_str_copy(error, error_size, "reactive_action_unsupported");
        }
        return ESP_ERR_INVALID_ARG;
    }

    scenario_enter_wait_command_dispatch_locked(session, now_ms);
    gm_room_session_mark_session_changed_locked(session);
    return ESP_OK;
}

static esp_err_t planned_command_resolve_locked(
    gm_room_session_t *session,
    gm_room_scenario_branch_runtime_t *branch,
    const gm_room_session_command_plan_t *plan,
    room_scenario_device_command_t *out_command,
    bool *out_has_more,
    char *error,
    size_t error_size)
{
    const room_scenario_step_t *step = NULL;
    const room_scenario_reactive_action_t *action = NULL;

    if (out_command) {
        memset(out_command, 0, sizeof(*out_command));
    }
    if (out_has_more) {
        *out_has_more = false;
    }
    if (!session || !branch || !plan || !out_command) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (plan->kind) {
    case GM_ROOM_SESSION_COMMAND_PLAN_SCENARIO_STEP:
        if (plan->expected_step_index >= session->running_scenario.step_count) {
            return ESP_ERR_INVALID_STATE;
        }
        step = &session->running_scenario.steps[plan->expected_step_index];
        if (step->type != ROOM_SCENARIO_STEP_DEVICE_COMMAND) {
            return ESP_ERR_INVALID_STATE;
        }
        *out_command = step->data.device_command;
        return ESP_OK;
    case GM_ROOM_SESSION_COMMAND_PLAN_SCENARIO_STEP_GROUP:
        if (plan->expected_step_index >= session->running_scenario.step_count) {
            return ESP_ERR_INVALID_STATE;
        }
        step = &session->running_scenario.steps[plan->expected_step_index];
        if (step->type != ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP ||
            plan->command_index >= step->data.device_command_group.command_count ||
            plan->command_index >= ROOM_SCENARIO_COMMAND_GROUP_MAX_COMMANDS) {
            return ESP_ERR_INVALID_STATE;
        }
        command_group_entry_to_device_command(out_command,
                                              &step->data.device_command_group,
                                              plan->command_index);
        if (out_has_more) {
            *out_has_more = (uint8_t)(plan->command_index + 1) < step->data.device_command_group.command_count;
        }
        return command_group_validate_immediate_only(out_command, error, error_size);
    case GM_ROOM_SESSION_COMMAND_PLAN_REACTIVE_ACTION:
    case GM_ROOM_SESSION_COMMAND_PLAN_REACTIVE_ACTION_GROUP: {
        uint16_t action_index = branch->reactive_action_start_index + branch->reactive_current_action;
        if (action_index >= session->running_scenario.reactive_action_count) {
            return ESP_ERR_INVALID_STATE;
        }
        action = &session->running_scenario.reactive_actions[action_index];
        if (plan->kind == GM_ROOM_SESSION_COMMAND_PLAN_REACTIVE_ACTION) {
            if (action->type != ROOM_SCENARIO_STEP_DEVICE_COMMAND) {
                return ESP_ERR_INVALID_STATE;
            }
            *out_command = action->data.device_command;
            return ESP_OK;
        }
        if (action->type != ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP ||
            action->group_mode == ROOM_SCENARIO_COMMAND_GROUP_PARALLEL ||
            plan->command_index >= action->group_command_count ||
            (size_t)action->group_command_start_index + action->group_command_count >
                session->running_scenario.reactive_group_command_count) {
            return ESP_ERR_INVALID_STATE;
        }
        *out_command =
            session->running_scenario.reactive_group_commands[action->group_command_start_index +
                                                              plan->command_index];
        if (out_has_more) {
            *out_has_more = (uint8_t)(plan->command_index + 1) < action->group_command_count;
        }
        return command_group_validate_immediate_only(out_command, error, error_size);
    }
    case GM_ROOM_SESSION_COMMAND_PLAN_NONE:
    default:
        return ESP_ERR_INVALID_STATE;
    }
}

static bool planned_command_session_matches_locked(
    gm_room_session_t *session,
    gm_room_scenario_branch_runtime_t *branch,
    const gm_room_session_command_plan_t *plan)
{
    if (!session || !branch || !plan || !branch->active) {
        return false;
    }
    gm_room_session_scenario_branch_load_into_session(session, branch);
    if (!scenario_waiting_for_command_dispatch(session)) {
        gm_room_session_scenario_branch_save_from_session(branch, session);
        return false;
    }
    if (plan->kind == GM_ROOM_SESSION_COMMAND_PLAN_SCENARIO_STEP ||
        plan->kind == GM_ROOM_SESSION_COMMAND_PLAN_SCENARIO_STEP_GROUP) {
        bool ok = session->current_step_index == plan->expected_step_index;
        gm_room_session_scenario_branch_save_from_session(branch, session);
        return ok;
    }
    {
        bool ok = branch->reactive_current_action == plan->expected_reactive_action;
        gm_room_session_scenario_branch_save_from_session(branch, session);
        return ok;
    }
}

esp_err_t gm_room_session_dispatch_planned_command(gm_room_session_command_plan_t *plan)
{
    esp_err_t result = ESP_OK;

    if (!gm_room_session_command_plan_present(plan)) {
        return ESP_OK;
    }

    while (gm_room_session_command_plan_present(plan)) {
        room_scenario_device_command_t command = {0};
        gm_room_session_command_dispatch_t dispatch = {0};
        char error[96] = {0};
        bool has_more = false;
        uint32_t now_ms = gm_room_session_scenario_now_ms();
        esp_err_t err = ESP_OK;

        if (gm_room_session_sessions_lock() != ESP_OK) {
            return ESP_ERR_TIMEOUT;
        }
        gm_room_session_t *session = find_session_mutable_locked(plan->room_id);
        gm_room_scenario_branch_runtime_t *branch = NULL;
        if (!session ||
            !session->running_scenario_valid ||
            plan->branch_index >= session->branch_runtime_count ||
            plan->branch_index >= ROOM_SCENARIO_MAX_BRANCHES) {
            gm_room_session_sessions_unlock();
            gm_room_session_command_plan_clear(plan);
            return ESP_OK;
        }
        branch = &session->branch_runtimes[plan->branch_index];
        if (!planned_command_session_matches_locked(session, branch, plan)) {
            gm_room_session_sessions_unlock();
            gm_room_session_command_plan_clear(plan);
            return ESP_OK;
        }
        gm_room_session_scenario_branch_load_into_session(session, branch);
        err = planned_command_resolve_locked(session,
                                             branch,
                                             plan,
                                             &command,
                                             &has_more,
                                             error,
                                             sizeof(error));
        gm_room_session_scenario_branch_save_from_session(branch, session);
        gm_room_session_sessions_unlock();
        if (err != ESP_OK) {
            if (gm_room_session_sessions_lock() == ESP_OK) {
                session = find_session_mutable_locked(plan->room_id);
                if (session &&
                    session->running_scenario_valid &&
                    plan->branch_index < session->branch_runtime_count &&
                    plan->branch_index < ROOM_SCENARIO_MAX_BRANCHES) {
                    branch = &session->branch_runtimes[plan->branch_index];
                    gm_room_session_scenario_branch_load_into_session(session, branch);
                    scenario_set_error_locked(session,
                                              error[0] ? error : "device_command_failed");
                    gm_room_session_scenario_branch_save_from_session(branch, session);
                    gm_room_session_scenario_update_summary_from_branches_locked(session);
                }
                gm_room_session_sessions_unlock();
            }
            gm_room_session_command_plan_clear(plan);
            return err;
        }

        err = gm_room_session_execute_quest_device_command_internal(&command,
                                                                    error,
                                                                    sizeof(error),
                                                                    &dispatch);
        now_ms = gm_room_session_scenario_now_ms();

        if (gm_room_session_sessions_lock() != ESP_OK) {
            return ESP_ERR_TIMEOUT;
        }
        session = find_session_mutable_locked(plan->room_id);
        if (!session ||
            !session->running_scenario_valid ||
            plan->branch_index >= session->branch_runtime_count ||
            plan->branch_index >= ROOM_SCENARIO_MAX_BRANCHES) {
            gm_room_session_sessions_unlock();
            gm_room_session_command_plan_clear(plan);
            return ESP_OK;
        }
        branch = &session->branch_runtimes[plan->branch_index];
        if (!planned_command_session_matches_locked(session, branch, plan)) {
            gm_room_session_sessions_unlock();
            gm_room_session_command_plan_clear(plan);
            return ESP_OK;
        }
        gm_room_session_scenario_branch_load_into_session(session, branch);

        if (err != ESP_OK) {
            scenario_set_error_locked(session,
                                      error[0] ? error
                                               : ((plan->kind == GM_ROOM_SESSION_COMMAND_PLAN_REACTIVE_ACTION ||
                                                   plan->kind == GM_ROOM_SESSION_COMMAND_PLAN_REACTIVE_ACTION_GROUP)
                                                      ? "reactive_device_command_failed"
                                                      : "device_command_failed"));
            gm_room_session_scenario_branch_save_from_session(branch, session);
            gm_room_session_scenario_update_summary_from_branches_locked(session);
            gm_room_session_sessions_unlock();
            gm_room_session_command_plan_clear(plan);
            return err;
        }

        if ((plan->kind == GM_ROOM_SESSION_COMMAND_PLAN_SCENARIO_STEP_GROUP ||
             plan->kind == GM_ROOM_SESSION_COMMAND_PLAN_REACTIVE_ACTION_GROUP) &&
            dispatch.result_required) {
            scenario_set_error_locked(session,
                                      plan->kind == GM_ROOM_SESSION_COMMAND_PLAN_SCENARIO_STEP_GROUP
                                          ? "device_command_group_result_required_unsupported"
                                          : "reactive_group_result_required_unsupported");
            gm_room_session_scenario_branch_save_from_session(branch, session);
            gm_room_session_scenario_update_summary_from_branches_locked(session);
            gm_room_session_sessions_unlock();
            gm_room_session_command_plan_clear(plan);
            return ESP_ERR_NOT_SUPPORTED;
        }

        if (dispatch.result_required) {
            scenario_enter_wait_command_result_locked(session, &dispatch, now_ms);
            gm_room_session_scenario_branch_save_from_session(branch, session);
            gm_room_session_scenario_update_summary_from_branches_locked(session);
            gm_room_session_sessions_unlock();
            gm_room_session_command_plan_clear(plan);
            return ESP_OK;
        }

        if (has_more) {
            plan->command_index++;
            gm_room_session_scenario_branch_save_from_session(branch, session);
            gm_room_session_sessions_unlock();
            continue;
        }

        if (plan->kind == GM_ROOM_SESSION_COMMAND_PLAN_SCENARIO_STEP ||
            plan->kind == GM_ROOM_SESSION_COMMAND_PLAN_SCENARIO_STEP_GROUP) {
            gm_room_session_command_plan_t next_plan = {0};
            session->scenario_state = GM_ROOM_SCENARIO_RUNNING;
            session->current_step_index++;
            gm_room_session_scenario_clear_wait_locked(session);
            gm_room_session_mark_session_changed_locked(session);
            gm_room_session_scenario_branch_save_from_session(branch, session);
            result = gm_room_session_execute_branch_locked(session,
                                                           branch,
                                                           now_ms,
                                                           GM_ROOM_SESSION_DISPATCH_BUDGET,
                                                           &next_plan);
            gm_room_session_sessions_unlock();
            *plan = next_plan;
            if (result != ESP_OK) {
                gm_room_session_command_plan_clear(plan);
                return result;
            }
            continue;
        }

        session->scenario_state = GM_ROOM_SCENARIO_RUNNING;
        gm_room_session_scenario_clear_wait_locked(session);
        branch->reactive_current_action++;
        branch->scenario_state = GM_ROOM_SCENARIO_RUNNING;
        scenario_branch_clear_wait_fields(branch);
        gm_room_session_mark_session_changed_locked(session);
        gm_room_session_scenario_branch_save_from_session(branch, session);
        {
            gm_room_session_command_plan_t next_plan = {0};
            result = gm_room_session_reactive_v2_continue_locked(session,
                                                                 branch,
                                                                 now_ms,
                                                                 &next_plan);
            gm_room_session_scenario_update_summary_from_branches_locked(session);
            gm_room_session_sessions_unlock();
            *plan = next_plan;
            if (result != ESP_OK) {
                gm_room_session_command_plan_clear(plan);
                return result;
            }
        }
    }

    return result;
}

esp_err_t gm_room_session_execute_device_command(const char *device_id,
                                                 const char *command_id,
                                                 const char *params_json)
{
    command_executor_request_t request = {0};
    if (!device_id || !device_id[0] || !command_id || !command_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(device_id) >= sizeof(request.device_id) ||
        strlen(command_id) >= sizeof(request.command_id)) {
        return ESP_ERR_INVALID_SIZE;
    }
    quest_str_copy(request.source, sizeof(request.source), "session");
    quest_str_copy(request.device_id, sizeof(request.device_id), device_id);
    quest_str_copy(request.command_id, sizeof(request.command_id), command_id);
    if (params_json && params_json[0]) {
        if (strlen(params_json) >= sizeof(request.params_json)) {
            return ESP_ERR_INVALID_SIZE;
        }
        quest_str_copy(request.params_json, sizeof(request.params_json), params_json);
    }
    return command_executor_execute(&request, NULL, NULL, 0);
}

void gm_room_session_stop_audio(void)
{
    (void)command_executor_execute_device_command(QUEST_DEVICE_SYSTEM_AUDIO_ID,
                                                  "stop",
                                                  "{\"channel\":\"all\"}");
}
