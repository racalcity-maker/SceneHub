#include "gm_room_session.h"
#include "gm_room_session_internal.h"

#include <string.h>

#include "command_executor.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "quest_common_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "room_scenario.h"

static const char *TAG = "gm_room_session";

EXT_RAM_BSS_ATTR gm_room_session_t g_gm_room_sessions[GM_SESSION_MAX_ROOMS];
static uint32_t s_generation = 0;
static SemaphoreHandle_t s_sessions_mutex = NULL;
static portMUX_TYPE s_sessions_mutex_init_lock = portMUX_INITIALIZER_UNLOCKED;
QueueHandle_t s_event_queue = NULL;
static TaskHandle_t s_event_task = NULL;

void *gm_room_session_heap_alloc(size_t size)
{
    void *ptr = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
        ptr = heap_caps_calloc(1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return ptr;
}

static esp_err_t sessions_ensure_mutex(void)
{
    if (s_sessions_mutex) {
        return ESP_OK;
    }
    portENTER_CRITICAL(&s_sessions_mutex_init_lock);
    if (!s_sessions_mutex) {
        s_sessions_mutex = xSemaphoreCreateMutex();
    }
    portEXIT_CRITICAL(&s_sessions_mutex_init_lock);
    return s_sessions_mutex ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t gm_room_session_ensure_event_worker(void)
{
    if (s_event_queue && s_event_task) {
        return ESP_OK;
    }
    if (!s_event_queue) {
        s_event_queue = xQueueCreateWithCaps(GM_ROOM_SESSION_EVENT_QUEUE_LEN,
                                             sizeof(event_bus_message_t),
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_event_queue) {
            s_event_queue = xQueueCreate(GM_ROOM_SESSION_EVENT_QUEUE_LEN, sizeof(event_bus_message_t));
        }
        if (!s_event_queue) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!s_event_task) {
        BaseType_t ok = xTaskCreate(gm_room_session_event_task,
                                    "gm_room_event",
                                    GM_ROOM_SESSION_EVENT_TASK_STACK,
                                    NULL,
                                    7,
                                    &s_event_task);
        if (ok != pdPASS) {
            s_event_task = NULL;
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

esp_err_t gm_room_session_sessions_lock(void)
{
    esp_err_t err = sessions_ensure_mutex();
    if (err != ESP_OK) {
        return err;
    }
    return (xSemaphoreTake(s_sessions_mutex, portMAX_DELAY) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

void gm_room_session_sessions_unlock(void)
{
    if (s_sessions_mutex) {
        xSemaphoreGive(s_sessions_mutex);
    }
}

void gm_room_session_mark_session_changed_locked(gm_room_session_t *session)
{
    if (!session) {
        return;
    }
    session->generation = ++s_generation;
}

void gm_room_session_scenario_clear_wait_locked(gm_room_session_t *session)
{
    if (!session) {
        return;
    }
    if (session->wait_type == GM_ROOM_SCENARIO_WAIT_DEVICE_COMMAND_RESULT &&
        session->wait_event_type[0]) {
        command_executor_cancel_request(session->wait_event_type);
    }
    session->wait_type = GM_ROOM_SCENARIO_WAIT_NONE;
    session->wait_until_ms = 0;
    session->wait_started_at_ms = 0;
    session->wait_event_type[0] = '\0';
    session->wait_source_id[0] = '\0';
    memset(session->wait_events, 0, sizeof(session->wait_events));
    memset(session->wait_event_matched, 0, sizeof(session->wait_event_matched));
    session->wait_event_count = 0;
    memset(session->wait_flags, 0, sizeof(session->wait_flags));
    session->wait_flag_count = 0;
    session->wait_operator_prompt[0] = '\0';
    session->wait_operator_label[0] = '\0';
    session->wait_operator_skip_allowed = false;
    session->wait_operator_skip_label[0] = '\0';
}

static void scenario_branch_clear_wait(gm_room_scenario_branch_runtime_t *branch)
{
    if (!branch) {
        return;
    }
    if (branch->wait_type == GM_ROOM_SCENARIO_WAIT_DEVICE_COMMAND_RESULT &&
        branch->wait_event_type[0]) {
        command_executor_cancel_request(branch->wait_event_type);
    }
    branch->wait_type = GM_ROOM_SCENARIO_WAIT_NONE;
    branch->wait_until_ms = 0;
    branch->wait_started_at_ms = 0;
    branch->wait_event_type[0] = '\0';
    branch->wait_source_id[0] = '\0';
    memset(branch->wait_events, 0, sizeof(branch->wait_events));
    memset(branch->wait_event_matched, 0, sizeof(branch->wait_event_matched));
    branch->wait_event_count = 0;
    memset(branch->wait_flags, 0, sizeof(branch->wait_flags));
    branch->wait_flag_count = 0;
    branch->wait_operator_prompt[0] = '\0';
    branch->wait_operator_label[0] = '\0';
    branch->wait_operator_skip_allowed = false;
    branch->wait_operator_skip_label[0] = '\0';
}

void scenario_clear_branch_runtimes_locked(gm_room_session_t *session)
{
    if (!session) {
        return;
    }
    for (uint8_t i = 0; i < session->branch_runtime_count &&
                        i < ROOM_SCENARIO_MAX_BRANCHES; ++i) {
        scenario_branch_clear_wait(&session->branch_runtimes[i]);
    }
    memset(session->branch_runtimes, 0, sizeof(session->branch_runtimes));
    session->branch_runtime_count = 0;
}

void scenario_clear_running_snapshot_locked(gm_room_session_t *session)
{
    if (!session) {
        return;
    }
    memset(&session->running_scenario, 0, sizeof(session->running_scenario));
    session->running_scenario_valid = false;
    session->running_scenario_generation = 0;
    scenario_clear_branch_runtimes_locked(session);
}

void scenario_clear_flags_locked(gm_room_session_t *session)
{
    if (!session) {
        return;
    }
    memset(session->scenario_flags, 0, sizeof(session->scenario_flags));
    session->scenario_flag_count = 0;
}

uint16_t scenario_branch_end_index(const gm_room_scenario_branch_runtime_t *branch,
                                          const room_scenario_t *scenario)
{
    uint32_t end_index = 0;
    if (!branch || !scenario) {
        return 0;
    }
    end_index = (uint32_t)branch->step_start_index + branch->step_count;
    if (end_index > scenario->step_count) {
        end_index = scenario->step_count;
    }
    return (uint16_t)end_index;
}

void gm_room_session_scenario_branch_load_into_session(
    gm_room_session_t *session,
    const gm_room_scenario_branch_runtime_t *branch)
{
    if (!session || !branch) {
        return;
    }
    session->scenario_state = branch->scenario_state;
    session->current_step_index = branch->current_step_index;
    session->wait_type = branch->wait_type;
    session->wait_until_ms = branch->wait_until_ms;
    session->wait_started_at_ms = branch->wait_started_at_ms;
    quest_str_copy(session->wait_event_type, sizeof(session->wait_event_type), branch->wait_event_type);
    quest_str_copy(session->wait_source_id, sizeof(session->wait_source_id), branch->wait_source_id);
    memcpy(session->wait_events, branch->wait_events, sizeof(session->wait_events));
    memcpy(session->wait_event_matched, branch->wait_event_matched, sizeof(session->wait_event_matched));
    session->wait_event_count = branch->wait_event_count;
    memcpy(session->wait_flags, branch->wait_flags, sizeof(session->wait_flags));
    session->wait_flag_count = branch->wait_flag_count;
    quest_str_copy(session->wait_operator_prompt,
                sizeof(session->wait_operator_prompt),
                branch->wait_operator_prompt);
    quest_str_copy(session->wait_operator_label,
                sizeof(session->wait_operator_label),
                branch->wait_operator_label);
    session->wait_operator_skip_allowed = branch->wait_operator_skip_allowed;
    quest_str_copy(session->wait_operator_skip_label,
                sizeof(session->wait_operator_skip_label),
                branch->wait_operator_skip_label);
}

void gm_room_session_scenario_branch_save_from_session(
    gm_room_scenario_branch_runtime_t *branch,
    const gm_room_session_t *session)
{
    if (!branch || !session) {
        return;
    }
    branch->scenario_state = session->scenario_state;
    branch->current_step_index = session->current_step_index;
    branch->wait_type = session->wait_type;
    branch->wait_until_ms = session->wait_until_ms;
    branch->wait_started_at_ms = session->wait_started_at_ms;
    quest_str_copy(branch->wait_event_type, sizeof(branch->wait_event_type), session->wait_event_type);
    quest_str_copy(branch->wait_source_id, sizeof(branch->wait_source_id), session->wait_source_id);
    memcpy(branch->wait_events, session->wait_events, sizeof(branch->wait_events));
    memcpy(branch->wait_event_matched, session->wait_event_matched, sizeof(branch->wait_event_matched));
    branch->wait_event_count = session->wait_event_count;
    memcpy(branch->wait_flags, session->wait_flags, sizeof(branch->wait_flags));
    branch->wait_flag_count = session->wait_flag_count;
    quest_str_copy(branch->wait_operator_prompt,
                sizeof(branch->wait_operator_prompt),
                session->wait_operator_prompt);
    quest_str_copy(branch->wait_operator_label,
                sizeof(branch->wait_operator_label),
                session->wait_operator_label);
    branch->wait_operator_skip_allowed = session->wait_operator_skip_allowed;
    quest_str_copy(branch->wait_operator_skip_label,
                sizeof(branch->wait_operator_skip_label),
                session->wait_operator_skip_label);
}

void gm_room_session_scenario_update_summary_from_branches_locked(gm_room_session_t *session)
{
    gm_room_scenario_branch_runtime_t *fallback = NULL;
    gm_room_scenario_branch_runtime_t *selected = NULL;
    gm_room_scenario_branch_runtime_t *reactive_selected = NULL;
    bool any_active = false;
    bool any_required_active = false;
    bool all_required_done = true;
    bool any_running = false;
    if (!session || session->branch_runtime_count == 0) {
        return;
    }
    if (session->scenario_state == GM_ROOM_SCENARIO_ERROR) {
        return;
    }
    for (uint8_t i = 0; i < session->branch_runtime_count && i < ROOM_SCENARIO_MAX_BRANCHES; ++i) {
        gm_room_scenario_branch_runtime_t *branch = &session->branch_runtimes[i];
        if (!branch->active) {
            continue;
        }
        bool is_reactive = branch->type == ROOM_SCENARIO_BRANCH_REACTIVE;
        any_active = true;
        if (!is_reactive &&
            (!fallback || (!fallback->required_for_completion && branch->required_for_completion))) {
            fallback = branch;
        }
        if (branch->scenario_state == GM_ROOM_SCENARIO_ERROR) {
            if (!is_reactive) {
                selected = branch;
                break;
            }
            if (!reactive_selected) {
                reactive_selected = branch;
            }
            continue;
        }
        if (!selected && !is_reactive && branch->scenario_state == GM_ROOM_SCENARIO_WAITING) {
            selected = branch;
        }
        if (!selected && !is_reactive && branch->scenario_state == GM_ROOM_SCENARIO_RUNNING) {
            selected = branch;
        }
        if (!reactive_selected &&
            is_reactive &&
            (branch->scenario_state == GM_ROOM_SCENARIO_WAITING ||
             branch->scenario_state == GM_ROOM_SCENARIO_RUNNING ||
             branch->scenario_state == GM_ROOM_SCENARIO_COOLDOWN)) {
            reactive_selected = branch;
        }
        if (!is_reactive &&
            (branch->scenario_state == GM_ROOM_SCENARIO_RUNNING ||
             branch->scenario_state == GM_ROOM_SCENARIO_WAITING ||
             branch->scenario_state == GM_ROOM_SCENARIO_COOLDOWN)) {
            any_running = true;
        }
        if (!is_reactive && branch->required_for_completion) {
            any_required_active = true;
            if (branch->scenario_state != GM_ROOM_SCENARIO_DONE) {
                all_required_done = false;
            }
        }
    }
    selected = selected ? selected : (fallback ? fallback : reactive_selected);
    if (selected) {
        gm_room_session_scenario_branch_load_into_session(session, selected);
    }
    if (!any_active) {
        session->scenario_state = GM_ROOM_SCENARIO_DONE;
    } else if (any_required_active && all_required_done) {
        session->scenario_state = GM_ROOM_SCENARIO_DONE;
    } else if (!any_running && selected && selected->scenario_state == GM_ROOM_SCENARIO_DONE) {
        session->scenario_state = GM_ROOM_SCENARIO_DONE;
    }
}

esp_err_t scenario_init_branch_runtimes_locked(gm_room_session_t *session)
{
    const room_scenario_t *scenario = NULL;
    if (!session || !session->running_scenario_valid) {
        return ESP_ERR_INVALID_ARG;
    }
    scenario = &session->running_scenario;
    scenario_clear_branch_runtimes_locked(session);
    if (scenario->branch_count > 0) {
        session->branch_runtime_count = (uint8_t)scenario->branch_count;
        for (size_t i = 0; i < scenario->branch_count && i < ROOM_SCENARIO_MAX_BRANCHES; ++i) {
            const room_scenario_branch_t *src = &scenario->branches[i];
            gm_room_scenario_branch_runtime_t *dst = &session->branch_runtimes[i];
            dst->active = src->enabled;
            dst->type = src->type;
            dst->required_for_completion = src->type == ROOM_SCENARIO_BRANCH_NORMAL &&
                                           src->required_for_completion;
            dst->priority = src->priority;
            dst->cooldown_ms = src->cooldown_ms;
            dst->cooldown_until_ms = 0;
            dst->max_fire_count = src->run_once ? 1 : src->max_fire_count;
            dst->fire_count = 0;
            dst->run_once = src->run_once;
            dst->fired_once = false;
            dst->reentry_mode = src->reentry_mode;
            dst->pending_trigger = false;
            dst->policy_cursor = 0;
            dst->policy_stage = 0;
            dst->last_variant_index = UINT8_MAX;
            dst->reactive_action_start_index = 0;
            dst->reactive_action_count = 0;
            dst->reactive_current_action = 0;
            dst->branch_index = (uint16_t)i;
            dst->step_start_index = src->step_start_index;
            dst->step_count = src->step_count;
            dst->current_step_index = src->step_start_index;
            dst->scenario_state = src->enabled
                                      ? (src->type == ROOM_SCENARIO_BRANCH_REACTIVE &&
                                                 (src->variant_count > 0 ||
                                                  src->trigger.kind != ROOM_SCENARIO_REACTIVE_TRIGGER_NONE)
                                             ? GM_ROOM_SCENARIO_WAITING
                                             : GM_ROOM_SCENARIO_RUNNING)
                                      : GM_ROOM_SCENARIO_STOPPED;
            scenario_branch_clear_wait(dst);
        }
        return ESP_OK;
    }
    session->branch_runtime_count = 1;
    session->branch_runtimes[0].active = true;
    session->branch_runtimes[0].type = ROOM_SCENARIO_BRANCH_NORMAL;
    session->branch_runtimes[0].required_for_completion = true;
    session->branch_runtimes[0].branch_index = 0;
    session->branch_runtimes[0].step_start_index = 0;
    session->branch_runtimes[0].step_count = (uint16_t)scenario->step_count;
    session->branch_runtimes[0].current_step_index = 0;
    session->branch_runtimes[0].scenario_state = GM_ROOM_SCENARIO_RUNNING;
    scenario_branch_clear_wait(&session->branch_runtimes[0]);
    return ESP_OK;
}

esp_err_t finish_game_without_audio_locked(gm_room_session_t *session, uint64_t now_ms)
{
    esp_err_t err = ESP_OK;
    if (!session) {
        return ESP_ERR_INVALID_ARG;
    }
    session->state = GM_SESSION_FINISHED;
    session->finished_at_ms = now_ms;
    err = gm_timer_finish(&session->timer, now_ms);
    if (err != ESP_OK) {
        return err;
    }
    session->scenario_state = GM_ROOM_SCENARIO_DONE;
    gm_room_session_scenario_clear_wait_locked(session);
    gm_room_session_mark_session_changed_locked(session);
    return ESP_OK;
}

esp_err_t scenario_set_flag_locked(gm_room_session_t *session,
                                          const char *name,
                                          bool value)
{
    event_bus_message_t message = {0};
    if (!session || !name || !name[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    for (uint8_t i = 0; i < session->scenario_flag_count; ++i) {
        if (strcmp(session->scenario_flags[i].name, name) == 0) {
            if (session->scenario_flags[i].value == value) {
                return ESP_OK;
            }
            session->scenario_flags[i].value = value;
            goto post_changed;
        }
    }
    if (session->scenario_flag_count >= GM_ROOM_SCENARIO_MAX_FLAGS) {
        return ESP_ERR_NO_MEM;
    }
    quest_str_copy(session->scenario_flags[session->scenario_flag_count].name,
                sizeof(session->scenario_flags[session->scenario_flag_count].name),
                name);
    session->scenario_flags[session->scenario_flag_count].value = value;
    session->scenario_flag_count++;
post_changed:
    message.type = EVENT_FLAG_CHANGED;
    message.payload_type = EVENT_BUS_PAYLOAD_DEVICE_CONTROL;
    quest_str_copy(message.topic, sizeof(message.topic), name);
    quest_str_copy(message.payload, sizeof(message.payload), name);
    quest_str_copy(message.data.device_control.device_id,
                   sizeof(message.data.device_control.device_id),
                   session->room_id);
    quest_str_copy(message.data.device_control.action_id,
                   sizeof(message.data.device_control.action_id),
                   name);
    quest_str_copy(message.data.device_control.source,
                   sizeof(message.data.device_control.source),
                   value ? "true" : "false");
    (void)event_bus_post_priority(&message, EVENT_BUS_PRIORITY_HIGH, 0);
    return ESP_OK;
}

static bool scenario_get_flag_locked(const gm_room_session_t *session,
                                     const char *name,
                                     bool *out_value)
{
    if (!session || !name || !name[0]) {
        return false;
    }
    for (uint8_t i = 0; i < session->scenario_flag_count; ++i) {
        if (strcmp(session->scenario_flags[i].name, name) == 0) {
            if (out_value) {
                *out_value = session->scenario_flags[i].value;
            }
            return true;
        }
    }
    return false;
}

bool scenario_wait_flags_met_locked(const gm_room_session_t *session)
{
    if (!session || session->wait_flag_count == 0) {
        return false;
    }
    for (uint8_t i = 0; i < session->wait_flag_count; ++i) {
        bool actual = false;
        if (!scenario_get_flag_locked(session, session->wait_flags[i].name, &actual) ||
            actual != session->wait_flags[i].value) {
            return false;
        }
    }
    return true;
}

static bool scenario_wait_timeout_reached(uint32_t now_ms, uint32_t wait_until_ms)
{
    return wait_until_ms > 0 && scenario_time_reached(now_ms, wait_until_ms);
}

bool scenario_apply_wait_timeout_locked(gm_room_session_t *session,
                                               const room_scenario_t *scenario,
                                               uint32_t now_ms)
{
    const room_scenario_step_t *step = NULL;
    const char *message = NULL;
    if (!session || !scenario ||
        !scenario_wait_timeout_reached(now_ms, session->wait_until_ms) ||
        session->current_step_index >= scenario->step_count) {
        return false;
    }
    step = &scenario->steps[session->current_step_index];
    if (step->type == ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT) {
        message = step->data.wait_device_event.timeout_message;
    } else if (step->type == ROOM_SCENARIO_STEP_WAIT_FLAGS) {
        message = step->data.wait_flags.timeout_message;
    } else {
        return false;
    }
    if (message && message[0]) {
        quest_str_copy(session->scenario_operator_message,
                    sizeof(session->scenario_operator_message),
                    message);
    }
    ESP_LOGW(TAG,
             "scenario wait timeout: room=%s step=%u message=%s",
             session->room_id,
             (unsigned)session->current_step_index,
             message && message[0] ? message : "");
    session->current_step_index++;
    session->scenario_state = GM_ROOM_SCENARIO_RUNNING;
    gm_room_session_scenario_clear_wait_locked(session);
    gm_room_session_mark_session_changed_locked(session);
    return true;
}

void scenario_set_wait_skip_from_step_locked(gm_room_session_t *session,
                                                    const room_scenario_step_t *step)
{
    if (!session || !step) {
        return;
    }
    session->wait_operator_skip_allowed = step->allow_operator_skip;
    if (!step->allow_operator_skip) {
        session->wait_operator_skip_label[0] = '\0';
        return;
    }
    quest_str_copy(session->wait_operator_skip_label,
                sizeof(session->wait_operator_skip_label),
                step->operator_skip_label[0] ? step->operator_skip_label : "Skip wait");
}

uint64_t gm_room_session_now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

uint32_t gm_room_session_scenario_now_ms(void)
{
    return (uint32_t)gm_room_session_now_ms();
}

bool scenario_time_reached(uint32_t now_ms, uint32_t target_ms)
{
    return (int32_t)(now_ms - target_ms) >= 0;
}

void scenario_set_error_locked(gm_room_session_t *session, const char *message)
{
    if (!session) {
        return;
    }
    session->scenario_state = GM_ROOM_SCENARIO_ERROR;
    gm_room_session_scenario_clear_wait_locked(session);
    quest_str_copy(session->scenario_last_error,
                sizeof(session->scenario_last_error),
                message ? message : "scenario_error");
    gm_room_session_mark_session_changed_locked(session);
}

const char *scenario_validation_error_message(const room_scenario_validation_report_t *report)
{
    if (!report) {
        return "scenario_validation_failed";
    }
    for (size_t i = 0; i < report->issue_count; ++i) {
        const room_scenario_validation_issue_t *issue = &report->issues[i];
        if (issue->level == ROOM_SCENARIO_VALIDATION_ERROR) {
            return issue->message[0] ? issue->message : issue->code;
        }
    }
    return "scenario_validation_failed";
}

gm_room_session_t *find_session_mutable_locked(const char *room_id)
{
    if (!room_id || !room_id[0]) {
        return NULL;
    }
    for (size_t i = 0; i < GM_SESSION_MAX_ROOMS; ++i) {
        if (g_gm_room_sessions[i].in_use && strcmp(g_gm_room_sessions[i].room_id, room_id) == 0) {
            return &g_gm_room_sessions[i];
        }
    }
    return NULL;
}

gm_room_session_t *alloc_session_locked(const char *room_id)
{
    gm_room_session_t *session = NULL;
    if (!room_id || !room_id[0]) {
        return NULL;
    }
    session = find_session_mutable_locked(room_id);
    if (session) {
        return session;
    }
    for (size_t i = 0; i < GM_SESSION_MAX_ROOMS; ++i) {
        if (!g_gm_room_sessions[i].in_use) {
            memset(&g_gm_room_sessions[i], 0, sizeof(g_gm_room_sessions[i]));
            g_gm_room_sessions[i].in_use = true;
            quest_str_copy(g_gm_room_sessions[i].room_id, sizeof(g_gm_room_sessions[i].room_id), room_id);
            gm_room_session_mark_session_changed_locked(&g_gm_room_sessions[i]);
            g_gm_room_sessions[i].scenario_state = GM_ROOM_SCENARIO_IDLE;
            gm_timer_reset(&g_gm_room_sessions[i].timer, 0, 0);
            gm_hint_reset(&g_gm_room_sessions[i].hint);
            return &g_gm_room_sessions[i];
        }
    }
    return NULL;
}

esp_err_t gm_room_session_init(void)
{
    esp_err_t err = sessions_ensure_mutex();
    if (err != ESP_OK) {
        return err;
    }
    err = gm_room_session_ensure_event_worker();
    if (err != ESP_OK) {
        return err;
    }
    err = event_bus_register_handler(gm_room_session_event_handler);
    if (err != ESP_OK) {
        return err;
    }
    gm_room_session_reset_all();
    return ESP_OK;
}

void gm_room_session_reset_all(void)
{
    command_executor_reset_pending();
    if (gm_room_session_sessions_lock() == ESP_OK) {
        memset(g_gm_room_sessions, 0, sizeof(g_gm_room_sessions));
        s_generation++;
        gm_room_session_sessions_unlock();
        return;
    }
    memset(g_gm_room_sessions, 0, sizeof(g_gm_room_sessions));
    s_generation++;
}

uint32_t gm_room_session_generation(void)
{
    uint32_t generation = 0;
    if (gm_room_session_sessions_lock() != ESP_OK) {
        return 0;
    }
    generation = s_generation;
    gm_room_session_sessions_unlock();
    return generation;
}

esp_err_t gm_room_session_get(const char *room_id, gm_room_session_t *out_session)
{
    esp_err_t err = ESP_OK;
    gm_room_session_t *session = NULL;
    if (!room_id || !room_id[0] || !out_session) {
        return ESP_ERR_INVALID_ARG;
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
    *out_session = *session;
    gm_room_session_sessions_unlock();
    return ESP_OK;
}

esp_err_t gm_room_session_start(const char *room_id, uint32_t duration_ms, uint64_t now_ms)
{
    esp_err_t err = ESP_OK;
    gm_room_session_t *session = NULL;
    if (!room_id || !room_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    err = gm_room_session_sessions_lock();
    if (err != ESP_OK) {
        return err;
    }
    session = alloc_session_locked(room_id);
    if (!session) {
        gm_room_session_sessions_unlock();
        return ESP_ERR_NO_MEM;
    }
    session->state = GM_SESSION_RUNNING;
    session->started_at_ms = now_ms;
    session->finished_at_ms = 0;
    gm_room_session_mark_session_changed_locked(session);
    gm_hint_reset(&session->hint);
    err = gm_timer_start(&session->timer, duration_ms, now_ms);
    gm_room_session_sessions_unlock();
    return err;
}

esp_err_t gm_room_session_pause(const char *room_id, uint64_t now_ms)
{
    esp_err_t err = ESP_OK;
    gm_room_session_t *session = NULL;
    err = gm_room_session_sessions_lock();
    if (err != ESP_OK) {
        return err;
    }
    session = find_session_mutable_locked(room_id);
    if (!session) {
        gm_room_session_sessions_unlock();
        return ESP_ERR_NOT_FOUND;
    }
    if (session->state != GM_SESSION_RUNNING) {
        gm_room_session_sessions_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    session->state = GM_SESSION_PAUSED;
    err = gm_timer_pause(&session->timer, now_ms);
    if (err == ESP_OK) {
        gm_room_session_mark_session_changed_locked(session);
    }
    gm_room_session_sessions_unlock();
    return err;
}

esp_err_t gm_room_session_resume(const char *room_id, uint64_t now_ms)
{
    esp_err_t err = ESP_OK;
    gm_room_session_t *session = NULL;
    err = gm_room_session_sessions_lock();
    if (err != ESP_OK) {
        return err;
    }
    session = find_session_mutable_locked(room_id);
    if (!session) {
        gm_room_session_sessions_unlock();
        return ESP_ERR_NOT_FOUND;
    }
    if (session->state != GM_SESSION_PAUSED) {
        gm_room_session_sessions_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    session->state = GM_SESSION_RUNNING;
    err = gm_timer_resume(&session->timer, now_ms);
    if (err == ESP_OK) {
        gm_room_session_mark_session_changed_locked(session);
    }
    gm_room_session_sessions_unlock();
    return err;
}

esp_err_t gm_room_session_finish(const char *room_id, uint64_t now_ms)
{
    esp_err_t err = ESP_OK;
    gm_room_session_t *session = NULL;
    err = gm_room_session_sessions_lock();
    if (err != ESP_OK) {
        return err;
    }
    session = find_session_mutable_locked(room_id);
    if (!session) {
        gm_room_session_sessions_unlock();
        return ESP_ERR_NOT_FOUND;
    }
    session->state = GM_SESSION_FINISHED;
    session->finished_at_ms = now_ms;
    err = gm_timer_finish(&session->timer, now_ms);
    if (err == ESP_OK) {
        gm_room_session_mark_session_changed_locked(session);
    }
    gm_room_session_sessions_unlock();
    if (err == ESP_OK) {
        gm_room_session_stop_audio();
    }
    return err;
}

esp_err_t gm_room_session_reset(const char *room_id, uint32_t duration_ms, uint64_t now_ms)
{
    esp_err_t err = ESP_OK;
    gm_room_session_t *session = NULL;
    err = gm_room_session_sessions_lock();
    if (err != ESP_OK) {
        return err;
    }
    session = find_session_mutable_locked(room_id);
    if (!session) {
        gm_room_session_sessions_unlock();
        return ESP_ERR_NOT_FOUND;
    }
    session->state = GM_SESSION_IDLE;
    session->started_at_ms = 0;
    session->finished_at_ms = 0;
    gm_hint_reset(&session->hint);
    gm_timer_reset(&session->timer, duration_ms, now_ms);
    gm_room_session_mark_session_changed_locked(session);
    gm_room_session_sessions_unlock();
    return ESP_OK;
}

esp_err_t gm_room_session_add_time(const char *room_id, int32_t delta_ms, uint64_t now_ms)
{
    esp_err_t err = ESP_OK;
    gm_room_session_t *session = NULL;
    err = gm_room_session_sessions_lock();
    if (err != ESP_OK) {
        return err;
    }
    session = find_session_mutable_locked(room_id);
    if (!session) {
        gm_room_session_sessions_unlock();
        return ESP_ERR_NOT_FOUND;
    }
    err = gm_timer_add_time(&session->timer, delta_ms, now_ms);
    if (err != ESP_OK) {
        gm_room_session_sessions_unlock();
        return err;
    }
    /*
     * Policy: if finished timer gets extra time, keep it paused (manual resume).
     * This avoids implicit autostart on "add time".
     */
    switch (session->timer.state) {
    case GM_TIMER_RUNNING:
        session->state = GM_SESSION_RUNNING;
        session->finished_at_ms = 0;
        break;
    case GM_TIMER_PAUSED:
        session->state = GM_SESSION_PAUSED;
        session->finished_at_ms = 0;
        break;
    case GM_TIMER_FINISHED:
        session->state = GM_SESSION_FINISHED;
        session->finished_at_ms = now_ms;
        break;
    case GM_TIMER_IDLE:
    default:
        session->state = GM_SESSION_IDLE;
        session->finished_at_ms = 0;
        break;
    }
    gm_room_session_mark_session_changed_locked(session);
    gm_room_session_sessions_unlock();
    return ESP_OK;
}

esp_err_t gm_room_session_set_hint(const char *room_id, const char *message, uint64_t now_ms)
{
    esp_err_t err = ESP_OK;
    gm_room_session_t *session = NULL;
    if (!room_id || !room_id[0] || !message || !message[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    err = gm_room_session_sessions_lock();
    if (err != ESP_OK) {
        return err;
    }
    session = alloc_session_locked(room_id);
    if (!session) {
        gm_room_session_sessions_unlock();
        return ESP_ERR_NO_MEM;
    }
    err = gm_hint_send(&session->hint, message, now_ms);
    if (err == ESP_OK) {
        gm_room_session_mark_session_changed_locked(session);
    }
    gm_room_session_sessions_unlock();
    return err;
}

esp_err_t gm_room_session_clear_hint(const char *room_id, uint64_t now_ms)
{
    esp_err_t err = ESP_OK;
    gm_room_session_t *session = NULL;
    err = gm_room_session_sessions_lock();
    if (err != ESP_OK) {
        return err;
    }
    session = find_session_mutable_locked(room_id);
    if (!session) {
        gm_room_session_sessions_unlock();
        return ESP_ERR_NOT_FOUND;
    }
    err = gm_hint_clear(&session->hint, now_ms);
    if (err == ESP_OK) {
        gm_room_session_mark_session_changed_locked(session);
    }
    gm_room_session_sessions_unlock();
    return err;
}
