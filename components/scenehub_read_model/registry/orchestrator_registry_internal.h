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

typedef struct {
    uint16_t total;
    uint16_t ready;
    uint16_t missing;
    uint16_t bad;
    uint16_t unsupported;
    uint16_t io_error;
    uint16_t unknown;
} orch_room_asset_summary_t;

const char *orch_default_room_id(void);
uint64_t orch_now_ms(void);
esp_err_t orch_scratch_lock(void);
void orch_scratch_unlock(void);
esp_err_t orch_cache_lock(void);
void orch_cache_unlock(void);
quest_device_t *orch_scratch_devices(size_t *out_capacity);
device_control_ingest_device_t *orch_scratch_ingest(void);
device_control_ingest_device_t *orch_scratch_ingest_devices(size_t *out_capacity);
gm_room_session_t *orch_scratch_session(void);
room_scenario_t *orch_scratch_room_scenario(void);
room_scenario_validation_report_t *orch_scratch_validation_report(void);

bool orch_runtime_is_active(orch_runtime_state_t state);
const char *orch_connectivity_str(orch_connectivity_t connectivity);
const char *orch_health_str(orch_health_t health);
const char *orch_runtime_state_str(orch_runtime_state_t state);
orch_room_scenario_runtime_state_t orch_runtime_state_from_gm(gm_room_scenario_state_t state);
const char *orch_room_scenario_runtime_state_str(orch_room_scenario_runtime_state_t state);
orch_room_scenario_wait_type_t orch_wait_type_from_gm(gm_room_scenario_wait_type_t wait_type);
const char *orch_room_scenario_wait_type_str(orch_room_scenario_wait_type_t wait_type);
const char *orch_room_scenario_step_type_str(orch_room_scenario_step_type_t type);
const char *orch_room_scenario_trigger_kind_str(room_scenario_reactive_trigger_kind_t kind);
const char *orch_room_scenario_policy_mode_str(room_scenario_reactive_policy_mode_t mode);
const char *orch_room_scenario_result_action_str(room_scenario_reactive_result_action_t action);
const char *orch_room_scenario_group_mode_str(room_scenario_command_group_mode_t mode);
const char *orch_room_scenario_step_state_str(orch_room_scenario_step_runtime_state_t state);
const char *orch_room_scenario_validation_level_str(room_scenario_validation_level_t level);
const char *orch_issue_scope_str(orch_issue_scope_t scope);
const char *orch_issue_severity_str(orch_issue_severity_t severity);
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
void orch_device_view_fill_control_device(const device_control_ingest_device_t *ingest,
                                          orch_control_device_entry_t *dst);
esp_err_t orch_device_view_get_device(const orch_registry_snapshot_t *snapshot,
                                      const char *device_id,
                                      orch_device_entry_t *out);
esp_err_t orch_device_view_list_control_devices(orch_control_device_entry_t *out_devices,
                                                size_t max_devices,
                                                size_t *out_count);

orch_room_entry_t *orch_room_view_find_room(orch_registry_snapshot_t *snapshot, const char *room_id);
orch_room_entry_t *orch_room_view_ensure_room(orch_registry_snapshot_t *snapshot, const char *room_id);
bool orch_room_view_has_scenario_device(const orch_room_entry_t *room, const char *device_id);
void orch_room_view_collect_rooms(orch_registry_snapshot_t *snapshot);
void orch_room_view_enrich_from_sessions(orch_registry_snapshot_t *snapshot);
esp_err_t orch_room_view_load_runtime_room(const char *room_id, orch_room_entry_t *out);
esp_err_t orch_room_view_load_runtime_room_with_session(const char *room_id,
                                                        const gm_room_session_t *session,
                                                        orch_room_entry_t *out);
bool orch_room_runtime_assets_collect(const char *scenario_id,
                                      const gm_room_session_t *session,
                                      room_scenario_t *scratch_scenario,
                                      orch_room_asset_summary_t *out_summary);
uint32_t orch_room_runtime_assets_generation(void);
void orch_room_runtime_assets_apply_summary(orch_room_runtime_view_t *out,
                                            const orch_room_asset_summary_t *summary);
bool orch_room_runtime_assets_load_cached(orch_room_runtime_view_t *out,
                                          uint32_t scenario_generation,
                                          uint32_t device_generation,
                                          uint32_t asset_generation);
void orch_room_runtime_assets_store_cached(const orch_room_runtime_view_t *out,
                                           uint32_t scenario_generation,
                                           uint32_t device_generation,
                                           uint32_t asset_generation,
                                           const orch_room_asset_summary_t *summary);

void orch_room_scenario_view_collect_all(orch_registry_snapshot_t *snapshot);
esp_err_t orch_room_scenario_view_list(const orch_registry_snapshot_t *snapshot,
                                       const char *room_id,
                                       orch_room_scenario_entry_t *out_scenarios,
                                       size_t max_scenarios,
                                       size_t *out_count);
esp_err_t orch_room_profile_view_list(const char *room_id,
                                      orch_room_profile_entry_t *out_profiles,
                                      size_t max_profiles,
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
