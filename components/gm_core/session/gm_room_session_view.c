#include "gm_room_session.h"
#include "gm_room_session_internal.h"

#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"

static EXT_RAM_BSS_ATTR gm_room_scenario_runtime_semantics_t s_runtime_semantics_scratch;
static EXT_RAM_BSS_ATTR gm_room_scenario_branch_semantics_t s_branch_semantics_scratch;
static EXT_RAM_BSS_ATTR char
    s_device_ref_scratch[ROOM_SCENARIO_MAX_STEPS][ROOM_SCENARIO_DEVICE_ID_MAX_LEN];
static SemaphoreHandle_t s_view_scratch_mutex = NULL;
static StaticSemaphore_t s_view_scratch_mutex_storage;
static portMUX_TYPE s_view_scratch_mutex_init_lock = portMUX_INITIALIZER_UNLOCKED;

static esp_err_t gm_room_session_view_scratch_lock(void)
{
    if (!s_view_scratch_mutex) {
        portENTER_CRITICAL(&s_view_scratch_mutex_init_lock);
        if (!s_view_scratch_mutex) {
            s_view_scratch_mutex = xSemaphoreCreateMutexStatic(&s_view_scratch_mutex_storage);
        }
        portEXIT_CRITICAL(&s_view_scratch_mutex_init_lock);
    }
    if (!s_view_scratch_mutex) {
        return ESP_ERR_NO_MEM;
    }
    return xSemaphoreTake(s_view_scratch_mutex, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void gm_room_session_view_scratch_unlock(void)
{
    if (s_view_scratch_mutex) {
        xSemaphoreGive(s_view_scratch_mutex);
    }
}

static void gm_room_session_view_copy(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_size, "%s", src);
}

static void gm_room_session_projection_collect_scenario_devices(
    gm_room_session_projection_view_t *out,
    const room_scenario_t *scenario)
{
    size_t count = 0;
    if (!out || !scenario) {
        return;
    }
    out->scenario_device_count = 0;
    if (room_scenario_collect_device_refs(scenario,
                                          out->scenario_device_ids,
                                          ROOM_SCENARIO_MAX_STEPS,
                                          &count) == ESP_OK ||
        count > 0) {
        out->scenario_device_count = (uint8_t)(count > UINT8_MAX ? UINT8_MAX : count);
    }
}

static uint8_t gm_room_session_count_scenario_devices(const room_scenario_t *scenario)
{
    size_t count = 0;

    if (!scenario) {
        return 0;
    }
    if (gm_room_session_view_scratch_lock() != ESP_OK) {
        return 0;
    }
    memset(s_device_ref_scratch, 0, sizeof(s_device_ref_scratch));
    (void)room_scenario_collect_device_refs(scenario,
                                            s_device_ref_scratch,
                                            ROOM_SCENARIO_MAX_STEPS,
                                            &count);
    gm_room_session_view_scratch_unlock();
    return (uint8_t)(count > UINT8_MAX ? UINT8_MAX : count);
}

static void gm_room_session_format_step_text(const room_scenario_step_t *step,
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
        gm_room_session_view_copy(out_text, out_text_len, step->label);
        return;
    }
    switch (step->type) {
    case ROOM_SCENARIO_STEP_WAIT_TIME:
        gm_room_session_view_copy(out_text, out_text_len, "Wait");
        break;
    case ROOM_SCENARIO_STEP_OPERATOR_APPROVAL:
        gm_room_session_view_copy(out_text, out_text_len, "Operator approval");
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
        gm_room_session_view_copy(out_text, out_text_len, "Command group");
        break;
    case ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE:
        gm_room_session_view_copy(out_text, out_text_len, "Operator message");
        break;
    case ROOM_SCENARIO_STEP_SET_FLAG:
        snprintf(out_text,
                 out_text_len,
                 "Set flag %s",
                 step->data.set_flag.name[0] ? step->data.set_flag.name : "flag");
        break;
    case ROOM_SCENARIO_STEP_WAIT_FLAGS:
        gm_room_session_view_copy(out_text, out_text_len, "Wait flags");
        break;
    case ROOM_SCENARIO_STEP_WAIT_ANY_DEVICE_EVENT:
        gm_room_session_view_copy(out_text, out_text_len, "Wait any event");
        break;
    case ROOM_SCENARIO_STEP_WAIT_ALL_DEVICE_EVENTS:
        gm_room_session_view_copy(out_text, out_text_len, "Wait all events");
        break;
    case ROOM_SCENARIO_STEP_END_GAME:
        gm_room_session_view_copy(out_text, out_text_len, "End game");
        break;
    default:
        gm_room_session_view_copy(out_text, out_text_len, "Step");
        break;
    }
}

static void gm_room_session_fill_wait_summary(gm_room_scenario_wait_type_t wait_type,
                                              uint32_t wait_started_at_ms,
                                              uint32_t wait_until_ms,
                                              const char *wait_source_id,
                                              const char *wait_event_type,
                                              uint8_t wait_event_count,
                                              uint8_t wait_flag_count,
                                              const char *wait_operator_prompt,
                                              char *out_summary,
                                              size_t out_summary_size)
{
    if (!out_summary || out_summary_size == 0) {
        return;
    }
    out_summary[0] = '\0';
    switch (wait_type) {
    case GM_ROOM_SCENARIO_WAIT_TIME: {
        uint32_t duration_ms = 0;
        uint32_t duration_sec = 0;
        if (wait_until_ms > wait_started_at_ms) {
            duration_ms = wait_until_ms - wait_started_at_ms;
        }
        duration_sec = duration_ms > 0 ? (uint32_t)((duration_ms + 999U) / 1000U) : 0;
        if (duration_sec > 0) {
            snprintf(out_summary, out_summary_size, "%lu sec", (unsigned long)duration_sec);
        } else {
            gm_room_session_view_copy(out_summary, out_summary_size, "timer");
        }
        break;
    }
    case GM_ROOM_SCENARIO_WAIT_DEVICE_COMMAND_RESULT:
        if (wait_event_type && strcmp(wait_event_type, "__dispatch_pending__") == 0) {
            gm_room_session_view_copy(out_summary, out_summary_size, "dispatch");
        } else {
            snprintf(out_summary,
                     out_summary_size,
                     "command result %s",
                     wait_event_type && wait_event_type[0] ? wait_event_type : "pending");
        }
        break;
    case GM_ROOM_SCENARIO_WAIT_DEVICE_EVENT:
        if ((wait_source_id && wait_source_id[0]) || (wait_event_type && wait_event_type[0])) {
            snprintf(out_summary,
                     out_summary_size,
                     "%s%s%s",
                     wait_source_id && wait_source_id[0] ? wait_source_id : "device",
                     wait_event_type && wait_event_type[0] ? ": " : "",
                     wait_event_type ? wait_event_type : "");
        } else {
            gm_room_session_view_copy(out_summary, out_summary_size, "device event");
        }
        break;
    case GM_ROOM_SCENARIO_WAIT_ANY_DEVICE_EVENT:
        snprintf(out_summary, out_summary_size, "any of %u events", (unsigned)wait_event_count);
        break;
    case GM_ROOM_SCENARIO_WAIT_ALL_DEVICE_EVENTS:
        snprintf(out_summary, out_summary_size, "all of %u events", (unsigned)wait_event_count);
        break;
    case GM_ROOM_SCENARIO_WAIT_FLAGS:
        gm_room_session_view_copy(out_summary, out_summary_size, wait_flag_count ? "flags" : "none");
        break;
    case GM_ROOM_SCENARIO_WAIT_OPERATOR:
        gm_room_session_view_copy(out_summary,
                                  out_summary_size,
                                  wait_operator_prompt && wait_operator_prompt[0]
                                      ? wait_operator_prompt
                                      : "operator approval");
        break;
    case GM_ROOM_SCENARIO_WAIT_NONE:
    default:
        gm_room_session_view_copy(out_summary, out_summary_size, "none");
        break;
    }
}

static uint16_t gm_room_session_branch_local_step_index(const gm_room_scenario_branch_runtime_t *runtime)
{
    uint16_t raw = 0;
    uint16_t start = 0;
    uint16_t count = 0;
    uint16_t local = 0;
    if (!runtime) {
        return 0;
    }
    raw = runtime->current_step_index;
    start = runtime->step_start_index;
    count = runtime->step_count;
    if (count == 0) {
        return 0;
    }
    if (raw >= start && raw < (uint16_t)(start + count)) {
        local = (uint16_t)(raw - start);
    } else if (raw >= (uint16_t)(start + count)) {
        local = count;
    } else {
        local = raw;
    }
    return local > count ? count : local;
}

static uint16_t gm_room_session_branch_done_steps(const gm_room_scenario_branch_runtime_t *runtime)
{
    uint16_t count = 0;
    uint16_t local = 0;
    if (!runtime) {
        return 0;
    }
    count = runtime->step_count;
    if (count == 0) {
        return 0;
    }
    if (runtime->scenario_state == GM_ROOM_SCENARIO_DONE) {
        return count;
    }
    local = gm_room_session_branch_local_step_index(runtime);
    switch (runtime->scenario_state) {
    case GM_ROOM_SCENARIO_RUNNING:
    case GM_ROOM_SCENARIO_WAITING:
    case GM_ROOM_SCENARIO_PAUSED:
    case GM_ROOM_SCENARIO_COOLDOWN:
    case GM_ROOM_SCENARIO_ERROR:
        return local > count ? count : local;
    case GM_ROOM_SCENARIO_IDLE:
    case GM_ROOM_SCENARIO_STOPPED:
    case GM_ROOM_SCENARIO_DONE:
    default:
        return 0;
    }
}

static int16_t gm_room_session_branch_failed_step_index(const gm_room_scenario_branch_runtime_t *runtime)
{
    uint16_t total = 0;
    uint16_t local = 0;
    if (!runtime || runtime->scenario_state != GM_ROOM_SCENARIO_ERROR) {
        return -1;
    }
    total = runtime->step_count;
    if (total == 0) {
        return -1;
    }
    local = gm_room_session_branch_local_step_index(runtime);
    if (local >= total) {
        return (int16_t)(total - 1);
    }
    return (int16_t)local;
}

static gm_room_scenario_step_runtime_state_t
gm_room_session_branch_current_step_state(const gm_room_scenario_branch_runtime_t *runtime)
{
    uint16_t total = 0;
    uint16_t local = 0;
    uint16_t done = 0;
    if (!runtime) {
        return GM_ROOM_SCENARIO_STEP_STATE_PENDING;
    }
    total = runtime->step_count;
    if (total == 0) {
        return GM_ROOM_SCENARIO_STEP_STATE_PENDING;
    }
    if (runtime->scenario_state == GM_ROOM_SCENARIO_DONE) {
        return GM_ROOM_SCENARIO_STEP_STATE_DONE;
    }
    local = gm_room_session_branch_local_step_index(runtime);
    done = gm_room_session_branch_done_steps(runtime);
    if (local < done) {
        return GM_ROOM_SCENARIO_STEP_STATE_DONE;
    }
    if (local >= total) {
        return GM_ROOM_SCENARIO_STEP_STATE_PENDING;
    }
    switch (runtime->scenario_state) {
    case GM_ROOM_SCENARIO_WAITING:
        return GM_ROOM_SCENARIO_STEP_STATE_WAITING;
    case GM_ROOM_SCENARIO_ERROR:
        return GM_ROOM_SCENARIO_STEP_STATE_ERROR;
    case GM_ROOM_SCENARIO_RUNNING:
    case GM_ROOM_SCENARIO_PAUSED:
        return GM_ROOM_SCENARIO_STEP_STATE_CURRENT;
    case GM_ROOM_SCENARIO_DONE:
        return GM_ROOM_SCENARIO_STEP_STATE_DONE;
    case GM_ROOM_SCENARIO_IDLE:
    case GM_ROOM_SCENARIO_STOPPED:
    case GM_ROOM_SCENARIO_COOLDOWN:
    default:
        return GM_ROOM_SCENARIO_STEP_STATE_PENDING;
    }
}

static void gm_room_session_branch_wait_skip(const gm_room_session_t *session,
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
        gm_room_session_view_copy(out_label, out_label_len, runtime->wait_operator_skip_label);
    }
    if (runtime->scenario_state == GM_ROOM_SCENARIO_WAITING &&
        runtime->wait_type != GM_ROOM_SCENARIO_WAIT_NONE &&
        runtime->current_step_index < session->running_scenario.step_count) {
        step = &session->running_scenario.steps[runtime->current_step_index];
        if (step->allow_operator_skip) {
            allowed = true;
            if (!out_label[0]) {
                gm_room_session_view_copy(out_label,
                                          out_label_len,
                                          step->operator_skip_label[0] ? step->operator_skip_label : "Skip wait");
            }
        }
    }
    *out_allowed = allowed;
}

static uint16_t gm_room_session_total_steps(const gm_room_session_t *session)
{
    bool has_flow = false;
    uint16_t total = 0;
    if (!session) {
        return 0;
    }
    for (uint8_t i = 0; i < session->branch_runtime_count && i < ROOM_SCENARIO_MAX_BRANCHES; ++i) {
        if (session->branch_runtimes[i].type != ROOM_SCENARIO_BRANCH_REACTIVE) {
            has_flow = true;
            total = (uint16_t)(total + session->branch_runtimes[i].step_count);
        }
    }
    if (has_flow) {
        return total;
    }
    total = 0;
    for (uint8_t i = 0; i < session->branch_runtime_count && i < ROOM_SCENARIO_MAX_BRANCHES; ++i) {
        total = (uint16_t)(total + session->branch_runtimes[i].step_count);
    }
    return total;
}

static uint16_t gm_room_session_done_steps(const gm_room_session_t *session)
{
    bool has_flow = false;
    uint16_t done = 0;
    if (!session) {
        return 0;
    }
    for (uint8_t i = 0; i < session->branch_runtime_count && i < ROOM_SCENARIO_MAX_BRANCHES; ++i) {
        if (session->branch_runtimes[i].type != ROOM_SCENARIO_BRANCH_REACTIVE) {
            has_flow = true;
            done = (uint16_t)(done + gm_room_session_branch_done_steps(&session->branch_runtimes[i]));
        }
    }
    if (has_flow) {
        return done;
    }
    done = 0;
    for (uint8_t i = 0; i < session->branch_runtime_count && i < ROOM_SCENARIO_MAX_BRANCHES; ++i) {
        done = (uint16_t)(done + gm_room_session_branch_done_steps(&session->branch_runtimes[i]));
    }
    return done;
}

static void gm_room_session_fill_timer_view(const gm_room_session_t *session,
                                            uint64_t now_ms,
                                            gm_room_session_timer_view_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (!session) {
        return;
    }
    out->present = true;
    out->generation = session->generation;
    out->session_state = session->state;
    out->session_active =
        (session->state == GM_SESSION_RUNNING || session->state == GM_SESSION_PAUSED);
    out->started_at_ms = session->started_at_ms;
    out->finished_at_ms = session->finished_at_ms;
    out->timer_state = session->timer.state;
    out->duration_ms = session->timer.duration_ms;
    out->remaining_ms = gm_timer_get_remaining(&session->timer, now_ms);
    out->paused_at_ms = session->timer.paused_at_ms;
    out->hint_active = session->hint.active;
    out->hint_count = session->hint.sent_count;
    out->hint_updated_at_ms = session->hint.last_changed_ms;
    gm_room_session_view_copy(out->hint_text, sizeof(out->hint_text), session->hint.message);
}

static void gm_room_session_fill_selected_view(const gm_room_session_t *session,
                                               gm_room_session_selected_view_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (!session) {
        return;
    }
    out->present = true;
    out->generation = session->generation;
    out->selected_scenario_generation = session->selected_scenario_generation;
    gm_room_session_view_copy(out->selected_profile_id,
                              sizeof(out->selected_profile_id),
                              session->selected_profile_id);
    gm_room_session_view_copy(out->selected_profile_name,
                              sizeof(out->selected_profile_name),
                              session->selected_profile_name);
    gm_room_session_view_copy(out->selected_profile_scenario_id,
                              sizeof(out->selected_profile_scenario_id),
                              session->selected_profile_scenario_id);
    out->selected_profile_duration_ms = session->selected_profile_duration_ms;
    gm_room_session_view_copy(out->selected_scenario_id,
                              sizeof(out->selected_scenario_id),
                              session->selected_scenario_id);
    gm_room_session_view_copy(out->selected_scenario_name,
                              sizeof(out->selected_scenario_name),
                              session->selected_scenario_name);
    out->running_scenario_valid = session->running_scenario_valid;
    if (session->running_scenario_valid) {
        gm_room_session_view_copy(out->running_scenario_id,
                                  sizeof(out->running_scenario_id),
                                  session->running_scenario.id);
        gm_room_session_view_copy(out->running_scenario_name,
                                  sizeof(out->running_scenario_name),
                                  session->running_scenario.name);
        out->running_scenario_generation = session->running_scenario_generation;
    }
}

static void gm_room_session_fill_runtime_summary(const gm_room_session_t *session,
                                                 gm_room_session_runtime_summary_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (!session) {
        return;
    }
    out->present = true;
    out->generation = session->generation;
    out->scenario_state = session->scenario_state;
    if (gm_room_session_view_scratch_lock() == ESP_OK) {
        memset(&s_runtime_semantics_scratch, 0, sizeof(s_runtime_semantics_scratch));
        if (gm_room_session_describe_runtime(session, &s_runtime_semantics_scratch) == ESP_OK) {
            out->total_steps = s_runtime_semantics_scratch.total_steps;
            out->done_steps = s_runtime_semantics_scratch.done_steps;
            gm_room_session_view_copy(out->current_step_text,
                                      sizeof(out->current_step_text),
                                      s_runtime_semantics_scratch.current_step_text);
            gm_room_session_view_copy(out->wait_summary,
                                      sizeof(out->wait_summary),
                                      s_runtime_semantics_scratch.wait_summary);
        }
        gm_room_session_view_scratch_unlock();
    } else {
        out->total_steps = gm_room_session_total_steps(session);
        out->done_steps = gm_room_session_done_steps(session);
    }
    out->current_step_index = session->current_step_index;
    out->wait_type = session->wait_type;
    out->wait_until_ms = session->wait_until_ms;
    out->wait_started_at_ms = session->wait_started_at_ms;
    gm_room_session_view_copy(out->wait_event_type,
                              sizeof(out->wait_event_type),
                              session->wait_event_type);
    gm_room_session_view_copy(out->wait_source_id,
                              sizeof(out->wait_source_id),
                              session->wait_source_id);
    out->wait_event_count = session->wait_event_count;
    memcpy(out->wait_events, session->wait_events, sizeof(out->wait_events));
    out->wait_flag_count = session->wait_flag_count;
    memcpy(out->wait_flags, session->wait_flags, sizeof(out->wait_flags));
    gm_room_session_view_copy(out->wait_operator_prompt,
                              sizeof(out->wait_operator_prompt),
                              session->wait_operator_prompt);
    gm_room_session_view_copy(out->wait_operator_label,
                              sizeof(out->wait_operator_label),
                              session->wait_operator_label);
    out->wait_operator_skip_allowed = session->wait_operator_skip_allowed;
    gm_room_session_view_copy(out->wait_operator_skip_label,
                              sizeof(out->wait_operator_skip_label),
                              session->wait_operator_skip_label);
    gm_room_session_view_copy(out->scenario_operator_message,
                              sizeof(out->scenario_operator_message),
                              session->scenario_operator_message);
    out->scenario_flag_count = session->scenario_flag_count;
    memcpy(out->scenario_flags, session->scenario_flags, sizeof(out->scenario_flags));
    if (session->running_scenario_valid) {
        out->scenario_device_count =
            gm_room_session_count_scenario_devices(&session->running_scenario);
    }
    gm_room_session_view_copy(out->scenario_last_error,
                              sizeof(out->scenario_last_error),
                              session->scenario_last_error);
}

static void gm_room_session_fill_branch_runtime_view(
    const gm_room_session_t *session,
    const gm_room_scenario_branch_runtime_t *runtime,
    uint8_t branch_index,
    gm_room_session_branch_runtime_view_t *out)
{
    const room_scenario_branch_t *branch = NULL;

    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (!session || !runtime) {
        return;
    }
    if (session->running_scenario.branch_count > branch_index) {
        branch = &session->running_scenario.branches[branch_index];
    }
    gm_room_session_view_copy(out->id,
                              sizeof(out->id),
                              branch && branch->id[0] ? branch->id : "main");
    gm_room_session_view_copy(out->name,
                              sizeof(out->name),
                              branch && branch->name[0] ? branch->name : "Main");
    out->active = runtime->active;
    out->type = runtime->type;
    out->required_for_completion = runtime->required_for_completion;
    out->priority = runtime->priority;
    out->cooldown_ms = runtime->cooldown_ms;
    out->cooldown_until_ms = runtime->cooldown_until_ms;
    out->max_fire_count = runtime->max_fire_count;
    out->fire_count = runtime->fire_count;
    out->run_once = runtime->run_once;
    out->fired_once = runtime->fired_once;
    out->reentry_mode = runtime->reentry_mode;
    out->pending_trigger = runtime->pending_trigger;
    out->step_start_index = runtime->step_start_index;
    out->step_count = runtime->step_count;
    out->current_step_index = runtime->current_step_index;
    out->total_steps = runtime->step_count;
    out->scenario_state = runtime->scenario_state;
    out->wait_type = runtime->wait_type;
    out->wait_until_ms = runtime->wait_until_ms;
    out->wait_started_at_ms = runtime->wait_started_at_ms;
    if (gm_room_session_view_scratch_lock() == ESP_OK) {
        memset(&s_branch_semantics_scratch, 0, sizeof(s_branch_semantics_scratch));
        if (gm_room_session_describe_branch_runtime(session, runtime, &s_branch_semantics_scratch) == ESP_OK) {
            out->current_local_step_index = s_branch_semantics_scratch.current_local_step_index;
            out->done_steps = s_branch_semantics_scratch.done_steps;
            out->total_steps = s_branch_semantics_scratch.total_steps;
            out->failed_step_index = s_branch_semantics_scratch.failed_step_index;
            out->current_step_state = s_branch_semantics_scratch.current_step_state;
            gm_room_session_view_copy(out->current_step_text,
                                      sizeof(out->current_step_text),
                                      s_branch_semantics_scratch.current_step_text);
            gm_room_session_view_copy(out->wait_summary,
                                      sizeof(out->wait_summary),
                                      s_branch_semantics_scratch.wait_summary);
            out->wait_operator_skip_allowed = s_branch_semantics_scratch.wait_operator_skip_allowed;
            gm_room_session_view_copy(out->wait_operator_skip_label,
                                      sizeof(out->wait_operator_skip_label),
                                      s_branch_semantics_scratch.wait_operator_skip_label);
        }
        gm_room_session_view_scratch_unlock();
    }
}

esp_err_t gm_room_session_get_read_views(const char *room_id,
                                         uint64_t now_ms,
                                         gm_room_session_timer_view_t *out_timer,
                                         gm_room_session_selected_view_t *out_selected,
                                         gm_room_session_runtime_summary_t *out_runtime)
{
    esp_err_t err = ESP_OK;
    gm_room_session_t *session = NULL;

    if (!room_id || !room_id[0] ||
        (!out_timer && !out_selected && !out_runtime)) {
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
    if (out_timer) {
        gm_room_session_fill_timer_view(session, now_ms, out_timer);
    }
    if (out_selected) {
        gm_room_session_fill_selected_view(session, out_selected);
    }
    if (out_runtime) {
        gm_room_session_fill_runtime_summary(session, out_runtime);
    }
    gm_room_session_sessions_unlock();
    return ESP_OK;
}

esp_err_t gm_room_session_get_timer_view(const char *room_id,
                                         uint64_t now_ms,
                                         gm_room_session_timer_view_t *out)
{
    return gm_room_session_get_read_views(room_id, now_ms, out, NULL, NULL);
}

esp_err_t gm_room_session_get_selected_view(const char *room_id,
                                            gm_room_session_selected_view_t *out)
{
    return gm_room_session_get_read_views(room_id, 0, NULL, out, NULL);
}

esp_err_t gm_room_session_get_runtime_summary(const char *room_id,
                                              gm_room_session_runtime_summary_t *out)
{
    return gm_room_session_get_read_views(room_id, 0, NULL, NULL, out);
}

void gm_room_session_build_projection_view(const gm_room_session_t *session,
                                           uint64_t now_ms,
                                           gm_room_session_projection_view_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (!session) {
        return;
    }
    out->present = true;
    gm_room_session_fill_timer_view(session, now_ms, &out->timer);
    gm_room_session_fill_selected_view(session, &out->selected);
    gm_room_session_fill_runtime_summary(session, &out->runtime);
    if (session->running_scenario_valid) {
        gm_room_session_projection_collect_scenario_devices(out, &session->running_scenario);
    }
    out->branch_count = session->branch_runtime_count > ROOM_SCENARIO_MAX_BRANCHES
                            ? ROOM_SCENARIO_MAX_BRANCHES
                            : session->branch_runtime_count;
    for (uint8_t i = 0; i < out->branch_count; ++i) {
        gm_room_session_fill_branch_runtime_view(session,
                                                 &session->branch_runtimes[i],
                                                 i,
                                                 &out->branches[i]);
    }
}

esp_err_t gm_room_session_get_projection_view(const char *room_id,
                                              uint64_t now_ms,
                                              gm_room_session_projection_view_t *out)
{
    esp_err_t err = ESP_OK;
    gm_room_session_t *session = NULL;

    if (!room_id || !room_id[0] || !out) {
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
    gm_room_session_build_projection_view(session, now_ms, out);
    gm_room_session_sessions_unlock();
    return ESP_OK;
}

esp_err_t gm_room_session_describe_runtime(const gm_room_session_t *session,
                                           gm_room_scenario_runtime_semantics_t *out)
{
    const room_scenario_step_t *step = NULL;
    if (!session || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->total_steps = gm_room_session_total_steps(session);
    out->done_steps = gm_room_session_done_steps(session);
    if (session->scenario_state == GM_ROOM_SCENARIO_DONE) {
        gm_room_session_view_copy(out->current_step_text, sizeof(out->current_step_text), "Complete");
    } else if (session->running_scenario_valid &&
               session->current_step_index < session->running_scenario.step_count) {
        step = &session->running_scenario.steps[session->current_step_index];
        gm_room_session_format_step_text(step, out->current_step_text, sizeof(out->current_step_text));
    }
    gm_room_session_fill_wait_summary(session->wait_type,
                                      session->wait_started_at_ms,
                                      session->wait_until_ms,
                                      session->wait_source_id,
                                      session->wait_event_type,
                                      session->wait_event_count,
                                      session->wait_flag_count,
                                      session->wait_operator_prompt,
                                      out->wait_summary,
                                      sizeof(out->wait_summary));
    return ESP_OK;
}

esp_err_t gm_room_session_describe_branch_runtime(
    const gm_room_session_t *session,
    const gm_room_scenario_branch_runtime_t *runtime,
    gm_room_scenario_branch_semantics_t *out)
{
    const room_scenario_step_t *step = NULL;
    if (!session || !runtime || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->current_local_step_index = gm_room_session_branch_local_step_index(runtime);
    out->done_steps = gm_room_session_branch_done_steps(runtime);
    out->total_steps = runtime->step_count;
    out->failed_step_index = gm_room_session_branch_failed_step_index(runtime);
    out->current_step_state = gm_room_session_branch_current_step_state(runtime);
    if (runtime->type == ROOM_SCENARIO_BRANCH_REACTIVE &&
        runtime->scenario_state == GM_ROOM_SCENARIO_WAITING &&
        runtime->wait_type == GM_ROOM_SCENARIO_WAIT_NONE) {
        gm_room_session_view_copy(out->current_step_text,
                                  sizeof(out->current_step_text),
                                  "Waiting for trigger");
    } else if (runtime->scenario_state == GM_ROOM_SCENARIO_DONE) {
        gm_room_session_view_copy(out->current_step_text, sizeof(out->current_step_text), "Complete");
    } else if (session->running_scenario_valid &&
               runtime->current_step_index < session->running_scenario.step_count) {
        step = &session->running_scenario.steps[runtime->current_step_index];
        gm_room_session_format_step_text(step, out->current_step_text, sizeof(out->current_step_text));
    }
    gm_room_session_fill_wait_summary(runtime->wait_type,
                                      runtime->wait_started_at_ms,
                                      runtime->wait_until_ms,
                                      runtime->wait_source_id,
                                      runtime->wait_event_type,
                                      runtime->wait_event_count,
                                      runtime->wait_flag_count,
                                      runtime->wait_operator_prompt,
                                      out->wait_summary,
                                      sizeof(out->wait_summary));
    gm_room_session_branch_wait_skip(session,
                                     runtime,
                                     &out->wait_operator_skip_allowed,
                                     out->wait_operator_skip_label,
                                     sizeof(out->wait_operator_skip_label));
    return ESP_OK;
}
