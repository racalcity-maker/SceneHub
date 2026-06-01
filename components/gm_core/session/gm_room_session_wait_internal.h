#pragma once

#include "gm_room_session_commands_internal.h"

typedef struct {
    bool present;
    gm_room_scenario_wait_event_match_t matches[ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS];
    uint8_t match_count;
} gm_room_session_wait_resolution_t;

void scenario_branch_clear_wait_fields(gm_room_scenario_branch_runtime_t *branch);
esp_err_t gm_room_session_expand_prepared_wait_resolution(
    const gm_room_session_prepared_scenario_t *prepared_scenario,
    const gm_room_session_prepared_event_resolution_t *prepared_resolution,
    gm_room_session_wait_resolution_t *out);
esp_err_t scenario_enter_wait_device_event_locked(gm_room_session_t *session,
                                                  const room_scenario_wait_device_event_t *wait,
                                                  const gm_room_session_wait_resolution_t *resolution,
                                                  uint32_t now_ms);
esp_err_t scenario_enter_wait_any_device_event_locked(
    gm_room_session_t *session,
    const room_scenario_wait_any_device_event_t *wait_any,
    const gm_room_session_wait_resolution_t *resolution,
    uint32_t now_ms);
esp_err_t scenario_enter_wait_all_device_events_locked(
    gm_room_session_t *session,
    const room_scenario_wait_all_device_events_t *wait_all,
    const gm_room_session_wait_resolution_t *resolution,
    uint32_t now_ms);
esp_err_t scenario_enter_wait_flags_locked(gm_room_session_t *session,
                                           const room_scenario_wait_flags_t *wait_flags,
                                           uint32_t now_ms);
void scenario_enter_wait_command_result_locked(
    gm_room_session_t *session,
    const gm_room_session_command_dispatch_result_t *dispatch,
    uint32_t now_ms);
