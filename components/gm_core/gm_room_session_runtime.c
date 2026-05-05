#include "gm_room_session_internal.h"

#include <string.h>

#include "command_executor.h"
#include "esp_heap_caps.h"
#include "quest_common_utils.h"
#include "quest_device.h"
#include "room_scenario.h"

#define GM_SCENARIO_MAX_STEPS_PER_TICK 8

static room_scenario_t *scenario_alloc(void)
{
    room_scenario_t *scenario = heap_caps_calloc(1, sizeof(*scenario), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!scenario) {
        scenario = heap_caps_calloc(1, sizeof(*scenario), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return scenario;
}

static esp_err_t load_selected_scenario_for_room(const char *room_id, room_scenario_t **out_scenario)
{
    char selected_id[ROOM_SCENARIO_ID_MAX_LEN] = {0};
    gm_room_session_t *session = NULL;
    room_scenario_t *scenario = NULL;
    esp_err_t err = ESP_OK;
    if (!room_id || !room_id[0] || !out_scenario) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_scenario = NULL;
    session = gm_room_session_heap_alloc(sizeof(*session));
    if (!session) {
        return ESP_ERR_NO_MEM;
    }
    err = gm_room_session_get(room_id, session);
    if (err != ESP_OK) {
        heap_caps_free(session);
        return err;
    }
    if (!session->selected_scenario_id[0]) {
        heap_caps_free(session);
        return ESP_ERR_INVALID_STATE;
    }
    quest_str_copy(selected_id, sizeof(selected_id), session->selected_scenario_id);
    heap_caps_free(session);
    scenario = scenario_alloc();
    if (!scenario) {
        return ESP_ERR_NO_MEM;
    }
    err = room_scenario_get(selected_id, scenario);
    if (err != ESP_OK) {
        heap_caps_free(scenario);
        return err;
    }
    if (strcmp(scenario->room_id, room_id) != 0) {
        heap_caps_free(scenario);
        return ESP_ERR_INVALID_STATE;
    }
    *out_scenario = scenario;
    return ESP_OK;
}

static esp_err_t execute_scenario_locked(gm_room_session_t *session,
                                         const room_scenario_t *scenario,
                                         uint32_t now_ms,
                                         uint8_t budget,
                                         uint16_t end_step_index)
{
    if (!session || !scenario) {
        return ESP_ERR_INVALID_ARG;
    }
    if (end_step_index == 0 || end_step_index > scenario->step_count) {
        end_step_index = (uint16_t)scenario->step_count;
    }
    while (budget > 0) {
        if (session->current_step_index >= end_step_index) {
            session->scenario_state = GM_ROOM_SCENARIO_DONE;
            gm_room_session_scenario_clear_wait_locked(session);
            gm_room_session_mark_session_changed_locked(session);
            return ESP_OK;
        }

        const room_scenario_step_t *step = &scenario->steps[session->current_step_index];
        if (!step->enabled) {
            session->current_step_index++;
            gm_room_session_mark_session_changed_locked(session);
            budget--;
            continue;
        }

        switch (step->type) {
        case ROOM_SCENARIO_STEP_DEVICE_COMMAND: {
            char command_error[96] = {0};
            gm_room_session_command_dispatch_t dispatch = {0};
            esp_err_t err = gm_room_session_execute_quest_device_command_internal(&step->data.device_command,
                                                         command_error,
                                                         sizeof(command_error),
                                                         &dispatch);
            if (err != ESP_OK) {
                scenario_set_error_locked(session,
                                          command_error[0] ? command_error : "device_command_failed");
                return err;
            }
            if (dispatch.result_required) {
                scenario_enter_wait_command_result_locked(session, &dispatch, now_ms);
                return ESP_OK;
            }
            session->scenario_state = GM_ROOM_SCENARIO_RUNNING;
            session->current_step_index++;
            gm_room_session_scenario_clear_wait_locked(session);
            gm_room_session_mark_session_changed_locked(session);
            budget--;
            break;
        }
        case ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP:
            for (uint8_t i = 0; i < step->data.device_command_group.command_count; ++i) {
                char command_error[96] = {0};
                room_scenario_device_command_t command = {0};
                quest_device_command_t command_meta = {0};
                quest_str_copy(command.device_id,
                            sizeof(command.device_id),
                            step->data.device_command_group.commands[i].device_id);
                quest_str_copy(command.command_id,
                            sizeof(command.command_id),
                            step->data.device_command_group.commands[i].command_id);
                quest_str_copy(command.params_json,
                            sizeof(command.params_json),
                            step->data.device_command_group.commands[i].params_json);
                esp_err_t meta_err = quest_device_get_command(command.device_id,
                                                              command.command_id,
                                                              &command_meta);
                if (meta_err != ESP_OK) {
                    scenario_set_error_locked(session, "device_command_group_command_not_found");
                    return meta_err;
                }
                if (command_meta.result_required) {
                    scenario_set_error_locked(session, "device_command_group_result_required_unsupported");
                    return ESP_ERR_NOT_SUPPORTED;
                }
                esp_err_t err = gm_room_session_execute_quest_device_command_internal(&command,
                                                             command_error,
                                                             sizeof(command_error),
                                                             NULL);
                if (err != ESP_OK) {
                    scenario_set_error_locked(session,
                                              command_error[0] ? command_error
                                                               : "device_command_group_failed");
                    return err;
                }
            }
            session->scenario_state = GM_ROOM_SCENARIO_RUNNING;
            session->current_step_index++;
            gm_room_session_scenario_clear_wait_locked(session);
            gm_room_session_mark_session_changed_locked(session);
            budget--;
            break;
        case ROOM_SCENARIO_STEP_WAIT_TIME:
            session->scenario_state = GM_ROOM_SCENARIO_WAITING;
            gm_room_session_scenario_clear_wait_locked(session);
            session->wait_type = GM_ROOM_SCENARIO_WAIT_TIME;
            session->wait_started_at_ms = now_ms;
            session->wait_until_ms = now_ms + step->data.wait_time.duration_ms;
            scenario_set_wait_skip_from_step_locked(session, step);
            gm_room_session_mark_session_changed_locked(session);
            return ESP_OK;
        case ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT: {
            esp_err_t err = scenario_enter_wait_device_event_locked(session,
                                                                    &step->data.wait_device_event,
                                                                    now_ms);
            if (err != ESP_OK) {
                scenario_set_error_locked(session, "wait_device_event_failed");
                return err;
            }
            scenario_set_wait_skip_from_step_locked(session, step);
            return ESP_OK;
        }
        case ROOM_SCENARIO_STEP_WAIT_ANY_DEVICE_EVENT: {
            esp_err_t err = scenario_enter_wait_any_device_event_locked(session,
                                                                        &step->data.wait_any_device_event,
                                                                        now_ms);
            if (err != ESP_OK) {
                scenario_set_error_locked(session, "wait_any_device_event_failed");
                return err;
            }
            scenario_set_wait_skip_from_step_locked(session, step);
            return ESP_OK;
        }
        case ROOM_SCENARIO_STEP_WAIT_ALL_DEVICE_EVENTS: {
            esp_err_t err = scenario_enter_wait_all_device_events_locked(session,
                                                                         &step->data.wait_all_device_events,
                                                                         now_ms);
            if (err != ESP_OK) {
                scenario_set_error_locked(session, "wait_all_device_events_failed");
                return err;
            }
            scenario_set_wait_skip_from_step_locked(session, step);
            return ESP_OK;
        }
        case ROOM_SCENARIO_STEP_WAIT_FLAGS: {
            esp_err_t err = scenario_enter_wait_flags_locked(session,
                                                             &step->data.wait_flags,
                                                             now_ms);
            if (err != ESP_OK) {
                scenario_set_error_locked(session, "wait_flags_failed");
                return err;
            }
            scenario_set_wait_skip_from_step_locked(session, step);
            if (scenario_wait_flags_met_locked(session)) {
                session->current_step_index++;
                session->scenario_state = GM_ROOM_SCENARIO_RUNNING;
                gm_room_session_scenario_clear_wait_locked(session);
                gm_room_session_mark_session_changed_locked(session);
                budget--;
                break;
            }
            return ESP_OK;
        }
        case ROOM_SCENARIO_STEP_OPERATOR_APPROVAL:
            session->scenario_state = GM_ROOM_SCENARIO_WAITING;
            gm_room_session_scenario_clear_wait_locked(session);
            session->wait_type = GM_ROOM_SCENARIO_WAIT_OPERATOR;
            session->wait_started_at_ms = now_ms;
            quest_str_copy(session->wait_operator_prompt,
                        sizeof(session->wait_operator_prompt),
                        step->data.operator_approval.prompt);
            quest_str_copy(session->wait_operator_label,
                        sizeof(session->wait_operator_label),
                        step->data.operator_approval.approve_label[0]
                            ? step->data.operator_approval.approve_label
                            : "Continue");
            gm_room_session_mark_session_changed_locked(session);
            return ESP_OK;
        case ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE:
            quest_str_copy(session->scenario_operator_message,
                        sizeof(session->scenario_operator_message),
                        step->data.operator_message.message);
            session->scenario_state = GM_ROOM_SCENARIO_RUNNING;
            session->current_step_index++;
            gm_room_session_scenario_clear_wait_locked(session);
            gm_room_session_mark_session_changed_locked(session);
            budget--;
            break;
        case ROOM_SCENARIO_STEP_SET_FLAG: {
            esp_err_t err = scenario_set_flag_locked(session,
                                                     step->data.set_flag.name,
                                                     step->data.set_flag.value);
            if (err != ESP_OK) {
                scenario_set_error_locked(session, "set_flag_failed");
                return err;
            }
            session->scenario_state = GM_ROOM_SCENARIO_RUNNING;
            session->current_step_index++;
            gm_room_session_scenario_clear_wait_locked(session);
            gm_room_session_mark_session_changed_locked(session);
            budget--;
            break;
        }
        case ROOM_SCENARIO_STEP_END_GAME: {
            esp_err_t err = finish_game_without_audio_locked(session, now_ms);
            if (err != ESP_OK) {
                scenario_set_error_locked(session, "end_game_failed");
                return err;
            }
            session->current_step_index++;
            gm_room_session_mark_session_changed_locked(session);
            return ESP_OK;
        }
        default:
            scenario_set_error_locked(session, "unsupported_step_type");
            return ESP_ERR_NOT_SUPPORTED;
        }
    }
    return ESP_OK;
}

esp_err_t gm_room_session_execute_branch_locked(gm_room_session_t *session,
                                                gm_room_scenario_branch_runtime_t *branch,
                                                uint32_t now_ms,
                                                uint8_t budget)
{
    esp_err_t err = ESP_OK;
    uint16_t end_index = 0;
    if (!session || !branch || !branch->active) {
        return ESP_ERR_INVALID_ARG;
    }
    if (gm_room_session_branch_is_reactive_v2(session, branch)) {
        return gm_room_session_reactive_v2_continue_locked(session, branch, now_ms);
    }
    end_index = scenario_branch_end_index(branch, &session->running_scenario);
    gm_room_session_scenario_branch_load_into_session(session, branch);
    err = execute_scenario_locked(session,
                                  &session->running_scenario,
                                  now_ms,
                                  budget,
                                  end_index);
    gm_room_session_scenario_branch_save_from_session(branch, session);
    if (err == ESP_OK &&
        branch->type == ROOM_SCENARIO_BRANCH_REACTIVE &&
        branch->scenario_state == GM_ROOM_SCENARIO_DONE) {
        branch->fired_once = true;
        branch->fire_count++;
        if (branch->max_fire_count == 0 || branch->fire_count < branch->max_fire_count) {
            branch->current_step_index = branch->step_start_index;
            branch->cooldown_until_ms = branch->cooldown_ms > 0 ? now_ms + branch->cooldown_ms : 0;
            branch->scenario_state = branch->cooldown_ms > 0
                                         ? GM_ROOM_SCENARIO_COOLDOWN
                                         : GM_ROOM_SCENARIO_RUNNING;
            scenario_branch_clear_wait_fields(branch);
            gm_room_session_mark_session_changed_locked(session);
            if (branch->scenario_state == GM_ROOM_SCENARIO_RUNNING) {
                gm_room_session_scenario_branch_load_into_session(session, branch);
                err = execute_scenario_locked(session,
                                              &session->running_scenario,
                                              now_ms,
                                              budget,
                                              end_index);
                gm_room_session_scenario_branch_save_from_session(branch, session);
            }
        }
    }
    gm_room_session_scenario_update_summary_from_branches_locked(session);
    return err;
}

static esp_err_t execute_all_running_branches_locked(gm_room_session_t *session,
                                                    uint32_t now_ms,
                                                    uint8_t budget_per_branch)
{
    esp_err_t first_err = ESP_OK;
    if (!session || session->branch_runtime_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    for (uint8_t i = 0; i < session->branch_runtime_count && i < ROOM_SCENARIO_MAX_BRANCHES; ++i) {
        gm_room_scenario_branch_runtime_t *branch = &session->branch_runtimes[i];
        esp_err_t err = ESP_OK;
        if (!branch->active || branch->scenario_state != GM_ROOM_SCENARIO_RUNNING) {
            continue;
        }
        if (gm_room_session_branch_is_reactive_v2(session, branch)) {
            continue;
        }
        err = gm_room_session_execute_branch_locked(session, branch, now_ms, budget_per_branch);
        if (err != ESP_OK && first_err == ESP_OK) {
            first_err = err;
        }
    }
    gm_room_session_scenario_update_summary_from_branches_locked(session);
    return first_err;
}

static gm_room_scenario_branch_runtime_t *scenario_first_branch_by_state_locked(
    gm_room_session_t *session,
    gm_room_scenario_state_t state,
    gm_room_scenario_wait_type_t wait_type,
    bool match_wait_type)
{
    if (!session) {
        return NULL;
    }
    for (uint8_t i = 0; i < session->branch_runtime_count && i < ROOM_SCENARIO_MAX_BRANCHES; ++i) {
        gm_room_scenario_branch_runtime_t *branch = &session->branch_runtimes[i];
        if (!branch->active || branch->scenario_state != state) {
            continue;
        }
        if (match_wait_type && branch->wait_type != wait_type) {
            continue;
        }
        return branch;
    }
    return NULL;
}

static gm_room_scenario_branch_runtime_t *scenario_branch_by_id_locked(gm_room_session_t *session,
                                                                       const char *branch_id)
{
    if (!session || !branch_id || !branch_id[0]) {
        return NULL;
    }
    for (uint8_t i = 0; i < session->branch_runtime_count && i < ROOM_SCENARIO_MAX_BRANCHES; ++i) {
        gm_room_scenario_branch_runtime_t *runtime = &session->branch_runtimes[i];
        const room_scenario_branch_t *branch =
            (session->running_scenario.branch_count > i) ? &session->running_scenario.branches[i] : NULL;
        const char *id = branch && branch->id[0] ? branch->id : (i == 0 ? "main" : "");
        if (runtime->active && id[0] && strcmp(id, branch_id) == 0) {
            return runtime;
        }
    }
    return NULL;
}

esp_err_t gm_room_session_scenario_start(const char *room_id)
{
    room_scenario_t *scenario = NULL;
    uint32_t scenario_generation = 0;
    room_scenario_validation_report_t *report = NULL;
    gm_room_session_t *session = NULL;
    esp_err_t err = load_selected_scenario_for_room(room_id, &scenario);
    if (err != ESP_OK) {
        return err;
    }
    report = gm_room_session_heap_alloc(sizeof(*report));
    if (!report) {
        heap_caps_free(scenario);
        return ESP_ERR_NO_MEM;
    }
    err = room_scenario_validate(scenario, report);
    if (err != ESP_OK || !report->valid) {
        esp_err_t lock_err = gm_room_session_sessions_lock();
        if (lock_err == ESP_OK) {
            session = find_session_mutable_locked(room_id);
            if (session) {
                scenario_clear_running_snapshot_locked(session);
                scenario_set_error_locked(session, scenario_validation_error_message(report));
            }
            gm_room_session_sessions_unlock();
        }
        heap_caps_free(report);
        heap_caps_free(scenario);
        return err != ESP_OK ? err : ESP_ERR_INVALID_ARG;
    }
    heap_caps_free(report);
    scenario_generation = room_scenario_generation();
    err = gm_room_session_sessions_lock();
    if (err != ESP_OK) {
        heap_caps_free(scenario);
        return err;
    }
    session = find_session_mutable_locked(room_id);
    if (!session) {
        gm_room_session_sessions_unlock();
        heap_caps_free(scenario);
        return ESP_ERR_NOT_FOUND;
    }
    session->running_scenario = *scenario;
    session->running_scenario_valid = true;
    session->running_scenario_generation = scenario_generation;
    session->scenario_state = GM_ROOM_SCENARIO_RUNNING;
    session->current_step_index = 0;
    gm_room_session_scenario_clear_wait_locked(session);
    scenario_clear_branch_runtimes_locked(session);
    session->scenario_operator_message[0] = '\0';
    scenario_clear_flags_locked(session);
    session->scenario_last_error[0] = '\0';
    err = scenario_init_branch_runtimes_locked(session);
    if (err != ESP_OK) {
        gm_room_session_sessions_unlock();
        heap_caps_free(scenario);
        return err;
    }
    gm_room_session_mark_session_changed_locked(session);
    err = execute_all_running_branches_locked(session,
                                              gm_room_session_scenario_now_ms(),
                                              GM_SCENARIO_MAX_STEPS_PER_TICK);
    gm_room_session_sessions_unlock();
    heap_caps_free(scenario);
    return err;
}

esp_err_t gm_room_session_scenario_stop(const char *room_id)
{
    gm_room_session_t *session = NULL;
    esp_err_t err = gm_room_session_sessions_lock();
    if (err != ESP_OK) {
        return err;
    }
    session = find_session_mutable_locked(room_id);
    if (!session) {
        gm_room_session_sessions_unlock();
        return ESP_ERR_NOT_FOUND;
    }
    session->scenario_state = GM_ROOM_SCENARIO_STOPPED;
    gm_room_session_scenario_clear_wait_locked(session);
    scenario_clear_running_snapshot_locked(session);
    session->scenario_operator_message[0] = '\0';
    scenario_clear_flags_locked(session);
    gm_room_session_mark_session_changed_locked(session);
    gm_room_session_sessions_unlock();
    return ESP_OK;
}

esp_err_t gm_room_session_scenario_next(const char *room_id)
{
    gm_room_session_t *session = NULL;
    gm_room_scenario_branch_runtime_t *branch = NULL;
    esp_err_t err = gm_room_session_sessions_lock();
    if (err != ESP_OK) {
        return err;
    }
    session = find_session_mutable_locked(room_id);
    if (!session) {
        gm_room_session_sessions_unlock();
        return ESP_ERR_NOT_FOUND;
    }
    if (!session->running_scenario_valid) {
        gm_room_session_sessions_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    branch = scenario_first_branch_by_state_locked(session,
                                                   GM_ROOM_SCENARIO_WAITING,
                                                   GM_ROOM_SCENARIO_WAIT_NONE,
                                                   false);
    if (!branch) {
        branch = scenario_first_branch_by_state_locked(session,
                                                       GM_ROOM_SCENARIO_RUNNING,
                                                       GM_ROOM_SCENARIO_WAIT_NONE,
                                                       false);
    }
    if (!branch) {
        gm_room_session_sessions_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    gm_room_session_scenario_branch_load_into_session(session, branch);
    if (session->scenario_state == GM_ROOM_SCENARIO_WAITING) {
        session->current_step_index++;
        session->scenario_state = GM_ROOM_SCENARIO_RUNNING;
        gm_room_session_scenario_clear_wait_locked(session);
    }
    gm_room_session_scenario_branch_save_from_session(branch, session);
    gm_room_session_mark_session_changed_locked(session);
    err = gm_room_session_execute_branch_locked(session,
                                branch,
                                gm_room_session_scenario_now_ms(),
                                GM_SCENARIO_MAX_STEPS_PER_TICK);
    gm_room_session_sessions_unlock();
    return err;
}

esp_err_t gm_room_session_scenario_next_branch(const char *room_id, const char *branch_id)
{
    gm_room_session_t *session = NULL;
    gm_room_scenario_branch_runtime_t *branch = NULL;
    esp_err_t err = ESP_OK;
    if (!branch_id || !branch_id[0]) {
        return gm_room_session_scenario_next(room_id);
    }
    err = gm_room_session_sessions_lock();
    if (err != ESP_OK) {
        return err;
    }
    session = find_session_mutable_locked(room_id);
    if (!session) {
        gm_room_session_sessions_unlock();
        return ESP_ERR_NOT_FOUND;
    }
    if (!session->running_scenario_valid) {
        gm_room_session_sessions_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    branch = scenario_branch_by_id_locked(session, branch_id);
    if (!branch ||
        (branch->scenario_state != GM_ROOM_SCENARIO_WAITING &&
         branch->scenario_state != GM_ROOM_SCENARIO_RUNNING)) {
        gm_room_session_sessions_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    gm_room_session_scenario_branch_load_into_session(session, branch);
    if (session->scenario_state == GM_ROOM_SCENARIO_WAITING) {
        session->current_step_index++;
        session->scenario_state = GM_ROOM_SCENARIO_RUNNING;
        gm_room_session_scenario_clear_wait_locked(session);
    }
    gm_room_session_scenario_branch_save_from_session(branch, session);
    gm_room_session_mark_session_changed_locked(session);
    err = gm_room_session_execute_branch_locked(session,
                                branch,
                                gm_room_session_scenario_now_ms(),
                                GM_SCENARIO_MAX_STEPS_PER_TICK);
    gm_room_session_sessions_unlock();
    return err;
}

esp_err_t gm_room_session_scenario_approve(const char *room_id)
{
    gm_room_session_t *session = NULL;
    gm_room_scenario_branch_runtime_t *branch = NULL;
    esp_err_t err = gm_room_session_sessions_lock();
    if (err != ESP_OK) {
        return err;
    }
    session = find_session_mutable_locked(room_id);
    if (!session) {
        gm_room_session_sessions_unlock();
        return ESP_ERR_NOT_FOUND;
    }
    if (!session->running_scenario_valid) {
        gm_room_session_sessions_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    branch = scenario_first_branch_by_state_locked(session,
                                                   GM_ROOM_SCENARIO_WAITING,
                                                   GM_ROOM_SCENARIO_WAIT_OPERATOR,
                                                   true);
    if (!branch) {
        gm_room_session_sessions_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    gm_room_session_scenario_branch_load_into_session(session, branch);
    session->current_step_index++;
    session->scenario_state = GM_ROOM_SCENARIO_RUNNING;
    gm_room_session_scenario_clear_wait_locked(session);
    gm_room_session_scenario_branch_save_from_session(branch, session);
    gm_room_session_mark_session_changed_locked(session);
    err = gm_room_session_execute_branch_locked(session,
                                branch,
                                gm_room_session_scenario_now_ms(),
                                GM_SCENARIO_MAX_STEPS_PER_TICK);
    gm_room_session_sessions_unlock();
    return err;
}

esp_err_t gm_room_session_scenario_reset(const char *room_id)
{
    gm_room_session_t *session = NULL;
    esp_err_t err = gm_room_session_sessions_lock();
    if (err != ESP_OK) {
        return err;
    }
    session = find_session_mutable_locked(room_id);
    if (!session) {
        gm_room_session_sessions_unlock();
        return ESP_ERR_NOT_FOUND;
    }
    session->scenario_state = GM_ROOM_SCENARIO_IDLE;
    session->current_step_index = 0;
    gm_room_session_scenario_clear_wait_locked(session);
    scenario_clear_running_snapshot_locked(session);
    session->scenario_operator_message[0] = '\0';
    scenario_clear_flags_locked(session);
    session->scenario_last_error[0] = '\0';
    gm_room_session_mark_session_changed_locked(session);
    gm_room_session_sessions_unlock();
    return ESP_OK;
}

void gm_room_session_scenario_tick(void)
{
    event_bus_message_t timeout_events[4] = {0};
    size_t timeout_count = command_executor_poll_timeouts(timeout_events,
                                                         sizeof(timeout_events) / sizeof(timeout_events[0]));
    uint32_t now_ms = gm_room_session_scenario_now_ms();
    for (size_t i = 0; i < timeout_count; ++i) {
        (void)event_bus_post_priority(&timeout_events[i], EVENT_BUS_PRIORITY_HIGH, 0);
        (void)gm_room_session_scenario_on_event(&timeout_events[i]);
    }
    if (gm_room_session_sessions_lock() != ESP_OK) {
        return;
    }
    for (size_t i = 0; i < GM_SESSION_MAX_ROOMS; ++i) {
        gm_room_session_t *session = &g_gm_room_sessions[i];
        if (!session->in_use || !session->running_scenario_valid) {
            continue;
        }
        if (session->branch_runtime_count > 0) {
            for (uint8_t branch_index = 0;
                 branch_index < session->branch_runtime_count &&
                 branch_index < ROOM_SCENARIO_MAX_BRANCHES;
                 ++branch_index) {
                gm_room_scenario_branch_runtime_t *branch = &session->branch_runtimes[branch_index];
                if (!branch->active) {
                    continue;
                }
                if (branch->scenario_state == GM_ROOM_SCENARIO_COOLDOWN) {
                    if (!scenario_time_reached(now_ms, branch->cooldown_until_ms)) {
                        continue;
                    }
                    branch->current_step_index = branch->step_start_index;
                    branch->scenario_state = gm_room_session_branch_is_reactive_v2(session, branch)
                                                 ? GM_ROOM_SCENARIO_WAITING
                                                 : GM_ROOM_SCENARIO_RUNNING;
                    branch->cooldown_until_ms = 0;
                    scenario_branch_clear_wait_fields(branch);
                    gm_room_session_mark_session_changed_locked(session);
                    if (gm_room_session_branch_is_reactive_v2(session, branch)) {
                        continue;
                    }
                }
                gm_room_session_scenario_branch_load_into_session(session, branch);
                if (session->scenario_state == GM_ROOM_SCENARIO_WAITING &&
                    session->wait_type == GM_ROOM_SCENARIO_WAIT_TIME) {
                    if (!scenario_time_reached(now_ms, session->wait_until_ms)) {
                        gm_room_session_scenario_branch_save_from_session(branch, session);
                        continue;
                    }
                    if (gm_room_session_branch_is_reactive_v2(session, branch)) {
                        branch->reactive_current_action++;
                        branch->scenario_state = GM_ROOM_SCENARIO_RUNNING;
                        scenario_branch_clear_wait_fields(branch);
                        (void)gm_room_session_reactive_v2_continue_locked(session, branch, now_ms);
                        continue;
                    }
                    session->current_step_index++;
                    session->scenario_state = GM_ROOM_SCENARIO_RUNNING;
                    gm_room_session_scenario_clear_wait_locked(session);
                    gm_room_session_mark_session_changed_locked(session);
                } else if (session->scenario_state == GM_ROOM_SCENARIO_WAITING &&
                           (session->wait_type == GM_ROOM_SCENARIO_WAIT_DEVICE_EVENT ||
                            session->wait_type == GM_ROOM_SCENARIO_WAIT_ANY_DEVICE_EVENT)) {
                    if (!scenario_apply_wait_timeout_locked(session,
                                                            &session->running_scenario,
                                                            now_ms)) {
                        gm_room_session_scenario_branch_save_from_session(branch, session);
                        continue;
                    }
                } else if (session->scenario_state == GM_ROOM_SCENARIO_WAITING &&
                           session->wait_type == GM_ROOM_SCENARIO_WAIT_FLAGS) {
                    if (!scenario_wait_flags_met_locked(session)) {
                        if (scenario_apply_wait_timeout_locked(session,
                                                               &session->running_scenario,
                                                               now_ms)) {
                            gm_room_session_scenario_branch_save_from_session(branch, session);
                            (void)gm_room_session_execute_branch_locked(session,
                                                        branch,
                                                        now_ms,
                                                        GM_SCENARIO_MAX_STEPS_PER_TICK);
                            continue;
                        }
                        gm_room_session_scenario_branch_save_from_session(branch, session);
                        continue;
                    }
                    session->current_step_index++;
                    session->scenario_state = GM_ROOM_SCENARIO_RUNNING;
                    gm_room_session_scenario_clear_wait_locked(session);
                    gm_room_session_mark_session_changed_locked(session);
                } else if (session->scenario_state != GM_ROOM_SCENARIO_RUNNING) {
                    gm_room_session_scenario_branch_save_from_session(branch, session);
                    continue;
                }
                gm_room_session_scenario_branch_save_from_session(branch, session);
                (void)gm_room_session_execute_branch_locked(session,
                                            branch,
                                            now_ms,
                                            GM_SCENARIO_MAX_STEPS_PER_TICK);
            }
            gm_room_session_scenario_update_summary_from_branches_locked(session);
            continue;
        }
    }
    gm_room_session_sessions_unlock();
}
