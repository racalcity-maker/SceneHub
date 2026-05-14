#pragma once

#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "orchestrator_audit.h"
#include "orchestrator_registry.h"
#include "orchestrator_timeline.h"

cJSON *orchestrator_api_view_room_scenarios(const char *room_id,
                                            const orch_room_scenario_detail_t *scenarios,
                                            size_t scenario_count);
cJSON *orchestrator_api_view_audit_recent(const orchestrator_audit_entry_t *entries, size_t count);
cJSON *orchestrator_api_view_timeline_recent(const orchestrator_timeline_entry_t *entries, size_t count);
cJSON *orchestrator_api_view_control_devices(const orch_control_device_entry_t *devices,
                                             size_t count,
                                             uint64_t now_ms);
cJSON *orchestrator_api_view_gm_state(const orch_registry_snapshot_t *snapshot);
cJSON *orchestrator_api_view_gm_system_summary(const orch_gm_system_summary_t *summary);
