#include "orchestrator_registry_internal.h"

#include <string.h>

#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static EXT_RAM_BSS_ATTR quest_device_t s_scratch_devices[ORCH_REGISTRY_MAX_DEVICES];
static EXT_RAM_BSS_ATTR device_control_ingest_device_t s_scratch_ingest;
static EXT_RAM_BSS_ATTR gm_room_session_t s_scratch_session;
static EXT_RAM_BSS_ATTR room_scenario_t s_scratch_room_scenario;
static EXT_RAM_BSS_ATTR room_scenario_validation_report_t s_scratch_validation_report;
static SemaphoreHandle_t s_scratch_mutex = NULL;
static StaticSemaphore_t s_scratch_mutex_storage;

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
        s_scratch_mutex = xSemaphoreCreateMutexStatic(&s_scratch_mutex_storage);
    }
    if (!s_scratch_mutex) {
        return ESP_ERR_NO_MEM;
    }
    return xSemaphoreTake(s_scratch_mutex, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

void orch_scratch_unlock(void)
{
    if (s_scratch_mutex) {
        xSemaphoreGive(s_scratch_mutex);
    }
}

quest_device_t *orch_scratch_devices(size_t *out_capacity)
{
    if (out_capacity) {
        *out_capacity = ORCH_REGISTRY_MAX_DEVICES;
    }
    memset(s_scratch_devices, 0, sizeof(s_scratch_devices));
    return s_scratch_devices;
}

device_control_ingest_device_t *orch_scratch_ingest(void)
{
    memset(&s_scratch_ingest, 0, sizeof(s_scratch_ingest));
    return &s_scratch_ingest;
}

gm_room_session_t *orch_scratch_session(void)
{
    memset(&s_scratch_session, 0, sizeof(s_scratch_session));
    return &s_scratch_session;
}

room_scenario_t *orch_scratch_room_scenario(void)
{
    memset(&s_scratch_room_scenario, 0, sizeof(s_scratch_room_scenario));
    return &s_scratch_room_scenario;
}

room_scenario_validation_report_t *orch_scratch_validation_report(void)
{
    memset(&s_scratch_validation_report, 0, sizeof(s_scratch_validation_report));
    return &s_scratch_validation_report;
}

bool orch_runtime_is_active(orch_runtime_state_t state)
{
    return state == ORCH_RUNTIME_STATE_ARMED ||
           state == ORCH_RUNTIME_STATE_ACTIVE ||
           state == ORCH_RUNTIME_STATE_PAUSED;
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
