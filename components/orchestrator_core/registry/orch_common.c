#include "orchestrator_registry_internal.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"

const char *orch_default_room_id(void)
{
    return "unassigned";
}

uint64_t orch_now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

void *orch_snapshot_alloc(size_t size)
{
    void *ptr = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
        ptr = heap_caps_calloc(1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return ptr;
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
