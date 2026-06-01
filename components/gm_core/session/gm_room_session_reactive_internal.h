#pragma once

#include "gm_room_session_commands_internal.h"

bool gm_room_session_branch_is_reactive_v2(const gm_room_session_t *session,
                                           const gm_room_scenario_branch_runtime_t *branch);
bool gm_room_session_reactive_v2_matches_event(const gm_room_session_t *session,
                                               const gm_room_scenario_branch_runtime_t *branch,
                                               const scenehub_event_t *message);
bool gm_room_session_reactive_v2_consume_trigger_event_locked(
    gm_room_session_t *session,
    gm_room_scenario_branch_runtime_t *branch,
    const scenehub_event_t *message);
esp_err_t gm_room_session_reactive_v2_fire_locked(gm_room_session_t *session,
                                                  gm_room_scenario_branch_runtime_t *branch,
                                                  uint32_t now_ms,
                                                  gm_room_session_command_plan_t *out_plan);
esp_err_t gm_room_session_reactive_v2_continue_locked(gm_room_session_t *session,
                                                      gm_room_scenario_branch_runtime_t *branch,
                                                      uint32_t now_ms,
                                                      gm_room_session_command_plan_t *out_plan);
esp_err_t gm_room_session_reactive_v2_handle_result_success_locked(
    gm_room_session_t *session,
    gm_room_scenario_branch_runtime_t *branch,
    uint32_t now_ms,
    gm_room_session_command_plan_t *out_plan);
esp_err_t gm_room_session_reactive_v2_handle_result_failure_locked(
    gm_room_session_t *session,
    gm_room_scenario_branch_runtime_t *branch,
    const char *status,
    uint32_t now_ms,
    gm_room_session_command_plan_t *out_plan);
esp_err_t gm_room_session_reactive_v2_apply_wait_timeout_locked(
    gm_room_session_t *session,
    gm_room_scenario_branch_runtime_t *branch,
    const room_scenario_reactive_action_t *action,
    uint32_t now_ms,
    gm_room_session_command_plan_t *out_plan);
esp_err_t gm_room_session_reactive_v2_trigger_during_wait_locked(
    gm_room_session_t *session,
    gm_room_scenario_branch_runtime_t *branch,
    uint32_t now_ms,
    gm_room_session_command_plan_t *out_plan);
