#pragma once

#include "gm_room_session_internal.h"

bool gm_room_session_command_plan_present(const gm_room_session_command_plan_t *plan);
void gm_room_session_command_plan_clear(gm_room_session_command_plan_t *plan);
esp_err_t gm_room_session_plan_scenario_command_locked(
    gm_room_session_t *session,
    uint8_t branch_index,
    const room_scenario_device_command_t *command,
    uint32_t now_ms,
    gm_room_session_command_plan_t *out_plan);
esp_err_t gm_room_session_plan_scenario_command_group_locked(
    gm_room_session_t *session,
    uint8_t branch_index,
    const room_scenario_device_command_group_t *group,
    uint32_t now_ms,
    char *error,
    size_t error_size,
    gm_room_session_command_plan_t *out_plan);
esp_err_t gm_room_session_plan_reactive_action_command_locked(
    gm_room_session_t *session,
    gm_room_scenario_branch_runtime_t *branch,
    const room_scenario_reactive_action_t *action,
    uint32_t now_ms,
    char *error,
    size_t error_size,
    gm_room_session_command_plan_t *out_plan);
void gm_room_session_stop_audio(void);
void gm_room_session_handle_command_result_event(const scenehub_event_t *message);
size_t gm_room_session_poll_command_timeouts(scenehub_event_t *out_events, size_t max_events);
uint64_t gm_room_session_next_command_timeout_deadline_ms(void);
void gm_room_session_cancel_command_request(const char *request_id);
void gm_room_session_reset_pending_commands(void);
