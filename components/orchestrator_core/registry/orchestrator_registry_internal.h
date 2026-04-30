#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "quest_common_utils.h"
#include "device_control_ingest.h"
#include "gm_room_session.h"
#include "orchestrator_registry.h"
#include "room_scenario.h"
#include "room_catalog.h"
#include "service_status.h"

#define ORCH_REGISTRY_CACHE_TTL_MS 250

const char *orch_default_room_id(void);
uint64_t orch_now_ms(void);
void *orch_snapshot_alloc(size_t size);

bool orch_runtime_is_active(orch_runtime_state_t state);
orch_health_t orch_health_from_severity(orch_issue_severity_t severity);
orch_health_t orch_health_from_status_text(const char *health);
orch_health_t orch_health_from_diag_level(const char *level);
void orch_promote_health(orch_device_entry_t *dst, orch_health_t health);
const char *orch_session_state_str(gm_session_state_t state);
const char *orch_timer_state_str(gm_timer_state_t state);

void orch_collect_services(orch_registry_snapshot_t *snapshot);

void orch_device_view_fill_device(const quest_device_t *dev,
                                  bool services_degraded,
                                  orch_device_entry_t *dst);
esp_err_t orch_device_view_get_device(const orch_registry_snapshot_t *snapshot,
                                      const char *device_id,
                                      orch_device_entry_t *out);

orch_room_entry_t *orch_room_view_find_room(orch_registry_snapshot_t *snapshot, const char *room_id);
orch_room_entry_t *orch_room_view_ensure_room(orch_registry_snapshot_t *snapshot, const char *room_id);
void orch_room_view_collect_rooms(orch_registry_snapshot_t *snapshot);
void orch_room_view_enrich_from_sessions(orch_registry_snapshot_t *snapshot);

void orch_room_scenario_view_collect_all(orch_registry_snapshot_t *snapshot);
esp_err_t orch_room_scenario_view_list(const orch_registry_snapshot_t *snapshot,
                                       const char *room_id,
                                       orch_room_scenario_entry_t *out_scenarios,
                                       size_t max_scenarios,
                                       size_t *out_count);
esp_err_t orch_room_scenario_view_list_details(const char *room_id,
                                               orch_room_scenario_detail_t *out_scenarios,
                                               size_t max_scenarios,
                                               size_t *out_count);

void orch_issue_builder_add_issue(orch_registry_snapshot_t *snapshot,
                                  orch_issue_scope_t scope,
                                  orch_issue_severity_t severity,
                                  const char *room_id,
                                  const char *device_id,
                                  const char *code,
                                  const char *title,
                                  const char *details);
void orch_issue_builder_collect_system(orch_registry_snapshot_t *snapshot);
void orch_issue_builder_collect_devices(orch_registry_snapshot_t *snapshot);
void orch_issue_builder_collect_rooms(orch_registry_snapshot_t *snapshot);

esp_err_t orch_snapshot_builder_build_uncached(orch_registry_snapshot_t *out);
