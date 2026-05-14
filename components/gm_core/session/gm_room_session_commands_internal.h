#pragma once

#include "gm_room_session_internal.h"

typedef struct {
    bool result_required;
    uint32_t timeout_ms;
    char request_id[48];
    char source_id[ROOM_SCENARIO_EVENT_SOURCE_ID_MAX_LEN];
    char command[48];
} gm_room_session_command_dispatch_t;

typedef enum {
    GM_ROOM_SESSION_COMMAND_PLAN_NONE = 0,
    GM_ROOM_SESSION_COMMAND_PLAN_SCENARIO_STEP,
    GM_ROOM_SESSION_COMMAND_PLAN_SCENARIO_STEP_GROUP,
    GM_ROOM_SESSION_COMMAND_PLAN_REACTIVE_ACTION,
    GM_ROOM_SESSION_COMMAND_PLAN_REACTIVE_ACTION_GROUP,
} gm_room_session_command_plan_kind_t;

typedef struct {
    char room_id[QUEST_ROOM_ID_MAX_LEN];
    uint8_t branch_index;
    gm_room_session_command_plan_kind_t kind;
    uint16_t expected_step_index;
    uint8_t expected_reactive_action;
    uint8_t command_index;
} gm_room_session_command_plan_t;

esp_err_t gm_room_session_execute_quest_device_command_internal(
    const room_scenario_device_command_t *step_command,
    char *error,
    size_t error_size,
    gm_room_session_command_dispatch_t *out_dispatch);
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
esp_err_t gm_room_session_dispatch_planned_command(gm_room_session_command_plan_t *plan);
void gm_room_session_stop_audio(void);
