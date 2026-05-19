#include "orchestrator_registry_internal.h"

#include <string.h>

#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static EXT_RAM_BSS_ATTR quest_device_t s_scratch_devices[ORCH_REGISTRY_MAX_DEVICES];
static EXT_RAM_BSS_ATTR device_control_ingest_device_t s_scratch_ingest;
static EXT_RAM_BSS_ATTR device_control_ingest_device_t s_scratch_ingest_devices[ORCH_REGISTRY_MAX_DEVICES];
static EXT_RAM_BSS_ATTR gm_room_session_t s_scratch_session;
static EXT_RAM_BSS_ATTR room_scenario_t s_scratch_room_scenario;
static EXT_RAM_BSS_ATTR room_scenario_validation_report_t s_scratch_validation_report;
static SemaphoreHandle_t s_scratch_mutex = NULL;
static StaticSemaphore_t s_scratch_mutex_storage;
static portMUX_TYPE s_scratch_mutex_init_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_scratch_owner = NULL;

static void orch_scratch_assert_locked_by_caller(void)
{
    configASSERT(s_scratch_mutex != NULL);
    configASSERT(s_scratch_owner == xTaskGetCurrentTaskHandle());
}

const char *orch_default_room_id(void)
{
    return "unassigned";
}

uint64_t orch_now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

esp_err_t orch_scratch_lock(void)
{
    if (!s_scratch_mutex) {
        portENTER_CRITICAL(&s_scratch_mutex_init_lock);
        if (!s_scratch_mutex) {
            s_scratch_mutex = xSemaphoreCreateMutexStatic(&s_scratch_mutex_storage);
        }
        portEXIT_CRITICAL(&s_scratch_mutex_init_lock);
    }
    if (!s_scratch_mutex) {
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(s_scratch_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    s_scratch_owner = xTaskGetCurrentTaskHandle();
    return ESP_OK;
}

void orch_scratch_unlock(void)
{
    if (s_scratch_mutex) {
        configASSERT(s_scratch_owner == xTaskGetCurrentTaskHandle());
        s_scratch_owner = NULL;
        xSemaphoreGive(s_scratch_mutex);
    }
}

quest_device_t *orch_scratch_devices(size_t *out_capacity)
{
    orch_scratch_assert_locked_by_caller();
    if (out_capacity) {
        *out_capacity = ORCH_REGISTRY_MAX_DEVICES;
    }
    memset(s_scratch_devices, 0, sizeof(s_scratch_devices));
    return s_scratch_devices;
}

device_control_ingest_device_t *orch_scratch_ingest(void)
{
    orch_scratch_assert_locked_by_caller();
    memset(&s_scratch_ingest, 0, sizeof(s_scratch_ingest));
    return &s_scratch_ingest;
}

device_control_ingest_device_t *orch_scratch_ingest_devices(size_t *out_capacity)
{
    orch_scratch_assert_locked_by_caller();
    if (out_capacity) {
        *out_capacity = ORCH_REGISTRY_MAX_DEVICES;
    }
    memset(s_scratch_ingest_devices, 0, sizeof(s_scratch_ingest_devices));
    return s_scratch_ingest_devices;
}

gm_room_session_t *orch_scratch_session(void)
{
    orch_scratch_assert_locked_by_caller();
    memset(&s_scratch_session, 0, sizeof(s_scratch_session));
    return &s_scratch_session;
}

room_scenario_t *orch_scratch_room_scenario(void)
{
    orch_scratch_assert_locked_by_caller();
    memset(&s_scratch_room_scenario, 0, sizeof(s_scratch_room_scenario));
    return &s_scratch_room_scenario;
}

room_scenario_validation_report_t *orch_scratch_validation_report(void)
{
    orch_scratch_assert_locked_by_caller();
    memset(&s_scratch_validation_report, 0, sizeof(s_scratch_validation_report));
    return &s_scratch_validation_report;
}

bool orch_runtime_is_active(orch_runtime_state_t state)
{
    return state == ORCH_RUNTIME_STATE_ARMED ||
           state == ORCH_RUNTIME_STATE_ACTIVE ||
           state == ORCH_RUNTIME_STATE_PAUSED;
}

const char *orch_connectivity_str(orch_connectivity_t connectivity)
{
    switch (connectivity) {
    case ORCH_CONNECTIVITY_ONLINE:
        return "online";
    case ORCH_CONNECTIVITY_OFFLINE:
        return "offline";
    case ORCH_CONNECTIVITY_UNKNOWN:
    default:
        return "unknown";
    }
}

const char *orch_health_str(orch_health_t health)
{
    switch (health) {
    case ORCH_HEALTH_DEGRADED:
        return "degraded";
    case ORCH_HEALTH_FAULT:
        return "fault";
    case ORCH_HEALTH_OK:
    default:
        return "ok";
    }
}

const char *orch_runtime_state_str(orch_runtime_state_t state)
{
    switch (state) {
    case ORCH_RUNTIME_STATE_IDLE:
        return "idle";
    case ORCH_RUNTIME_STATE_ARMED:
        return "armed";
    case ORCH_RUNTIME_STATE_ACTIVE:
        return "active";
    case ORCH_RUNTIME_STATE_PAUSED:
        return "paused";
    case ORCH_RUNTIME_STATE_COMPLETED:
        return "completed";
    case ORCH_RUNTIME_STATE_TIMEOUT:
        return "timeout";
    case ORCH_RUNTIME_STATE_FAILED:
        return "failed";
    case ORCH_RUNTIME_STATE_UNKNOWN:
    default:
        return "unknown";
    }
}

orch_room_scenario_runtime_state_t orch_runtime_state_from_gm(gm_room_scenario_state_t state)
{
    switch (state) {
    case GM_ROOM_SCENARIO_RUNNING:
        return ORCH_ROOM_SCENARIO_RUNTIME_RUNNING;
    case GM_ROOM_SCENARIO_WAITING:
        return ORCH_ROOM_SCENARIO_RUNTIME_WAITING;
    case GM_ROOM_SCENARIO_PAUSED:
        return ORCH_ROOM_SCENARIO_RUNTIME_PAUSED;
    case GM_ROOM_SCENARIO_DONE:
        return ORCH_ROOM_SCENARIO_RUNTIME_DONE;
    case GM_ROOM_SCENARIO_STOPPED:
        return ORCH_ROOM_SCENARIO_RUNTIME_STOPPED;
    case GM_ROOM_SCENARIO_COOLDOWN:
        return ORCH_ROOM_SCENARIO_RUNTIME_COOLDOWN;
    case GM_ROOM_SCENARIO_ERROR:
        return ORCH_ROOM_SCENARIO_RUNTIME_ERROR;
    case GM_ROOM_SCENARIO_IDLE:
    default:
        return ORCH_ROOM_SCENARIO_RUNTIME_IDLE;
    }
}

const char *orch_room_scenario_runtime_state_str(orch_room_scenario_runtime_state_t state)
{
    switch (state) {
    case ORCH_ROOM_SCENARIO_RUNTIME_RUNNING:
        return "running";
    case ORCH_ROOM_SCENARIO_RUNTIME_WAITING:
        return "waiting";
    case ORCH_ROOM_SCENARIO_RUNTIME_PAUSED:
        return "paused";
    case ORCH_ROOM_SCENARIO_RUNTIME_DONE:
        return "done";
    case ORCH_ROOM_SCENARIO_RUNTIME_STOPPED:
        return "stopped";
    case ORCH_ROOM_SCENARIO_RUNTIME_COOLDOWN:
        return "cooldown";
    case ORCH_ROOM_SCENARIO_RUNTIME_ERROR:
        return "error";
    case ORCH_ROOM_SCENARIO_RUNTIME_IDLE:
    default:
        return "idle";
    }
}

orch_room_scenario_wait_type_t orch_wait_type_from_gm(gm_room_scenario_wait_type_t wait_type)
{
    switch (wait_type) {
    case GM_ROOM_SCENARIO_WAIT_TIME:
        return ORCH_ROOM_SCENARIO_WAIT_TIME;
    case GM_ROOM_SCENARIO_WAIT_DEVICE_EVENT:
        return ORCH_ROOM_SCENARIO_WAIT_DEVICE_EVENT;
    case GM_ROOM_SCENARIO_WAIT_ANY_DEVICE_EVENT:
        return ORCH_ROOM_SCENARIO_WAIT_ANY_DEVICE_EVENT;
    case GM_ROOM_SCENARIO_WAIT_OPERATOR:
        return ORCH_ROOM_SCENARIO_WAIT_OPERATOR;
    case GM_ROOM_SCENARIO_WAIT_FLAGS:
        return ORCH_ROOM_SCENARIO_WAIT_FLAGS;
    case GM_ROOM_SCENARIO_WAIT_ALL_DEVICE_EVENTS:
        return ORCH_ROOM_SCENARIO_WAIT_ALL_DEVICE_EVENTS;
    case GM_ROOM_SCENARIO_WAIT_DEVICE_COMMAND_RESULT:
        return ORCH_ROOM_SCENARIO_WAIT_DEVICE_COMMAND_RESULT;
    case GM_ROOM_SCENARIO_WAIT_NONE:
    default:
        return ORCH_ROOM_SCENARIO_WAIT_NONE;
    }
}

const char *orch_room_scenario_wait_type_str(orch_room_scenario_wait_type_t wait_type)
{
    switch (wait_type) {
    case ORCH_ROOM_SCENARIO_WAIT_TIME:
        return "time";
    case ORCH_ROOM_SCENARIO_WAIT_DEVICE_EVENT:
        return "event";
    case ORCH_ROOM_SCENARIO_WAIT_ANY_DEVICE_EVENT:
        return "any_events";
    case ORCH_ROOM_SCENARIO_WAIT_ALL_DEVICE_EVENTS:
        return "all_events";
    case ORCH_ROOM_SCENARIO_WAIT_DEVICE_COMMAND_RESULT:
        return "command_result";
    case ORCH_ROOM_SCENARIO_WAIT_OPERATOR:
        return "operator";
    case ORCH_ROOM_SCENARIO_WAIT_FLAGS:
        return "flags";
    case ORCH_ROOM_SCENARIO_WAIT_NONE:
    default:
        return "none";
    }
}

const char *orch_room_scenario_step_type_str(orch_room_scenario_step_type_t type)
{
    switch (type) {
    case ORCH_ROOM_SCENARIO_STEP_WAIT_TIME:
        return "wait_time";
    case ORCH_ROOM_SCENARIO_STEP_OPERATOR_APPROVAL:
        return "operator_approval";
    case ORCH_ROOM_SCENARIO_STEP_DEVICE_COMMAND:
        return "device_command";
    case ORCH_ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT:
        return "wait_device_event";
    case ORCH_ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP:
        return "device_command_group";
    case ORCH_ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE:
        return "show_operator_message";
    case ORCH_ROOM_SCENARIO_STEP_SET_FLAG:
        return "set_flag";
    case ORCH_ROOM_SCENARIO_STEP_WAIT_FLAGS:
        return "wait_flags";
    case ORCH_ROOM_SCENARIO_STEP_WAIT_ANY_DEVICE_EVENT:
        return "wait_any_device_event";
    case ORCH_ROOM_SCENARIO_STEP_WAIT_ALL_DEVICE_EVENTS:
        return "wait_all_device_events";
    case ORCH_ROOM_SCENARIO_STEP_END_GAME:
        return "end_game";
    default:
        return "operator_approval";
    }
}

const char *orch_room_scenario_trigger_kind_str(room_scenario_reactive_trigger_kind_t kind)
{
    switch (kind) {
    case ROOM_SCENARIO_REACTIVE_TRIGGER_DEVICE_EVENT:
        return "device_event";
    case ROOM_SCENARIO_REACTIVE_TRIGGER_FLAG_CHANGED:
        return "flag_changed";
    case ROOM_SCENARIO_REACTIVE_TRIGGER_OPERATOR_EVENT:
        return "operator_event";
    case ROOM_SCENARIO_REACTIVE_TRIGGER_RUNTIME_EVENT:
        return "runtime_event";
    case ROOM_SCENARIO_REACTIVE_TRIGGER_NONE:
    default:
        return "none";
    }
}

const char *orch_room_scenario_policy_mode_str(room_scenario_reactive_policy_mode_t mode)
{
    switch (mode) {
    case ROOM_SCENARIO_REACTIVE_POLICY_ROTATE:
        return "rotate";
    case ROOM_SCENARIO_REACTIVE_POLICY_RANDOM:
        return "random";
    case ROOM_SCENARIO_REACTIVE_POLICY_ESCALATE:
        return "escalate";
    case ROOM_SCENARIO_REACTIVE_POLICY_SINGLE:
    default:
        return "single";
    }
}

const char *orch_room_scenario_result_action_str(room_scenario_reactive_result_action_t action)
{
    switch (action) {
    case ROOM_SCENARIO_REACTIVE_RESULT_SET_FLAG:
        return "set_flag";
    case ROOM_SCENARIO_REACTIVE_RESULT_FAIL_REACTION:
        return "fail_reaction";
    case ROOM_SCENARIO_REACTIVE_RESULT_FAIL_SCENARIO:
        return "fail_scenario";
    case ROOM_SCENARIO_REACTIVE_RESULT_RETRY:
        return "retry";
    case ROOM_SCENARIO_REACTIVE_RESULT_CONTINUE:
    default:
        return "continue";
    }
}

const char *orch_room_scenario_group_mode_str(room_scenario_command_group_mode_t mode)
{
    return mode == ROOM_SCENARIO_COMMAND_GROUP_PARALLEL ? "parallel" : "sequential";
}

const char *orch_room_scenario_step_state_str(orch_room_scenario_step_runtime_state_t state)
{
    switch (state) {
    case ORCH_ROOM_SCENARIO_STEP_STATE_CURRENT:
        return "current";
    case ORCH_ROOM_SCENARIO_STEP_STATE_WAITING:
        return "waiting";
    case ORCH_ROOM_SCENARIO_STEP_STATE_DONE:
        return "done";
    case ORCH_ROOM_SCENARIO_STEP_STATE_ERROR:
        return "error";
    case ORCH_ROOM_SCENARIO_STEP_STATE_SKIPPED:
        return "skipped";
    case ORCH_ROOM_SCENARIO_STEP_STATE_PENDING:
    default:
        return "pending";
    }
}

const char *orch_room_scenario_validation_level_str(room_scenario_validation_level_t level)
{
    switch (level) {
    case ROOM_SCENARIO_VALIDATION_WARNING:
        return "warning";
    case ROOM_SCENARIO_VALIDATION_ERROR:
    default:
        return "error";
    }
}

const char *orch_issue_scope_str(orch_issue_scope_t scope)
{
    switch (scope) {
    case ORCH_ISSUE_SCOPE_SYSTEM:
        return "system";
    case ORCH_ISSUE_SCOPE_ROOM:
        return "room";
    case ORCH_ISSUE_SCOPE_DEVICE:
    default:
        return "device";
    }
}

const char *orch_issue_severity_str(orch_issue_severity_t severity)
{
    switch (severity) {
    case ORCH_ISSUE_SEVERITY_INFO:
        return "info";
    case ORCH_ISSUE_SEVERITY_WARNING:
        return "warning";
    case ORCH_ISSUE_SEVERITY_ERROR:
    default:
        return "error";
    }
}

orch_health_t orch_health_from_severity(orch_issue_severity_t severity)
{
    switch (severity) {
    case ORCH_ISSUE_SEVERITY_ERROR:
        return ORCH_HEALTH_FAULT;
    case ORCH_ISSUE_SEVERITY_WARNING:
        return ORCH_HEALTH_DEGRADED;
    case ORCH_ISSUE_SEVERITY_INFO:
    default:
        return ORCH_HEALTH_OK;
    }
}

orch_health_t orch_health_from_status_text(const char *health)
{
    if (!health || !health[0]) {
        return ORCH_HEALTH_OK;
    }
    if (strcmp(health, "ok") == 0) {
        return ORCH_HEALTH_OK;
    }
    if (strcmp(health, "warn") == 0 || strcmp(health, "warning") == 0 ||
        strcmp(health, "degraded") == 0) {
        return ORCH_HEALTH_DEGRADED;
    }
    if (strcmp(health, "error") == 0 || strcmp(health, "fault") == 0) {
        return ORCH_HEALTH_FAULT;
    }
    return ORCH_HEALTH_DEGRADED;
}

orch_health_t orch_health_from_diag_level(const char *level)
{
    if (!level || !level[0]) {
        return ORCH_HEALTH_OK;
    }
    if (strcmp(level, "info") == 0 || strcmp(level, "debug") == 0) {
        return ORCH_HEALTH_OK;
    }
    if (strcmp(level, "warn") == 0 || strcmp(level, "warning") == 0) {
        return ORCH_HEALTH_DEGRADED;
    }
    if (strcmp(level, "error") == 0 || strcmp(level, "fatal") == 0) {
        return ORCH_HEALTH_FAULT;
    }
    return ORCH_HEALTH_DEGRADED;
}

void orch_promote_health(orch_device_entry_t *dst, orch_health_t health)
{
    if (!dst || health == ORCH_HEALTH_OK) {
        return;
    }
    if (health == ORCH_HEALTH_FAULT) {
        dst->health = ORCH_HEALTH_FAULT;
        dst->has_fault = true;
        dst->has_degraded = false;
        return;
    }
    if (dst->health != ORCH_HEALTH_FAULT) {
        dst->health = ORCH_HEALTH_DEGRADED;
        dst->has_degraded = true;
    }
}

const char *orch_session_state_str(gm_session_state_t state)
{
    switch (state) {
    case GM_SESSION_RUNNING:
        return "running";
    case GM_SESSION_PAUSED:
        return "paused";
    case GM_SESSION_FINISHED:
        return "finished";
    case GM_SESSION_IDLE:
    default:
        return "idle";
    }
}

const char *orch_timer_state_str(gm_timer_state_t state)
{
    switch (state) {
    case GM_TIMER_RUNNING:
        return "running";
    case GM_TIMER_PAUSED:
        return "paused";
    case GM_TIMER_FINISHED:
        return "finished";
    case GM_TIMER_IDLE:
    default:
        return "idle";
    }
}
