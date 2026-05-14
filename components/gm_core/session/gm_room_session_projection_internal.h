#pragma once

#include "gm_room_session_internal.h"

uint16_t scenario_branch_end_index(const gm_room_scenario_branch_runtime_t *branch,
                                   const room_scenario_t *scenario);
void gm_room_session_scenario_branch_load_into_session(
    gm_room_session_t *session,
    const gm_room_scenario_branch_runtime_t *branch);
void gm_room_session_scenario_branch_save_from_session(
    gm_room_scenario_branch_runtime_t *branch,
    const gm_room_session_t *session);
void gm_room_session_scenario_update_summary_from_branches_locked(gm_room_session_t *session);
