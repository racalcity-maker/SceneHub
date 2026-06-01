#include "gm_room_session_commands_internal.h"
#include "gm_room_session_projection_internal.h"
#include "gm_room_session_reactive_internal.h"
#include "gm_room_session_runner_internal.h"
#include "gm_room_session_wait_internal.h"

#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "quest_common_utils.h"
#include "room_scenario.h"
#include "scenehub_scenario_validation.h"

#define GM_SCENARIO_MAX_STEPS_PER_TICK 8
#define GM_RUNTIME_STACK_WARN_BYTES 2048
#define GM_RUNTIME_STACK_WARN_INTERVAL_TICKS pdMS_TO_TICKS(10000)

static const char *TAG = "gm_room_runtime";

static SemaphoreHandle_t s_tick_mutex = NULL;
static StaticSemaphore_t s_tick_mutex_storage;
static portMUX_TYPE s_tick_mutex_init_lock = portMUX_INITIALIZER_UNLOCKED;
static esp_timer_handle_t s_runtime_deadline_timer = NULL;
static EXT_RAM_BSS_ATTR scenehub_event_t s_timeout_events[4];
static EXT_RAM_BSS_ATTR room_scenario_validation_report_t s_start_report;
static SemaphoreHandle_t s_start_scratch_mutex = NULL;
static StaticSemaphore_t s_start_scratch_mutex_storage;
static portMUX_TYPE s_start_scratch_mutex_init_lock = portMUX_INITIALIZER_UNLOCKED;

static esp_err_t gm_room_session_ensure_tick_mutex(void)
{
    if (s_tick_mutex) {
        return ESP_OK;
    }
    portENTER_CRITICAL(&s_tick_mutex_init_lock);
    if (!s_tick_mutex) {
        s_tick_mutex = xSemaphoreCreateMutexStatic(&s_tick_mutex_storage);
    }
    portEXIT_CRITICAL(&s_tick_mutex_init_lock);
    return s_tick_mutex ? ESP_OK : ESP_ERR_NO_MEM;
}

static void gm_room_session_runtime_check_stack(void)
{
    static TickType_t s_last_warn_tick = 0;
    UBaseType_t high_water = uxTaskGetStackHighWaterMark(NULL);
    TickType_t now = xTaskGetTickCount();
    if (high_water >= GM_RUNTIME_STACK_WARN_BYTES) {
        return;
    }
    if (s_last_warn_tick != 0 &&
        (now - s_last_warn_tick) < GM_RUNTIME_STACK_WARN_INTERVAL_TICKS) {
        return;
    }
    s_last_warn_tick = now;
    ESP_LOGW(TAG, "low stack headroom: high_water=%u", (unsigned)high_water);
}

static void gm_room_session_runtime_deadline_timer_cb(void *arg)
{
    (void)arg;
    gm_room_session_runtime_wake();
}

static esp_err_t gm_room_session_ensure_deadline_timer(void)
{
    if (s_runtime_deadline_timer) {
        return ESP_OK;
    }

    const esp_timer_create_args_t timer_args = {
        .callback = gm_room_session_runtime_deadline_timer_cb,
        .name = "gm_runtime_deadline",
    };
    esp_err_t err = esp_timer_create(&timer_args, &s_runtime_deadline_timer);
    if (err == ESP_OK) {
        ESP_LOGD(TAG, "created runtime deadline timer handle=%p", (void *)s_runtime_deadline_timer);
    } else {
        ESP_LOGE(TAG, "failed to create runtime deadline timer: %s", esp_err_to_name(err));
    }
    return err;
}

static void gm_room_session_runtime_update_deadline_timer(void)
{
    uint64_t now_ms = gm_room_session_scenario_now_ms();
    uint64_t next_deadline_ms = gm_room_session_next_command_timeout_deadline_ms();

    if (gm_room_session_sessions_lock() == ESP_OK) {
        for (size_t i = 0; i < GM_SESSION_MAX_ROOMS; ++i) {
            const gm_room_session_t *session = &g_gm_room_sessions[i];
            if (!session->in_use || !session->running_scenario_valid) {
                continue;
            }
            if (session->wait_until_ms > 0 &&
                (next_deadline_ms == 0 || session->wait_until_ms < next_deadline_ms)) {
                next_deadline_ms = session->wait_until_ms;
            }
            for (uint8_t branch_index = 0;
                 branch_index < session->branch_runtime_count &&
                 branch_index < ROOM_SCENARIO_MAX_BRANCHES;
                 ++branch_index) {
                const gm_room_scenario_branch_runtime_t *branch = &session->branch_runtimes[branch_index];
                if (!branch->active) {
                    continue;
                }
                if (branch->wait_until_ms > 0 &&
                    (next_deadline_ms == 0 || branch->wait_until_ms < next_deadline_ms)) {
                    next_deadline_ms = branch->wait_until_ms;
                }
                if (branch->cooldown_until_ms > 0 &&
                    (next_deadline_ms == 0 || branch->cooldown_until_ms < next_deadline_ms)) {
                    next_deadline_ms = branch->cooldown_until_ms;
                }
            }
        }
        gm_room_session_sessions_unlock();
    }

    if (gm_room_session_ensure_deadline_timer() != ESP_OK || !s_runtime_deadline_timer) {
        return;
    }

    (void)esp_timer_stop(s_runtime_deadline_timer);
    if (next_deadline_ms == 0) {
        return;
    }

    uint64_t delay_us = 1000;
    if (next_deadline_ms > now_ms) {
        delay_us = (next_deadline_ms - now_ms) * 1000ULL;
        if (delay_us == 0) {
            delay_us = 1000;
        }
    }
    ESP_LOGD(TAG,
             "runtime deadline timer start handle=%p next_deadline_ms=%llu now_ms=%llu delay_us=%llu",
             (void *)s_runtime_deadline_timer,
             (unsigned long long)next_deadline_ms,
             (unsigned long long)now_ms,
             (unsigned long long)delay_us);
    (void)esp_timer_start_once(s_runtime_deadline_timer, delay_us);
}

static esp_err_t gm_room_session_start_scratch_lock(void)
{
    if (!s_start_scratch_mutex) {
        portENTER_CRITICAL(&s_start_scratch_mutex_init_lock);
        if (!s_start_scratch_mutex) {
            s_start_scratch_mutex = xSemaphoreCreateMutexStatic(&s_start_scratch_mutex_storage);
        }
        portEXIT_CRITICAL(&s_start_scratch_mutex_init_lock);
        if (!s_start_scratch_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }
    return (xSemaphoreTake(s_start_scratch_mutex, portMAX_DELAY) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void gm_room_session_start_scratch_unlock(void)
{
    if (s_start_scratch_mutex) {
        xSemaphoreGive(s_start_scratch_mutex);
    }
}

static esp_err_t execute_scenario_locked(gm_room_session_t *session,
                                         const room_scenario_t *scenario,
                                         uint32_t now_ms,
                                         uint8_t budget,
                                         uint16_t end_step_index,
                                         uint8_t branch_index,
                                         const gm_room_session_wait_resolution_t *wait_resolution,
                                         gm_room_session_command_plan_t *out_plan)
{
    if (!session || !scenario || !out_plan) {
        return ESP_ERR_INVALID_ARG;
    }
    gm_room_session_command_plan_clear(out_plan);
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
            esp_err_t err = gm_room_session_plan_scenario_command_locked(session,
                                                                         branch_index,
                                                                         &step->data.device_command,
                                                                         now_ms,
                                                                         out_plan);
            if (err != ESP_OK) {
                scenario_set_error_locked(session,
                                          "device_command_failed");
                return err;
            }
            return ESP_OK;
        }
        case ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP: {
            char command_error[96] = {0};
            esp_err_t err = gm_room_session_plan_scenario_command_group_locked(
                session,
                branch_index,
                &step->data.device_command_group,
                now_ms,
                command_error,
                sizeof(command_error),
                out_plan);
            if (err != ESP_OK) {
                scenario_set_error_locked(session,
                                          command_error[0] ? command_error
                                                           : "device_command_group_failed");
                return err;
            }
            return ESP_OK;
        }
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
                                                                    wait_resolution,
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
                                                                        wait_resolution,
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
                                                                         wait_resolution,
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

static esp_err_t prepare_wait_resolution_locked(gm_room_session_t *session,
                                                gm_room_scenario_branch_runtime_t *branch,
                                                gm_room_session_wait_resolution_t *out)
{
    const gm_room_session_prepared_scenario_t *prepared_scenario = NULL;
    const gm_room_session_prepared_event_resolution_t *prepared_resolution = NULL;

    if (!session || !branch || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (branch->scenario_state != GM_ROOM_SCENARIO_RUNNING ||
        branch->current_step_index >= session->running_scenario.step_count) {
        return ESP_OK;
    }

    const room_scenario_step_t *step = &session->running_scenario.steps[branch->current_step_index];
    switch (step->type) {
    case ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT:
    case ROOM_SCENARIO_STEP_WAIT_ANY_DEVICE_EVENT:
    case ROOM_SCENARIO_STEP_WAIT_ALL_DEVICE_EVENTS:
        break;
    default:
        return ESP_OK;
    }
    prepared_scenario = gm_room_session_get_prepared_scenario_locked(session);
    if (!prepared_scenario) {
        return ESP_ERR_INVALID_STATE;
    }
    prepared_resolution = &prepared_scenario->step_waits[branch->current_step_index];
    return gm_room_session_expand_prepared_wait_resolution(prepared_scenario,
                                                           prepared_resolution,
                                                           out);
}

static esp_err_t execute_branch_core_locked(gm_room_session_t *session,
                                            gm_room_scenario_branch_runtime_t *branch,
                                            uint32_t now_ms,
                                            uint8_t budget,
                                            bool update_summary,
                                            gm_room_session_command_plan_t *out_plan)
{
    esp_err_t err = ESP_OK;
    uint16_t end_index = 0;
    gm_room_session_wait_resolution_t wait_resolution = {0};
    if (!session || !branch || !branch->active || !out_plan) {
        return ESP_ERR_INVALID_ARG;
    }
    gm_room_session_command_plan_clear(out_plan);
    if (gm_room_session_branch_is_reactive_v2(session, branch)) {
        return gm_room_session_reactive_v2_continue_locked(session, branch, now_ms, out_plan);
    }
    err = prepare_wait_resolution_locked(session, branch, &wait_resolution);
    if (err != ESP_OK) {
        return err;
    }
    end_index = scenario_branch_end_index(branch, &session->running_scenario);
    gm_room_session_scenario_branch_load_into_session(session, branch);
    err = execute_scenario_locked(session,
                                  &session->running_scenario,
                                  now_ms,
                                  budget,
                                  end_index,
                                  branch->branch_index,
                                  wait_resolution.present ? &wait_resolution : NULL,
                                  out_plan);
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
                gm_room_session_wait_resolution_t restart_wait_resolution = {0};
                gm_room_session_scenario_branch_load_into_session(session, branch);
                gm_room_session_command_plan_t restart_plan = {0};
                err = prepare_wait_resolution_locked(session,
                                                     branch,
                                                     &restart_wait_resolution);
                if (err != ESP_OK) {
                    return err;
                }
                err = execute_scenario_locked(session,
                                              &session->running_scenario,
                                              now_ms,
                                              budget,
                                              end_index,
                                              branch->branch_index,
                                              restart_wait_resolution.present ? &restart_wait_resolution : NULL,
                                              &restart_plan);
                gm_room_session_scenario_branch_save_from_session(branch, session);
                if (gm_room_session_command_plan_present(&restart_plan)) {
                    *out_plan = restart_plan;
                }
            }
        }
    }
    if (update_summary) {
        gm_room_session_scenario_update_summary_from_branches_locked(session);
    }
    return err;
}

esp_err_t gm_room_session_execute_branch_locked(gm_room_session_t *session,
                                                gm_room_scenario_branch_runtime_t *branch,
                                                uint32_t now_ms,
                                                uint8_t budget,
                                                gm_room_session_command_plan_t *out_plan)
{
    return execute_branch_core_locked(session, branch, now_ms, budget, true, out_plan);
}

static esp_err_t execute_all_running_branches_locked(gm_room_session_t *session,
                                                     uint32_t now_ms,
                                                     uint8_t budget_per_branch,
                                                     gm_room_session_command_plan_t *out_plan)
{
    esp_err_t first_err = ESP_OK;
    if (!session || session->branch_runtime_count == 0 || !out_plan) {
        return ESP_ERR_INVALID_ARG;
    }
    gm_room_session_command_plan_clear(out_plan);
    for (uint8_t i = 0; i < session->branch_runtime_count && i < ROOM_SCENARIO_MAX_BRANCHES; ++i) {
        gm_room_scenario_branch_runtime_t *branch = &session->branch_runtimes[i];
        esp_err_t err = ESP_OK;
        if (!branch->active || branch->scenario_state != GM_ROOM_SCENARIO_RUNNING) {
            continue;
        }
        if (gm_room_session_branch_is_reactive_v2(session, branch)) {
            continue;
        }
        err = execute_branch_core_locked(session, branch, now_ms, budget_per_branch, false, out_plan);
        if (err != ESP_OK && first_err == ESP_OK) {
            first_err = err;
        }
        if (gm_room_session_command_plan_present(out_plan)) {
            break;
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

static esp_err_t scenario_start_prepared_plan_locked(
    const char *room_id,
    const room_scenario_t *scenario,
    const gm_room_session_prepared_scenario_t *prepared_scenario,
    uint32_t scenario_generation,
    gm_room_session_command_plan_t *out_plan);

static esp_err_t scenario_start_prepared_plan_locked(
    const char *room_id,
    const room_scenario_t *scenario,
    const gm_room_session_prepared_scenario_t *prepared_scenario,
    uint32_t scenario_generation,
    gm_room_session_command_plan_t *out_plan)
{
    room_scenario_validation_report_t *report = &s_start_report;
    gm_room_session_t *session = NULL;
    esp_err_t err = ESP_OK;

    if (!out_plan) {
        return ESP_ERR_INVALID_ARG;
    }
    gm_room_session_command_plan_clear(out_plan);
    memset(report, 0, sizeof(*report));
    if (!room_id || !room_id[0] || !scenario || !prepared_scenario) {
        return ESP_ERR_INVALID_ARG;
    }
    err = scenehub_scenario_validate(scenario, report);
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
        return err != ESP_OK ? err : ESP_ERR_INVALID_ARG;
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
    session->running_scenario = *scenario;
    gm_room_session_store_prepared_scenario_locked(session, prepared_scenario);
    session->running_scenario_valid = true;
    session->running_scenario_generation = scenario_generation;
    session->scenario_state = GM_ROOM_SCENARIO_RUNNING;
    session->current_step_index = 0;
    gm_room_session_scenario_clear_wait_locked(session);
    scenario_clear_branch_runtimes_locked(session);
    session->scenario_operator_message[0] = '\0';
    scenario_clear_flags_locked(session);
    session->scenario_last_error[0] = '\0';
    err = scenario_init_branch_runtimes_locked(session, prepared_scenario);
    if (err != ESP_OK) {
        scenario_clear_running_snapshot_locked(session);
        scenario_set_error_locked(session, "scenario_prepared_event_refs_invalid");
        gm_room_session_sessions_unlock();
        return err;
    }
    gm_room_session_mark_session_changed_locked(session);
    err = execute_all_running_branches_locked(session,
                                              gm_room_session_scenario_now_ms(),
                                              GM_SCENARIO_MAX_STEPS_PER_TICK,
                                              out_plan);
    gm_room_session_sessions_unlock();
    if (err == ESP_OK) {
        gm_room_session_runtime_wake();
    }
    return err;
}

esp_err_t gm_room_session_scenario_start_prepared_plan(
    const char *room_id,
    const room_scenario_t *scenario,
    const gm_room_session_prepared_scenario_t *prepared_scenario,
    uint32_t scenario_generation,
    gm_room_session_command_plan_t *out_plan)
{
    esp_err_t err = ESP_OK;

    if (!out_plan) {
        return ESP_ERR_INVALID_ARG;
    }
    gm_room_session_command_plan_clear(out_plan);
    err = gm_room_session_start_scratch_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = scenario_start_prepared_plan_locked(room_id,
                                              scenario,
                                              prepared_scenario,
                                              scenario_generation,
                                              out_plan);
    gm_room_session_start_scratch_unlock();
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
    gm_room_session_runtime_wake();
    return ESP_OK;
}

esp_err_t gm_room_session_scenario_next_plan(const char *room_id,
                                             gm_room_session_command_plan_t *out_plan)
{
    gm_room_session_t *session = NULL;
    gm_room_scenario_branch_runtime_t *branch = NULL;
    esp_err_t err = ESP_OK;

    if (!out_plan) {
        return ESP_ERR_INVALID_ARG;
    }
    gm_room_session_command_plan_clear(out_plan);
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
                                                GM_SCENARIO_MAX_STEPS_PER_TICK,
                                                out_plan);
    gm_room_session_sessions_unlock();
    if (err == ESP_OK) {
        gm_room_session_runtime_wake();
    }
    return err;
}

esp_err_t gm_room_session_scenario_next_branch_plan(const char *room_id,
                                                    const char *branch_id,
                                                    gm_room_session_command_plan_t *out_plan)
{
    gm_room_session_t *session = NULL;
    gm_room_scenario_branch_runtime_t *branch = NULL;
    esp_err_t err = ESP_OK;
    if (!out_plan) {
        return ESP_ERR_INVALID_ARG;
    }
    gm_room_session_command_plan_clear(out_plan);
    if (!branch_id || !branch_id[0]) {
        return gm_room_session_scenario_next_plan(room_id, out_plan);
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
                                                GM_SCENARIO_MAX_STEPS_PER_TICK,
                                                out_plan);
    gm_room_session_sessions_unlock();
    if (err == ESP_OK) {
        gm_room_session_runtime_wake();
    }
    return err;
}

esp_err_t gm_room_session_scenario_approve_plan(const char *room_id,
                                                gm_room_session_command_plan_t *out_plan)
{
    gm_room_session_t *session = NULL;
    gm_room_scenario_branch_runtime_t *branch = NULL;
    esp_err_t err = ESP_OK;

    if (!out_plan) {
        return ESP_ERR_INVALID_ARG;
    }
    gm_room_session_command_plan_clear(out_plan);
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
                                                GM_SCENARIO_MAX_STEPS_PER_TICK,
                                                out_plan);
    gm_room_session_sessions_unlock();
    if (err == ESP_OK) {
        gm_room_session_runtime_wake();
    }
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
    gm_room_session_runtime_wake();
    return ESP_OK;
}

static void gm_room_session_runtime_handle_command_timeouts(void)
{
    size_t timeout_count = 0;

    memset(s_timeout_events, 0, sizeof(s_timeout_events));
    timeout_count = gm_room_session_poll_command_timeouts(
        s_timeout_events,
        sizeof(s_timeout_events) / sizeof(s_timeout_events[0]));
    for (size_t i = 0; i < timeout_count; ++i) {
        (void)event_bus_post_priority(&s_timeout_events[i], EVENT_BUS_PRIORITY_HIGH, 0);
        (void)gm_room_session_scenario_on_event(&s_timeout_events[i]);
    }
}

static bool gm_room_session_runtime_prepare_branch_locked(gm_room_session_t *session,
                                                          gm_room_scenario_branch_runtime_t *branch,
                                                          uint32_t now_ms)
{
    if (!session || !branch || !branch->active) {
        return false;
    }
    if (branch->scenario_state != GM_ROOM_SCENARIO_COOLDOWN) {
        return true;
    }
    if (!scenario_time_reached(now_ms, branch->cooldown_until_ms)) {
        return false;
    }

    branch->current_step_index = branch->step_start_index;
    branch->scenario_state = gm_room_session_branch_is_reactive_v2(session, branch)
                                 ? GM_ROOM_SCENARIO_WAITING
                                 : GM_ROOM_SCENARIO_RUNNING;
    branch->cooldown_until_ms = 0;
    scenario_branch_clear_wait_fields(branch);
    gm_room_session_mark_session_changed_locked(session);
    return !gm_room_session_branch_is_reactive_v2(session, branch);
}

static bool gm_room_session_runtime_process_wait_locked(gm_room_session_t *session,
                                                        gm_room_scenario_branch_runtime_t *branch,
                                                        uint32_t now_ms,
                                                        gm_room_session_command_plan_t *plan,
                                                        bool *out_ready_to_advance)
{
    if (!session || !branch || !plan || !out_ready_to_advance) {
        return false;
    }
    *out_ready_to_advance = false;

    if (session->scenario_state == GM_ROOM_SCENARIO_WAITING &&
        session->wait_type == GM_ROOM_SCENARIO_WAIT_TIME) {
        if (!scenario_time_reached(now_ms, session->wait_until_ms)) {
            gm_room_session_scenario_branch_save_from_session(branch, session);
            return true;
        }
        if (gm_room_session_branch_is_reactive_v2(session, branch)) {
            uint16_t action_index = branch->reactive_action_start_index + branch->reactive_current_action;
            if (action_index >= session->running_scenario.reactive_action_count) {
                scenario_set_error_locked(session, "reactive_action_range_invalid");
                gm_room_session_scenario_branch_save_from_session(branch, session);
                return true;
            }
            (void)gm_room_session_reactive_v2_apply_wait_timeout_locked(
                session,
                branch,
                &session->running_scenario.reactive_actions[action_index],
                now_ms,
                plan);
            gm_room_session_scenario_branch_save_from_session(branch, session);
            return true;
        }
        session->current_step_index++;
        session->scenario_state = GM_ROOM_SCENARIO_RUNNING;
        gm_room_session_scenario_clear_wait_locked(session);
        gm_room_session_mark_session_changed_locked(session);
        *out_ready_to_advance = true;
        return false;
    }

    if (session->scenario_state == GM_ROOM_SCENARIO_WAITING &&
        (session->wait_type == GM_ROOM_SCENARIO_WAIT_DEVICE_EVENT ||
         session->wait_type == GM_ROOM_SCENARIO_WAIT_ANY_DEVICE_EVENT ||
         session->wait_type == GM_ROOM_SCENARIO_WAIT_ALL_DEVICE_EVENTS)) {
        if (!session->wait_until_ms || !scenario_time_reached(now_ms, session->wait_until_ms)) {
            gm_room_session_scenario_branch_save_from_session(branch, session);
            return true;
        }
        if (gm_room_session_branch_is_reactive_v2(session, branch)) {
            uint16_t action_index = branch->reactive_action_start_index + branch->reactive_current_action;
            if (action_index >= session->running_scenario.reactive_action_count) {
                scenario_set_error_locked(session, "reactive_action_range_invalid");
                gm_room_session_scenario_branch_save_from_session(branch, session);
                return true;
            }
            (void)gm_room_session_reactive_v2_apply_wait_timeout_locked(
                session,
                branch,
                &session->running_scenario.reactive_actions[action_index],
                now_ms,
                plan);
            gm_room_session_scenario_branch_save_from_session(branch, session);
            return true;
        }
        if (!scenario_apply_wait_timeout_locked(session,
                                                &session->running_scenario,
                                                now_ms)) {
            gm_room_session_scenario_branch_save_from_session(branch, session);
            return true;
        }
        *out_ready_to_advance = true;
        return false;
    }

    if (session->scenario_state == GM_ROOM_SCENARIO_WAITING &&
        session->wait_type == GM_ROOM_SCENARIO_WAIT_FLAGS) {
        if (!scenario_wait_flags_met_locked(session)) {
            if (gm_room_session_branch_is_reactive_v2(session, branch)) {
                if (!session->wait_until_ms || !scenario_time_reached(now_ms, session->wait_until_ms)) {
                    gm_room_session_scenario_branch_save_from_session(branch, session);
                    return true;
                }
                uint16_t action_index = branch->reactive_action_start_index + branch->reactive_current_action;
                if (action_index >= session->running_scenario.reactive_action_count) {
                    scenario_set_error_locked(session, "reactive_action_range_invalid");
                    gm_room_session_scenario_branch_save_from_session(branch, session);
                    return true;
                }
                (void)gm_room_session_reactive_v2_apply_wait_timeout_locked(
                    session,
                    branch,
                    &session->running_scenario.reactive_actions[action_index],
                    now_ms,
                    plan);
                gm_room_session_scenario_branch_save_from_session(branch, session);
                return true;
            }
            if (scenario_apply_wait_timeout_locked(session,
                                                   &session->running_scenario,
                                                   now_ms)) {
                gm_room_session_scenario_branch_save_from_session(branch, session);
                (void)execute_branch_core_locked(session,
                                                 branch,
                                                 now_ms,
                                                 GM_SCENARIO_MAX_STEPS_PER_TICK,
                                                 false,
                                                 plan);
                return true;
            }
            gm_room_session_scenario_branch_save_from_session(branch, session);
            return true;
        }
        session->current_step_index++;
        session->scenario_state = GM_ROOM_SCENARIO_RUNNING;
        gm_room_session_scenario_clear_wait_locked(session);
        gm_room_session_mark_session_changed_locked(session);
        *out_ready_to_advance = true;
        return false;
    }

    if (session->scenario_state != GM_ROOM_SCENARIO_RUNNING) {
        gm_room_session_scenario_branch_save_from_session(branch, session);
        return true;
    }

    *out_ready_to_advance = true;
    return false;
}

static bool gm_room_session_runtime_advance_ready_branch_locked(
    gm_room_session_t *session,
    gm_room_scenario_branch_runtime_t *branch,
    uint32_t now_ms,
    gm_room_session_command_plan_t *plan)
{
    if (!session || !branch || !plan) {
        return false;
    }

    gm_room_session_scenario_branch_save_from_session(branch, session);
    (void)execute_branch_core_locked(session,
                                     branch,
                                     now_ms,
                                     GM_SCENARIO_MAX_STEPS_PER_TICK,
                                     false,
                                     plan);
    return gm_room_session_command_plan_present(plan);
}

static bool gm_room_session_runtime_process_branch_locked(gm_room_session_t *session,
                                                          gm_room_scenario_branch_runtime_t *branch,
                                                          uint32_t now_ms,
                                                          gm_room_session_command_plan_t *plan)
{
    bool ready_to_advance = false;

    if (!gm_room_session_runtime_prepare_branch_locked(session, branch, now_ms)) {
        return false;
    }

    gm_room_session_scenario_branch_load_into_session(session, branch);
    if (gm_room_session_runtime_process_wait_locked(session,
                                                    branch,
                                                    now_ms,
                                                    plan,
                                                    &ready_to_advance)) {
        return gm_room_session_command_plan_present(plan);
    }
    if (gm_room_session_command_plan_present(plan)) {
        return true;
    }
    if (!ready_to_advance) {
        return false;
    }
    return gm_room_session_runtime_advance_ready_branch_locked(session, branch, now_ms, plan);
}

static bool gm_room_session_runtime_advance_ready_branches_locked(
    gm_room_session_t *session,
    uint32_t now_ms,
    gm_room_session_command_plan_t *plan)
{
    if (!session || !plan || session->branch_runtime_count == 0) {
        return false;
    }

    for (uint8_t branch_index = 0;
         branch_index < session->branch_runtime_count &&
         branch_index < ROOM_SCENARIO_MAX_BRANCHES;
         ++branch_index) {
        gm_room_scenario_branch_runtime_t *branch = &session->branch_runtimes[branch_index];
        if (gm_room_session_runtime_process_branch_locked(session, branch, now_ms, plan)) {
            gm_room_session_scenario_update_summary_from_branches_locked(session);
            return true;
        }
    }

    gm_room_session_scenario_update_summary_from_branches_locked(session);
    return false;
}

esp_err_t gm_room_session_runtime_process_pending_work_plan(gm_room_session_command_plan_t *out_plan)
{
    uint32_t now_ms = gm_room_session_scenario_now_ms();

    if (!out_plan) {
        return ESP_ERR_INVALID_ARG;
    }
    gm_room_session_command_plan_clear(out_plan);
    if (gm_room_session_ensure_tick_mutex() != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(s_tick_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (gm_room_session_sessions_lock() != ESP_OK) {
        xSemaphoreGive(s_tick_mutex);
        return ESP_ERR_TIMEOUT;
    }

    for (size_t i = 0; i < GM_SESSION_MAX_ROOMS; ++i) {
        gm_room_session_t *session = &g_gm_room_sessions[i];
        if (!session->in_use || !session->running_scenario_valid) {
            continue;
        }
        if (gm_room_session_runtime_advance_ready_branches_locked(session, now_ms, out_plan)) {
            goto dispatch_planned_command;
        }
    }

dispatch_planned_command:
    gm_room_session_sessions_unlock();
    xSemaphoreGive(s_tick_mutex);
    return ESP_OK;
}

void gm_room_session_runtime_process_pending_work(void)
{
    gm_room_session_command_plan_t plan = {0};
    esp_err_t dispatch_err = ESP_OK;

    gm_room_session_runtime_handle_command_timeouts();
    if (gm_room_session_runtime_process_pending_work_plan(&plan) != ESP_OK) {
        return;
    }
    if (gm_room_session_command_plan_present(&plan)) {
        dispatch_err = gm_room_session_dispatch_command_plan("runtime_tick", &plan);
        (void)dispatch_err;
    }
}

void gm_room_session_runtime_task(void *ctx)
{
    gm_room_runtime_cause_t cause = {0};
    (void)ctx;

    for (;;) {
        if (xQueueReceive(s_runtime_queue, &cause, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (cause.kind == GM_ROOM_RUNTIME_CAUSE_EVENT) {
            (void)gm_room_session_scenario_on_event(&cause.event);
        }
        gm_room_session_runtime_process_pending_work();
        gm_room_session_runtime_update_deadline_timer();
        gm_room_session_runtime_check_stack();
    }
}
