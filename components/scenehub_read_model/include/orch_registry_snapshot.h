#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "orch_device_view.h"
#include "orch_issue_view.h"
#include "orch_room_view.h"
#include "orch_scenario_view.h"
#include "quest_common_limits.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ORCH_REGISTRY_SERVICE_ID_MAX_LEN
#define ORCH_REGISTRY_SERVICE_ID_MAX_LEN 16
#endif

typedef struct {
    char service_id[ORCH_REGISTRY_SERVICE_ID_MAX_LEN];
    bool init_attempted;
    bool init_ok;
    bool start_attempted;
    bool start_ok;
    bool fault;
    esp_err_t last_error;
    orch_health_t health;
} orch_service_entry_t;

typedef struct {
    uint32_t generation;
    uint32_t cache_version;
    uint64_t snapshot_built_at_ms;
    char active_profile[QUEST_ID_MAX_LEN];
    uint8_t service_count;
    uint8_t room_count;
    uint8_t device_count;
    uint8_t issue_count;
    size_t room_scenario_count;
    uint16_t active_session_count;
    uint16_t active_hint_count;
    bool has_fault;
    bool has_degraded;
    orch_service_entry_t services[8];
    orch_room_entry_t rooms[ORCH_REGISTRY_MAX_ROOMS];
    orch_device_entry_t devices[ORCH_REGISTRY_MAX_DEVICES];
    orch_issue_entry_t issues[ORCH_REGISTRY_MAX_ISSUES];
    orch_room_scenario_entry_t room_scenarios[ORCH_REGISTRY_MAX_ROOM_SCENARIOS];
} orch_registry_snapshot_t;

/*
 * Aggregate read-model snapshot. Prefer narrower family APIs for new endpoints
 * instead of growing this structure as a catch-all DTO.
 */

typedef struct {
    uint32_t generation;
    uint8_t room_count;
    uint8_t device_count;
    uint8_t online_device_count;
    uint16_t issue_count;
    uint16_t active_session_count;
    uint16_t active_hint_count;
    uint16_t degraded_count;
    uint16_t fault_count;
    uint32_t dropped_critical_events;
    uint32_t dropped_noncritical_events;
    uint32_t dropped_event_queue_events;
    uint32_t dropped_runtime_queue_events;
    bool has_fault;
    bool has_degraded;
} orch_gm_system_summary_t;

esp_err_t orchestrator_registry_init(void);
esp_err_t orchestrator_registry_build_snapshot(orch_registry_snapshot_t *out);
esp_err_t orchestrator_registry_get_system_summary(orch_gm_system_summary_t *out);
void orchestrator_registry_invalidate(void);

#ifdef __cplusplus
}
#endif
