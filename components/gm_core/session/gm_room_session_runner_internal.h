#pragma once

#include "gm_room_session_commands_internal.h"

esp_err_t gm_room_session_execute_branch_locked(gm_room_session_t *session,
                                                gm_room_scenario_branch_runtime_t *branch,
                                                uint32_t now_ms,
                                                uint8_t budget,
                                                gm_room_session_command_plan_t *out_plan);
