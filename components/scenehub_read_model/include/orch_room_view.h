#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "gm_game_profile.h"
#include "orch_device_view.h"
#include "orch_issue_view.h"
#include "orch_runtime_view.h"
#include "orch_scenario_view.h"
#include "quest_common_limits.h"
#include "room_catalog.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char room_id[QUEST_ROOM_ID_MAX_LEN];
    char title[QUEST_NAME_MAX_LEN];
    char session_state[ORCH_REGISTRY_STATE_MAX_LEN];
    char timer_state[ORCH_REGISTRY_STATE_MAX_LEN];
    char hint_message[QUEST_PAYLOAD_MAX_LEN];
    uint16_t sort_order;
    uint8_t device_count;
    uint8_t active_device_count;
    uint8_t issue_count;
    uint32_t timer_duration_ms;
    uint32_t timer_remaining_ms;
    uint32_t hint_sent_count;
    uint32_t selected_scenario_generation;
    uint32_t selected_profile_duration_ms;
    uint64_t session_started_at_ms;
    char selected_profile_id[GM_GAME_PROFILE_ID_MAX_LEN];
    char selected_profile_name[GM_GAME_PROFILE_NAME_MAX_LEN];
    char selected_profile_scenario_id[GM_GAME_PROFILE_SCENARIO_ID_MAX_LEN];
    char selected_scenario_id[ORCH_ROOM_SCENARIO_ID_MAX_LEN];
    char selected_scenario_name[ORCH_ROOM_SCENARIO_NAME_MAX_LEN];
    char running_scenario_id[ORCH_ROOM_SCENARIO_ID_MAX_LEN];
    char running_scenario_name[ORCH_ROOM_SCENARIO_NAME_MAX_LEN];
    uint32_t running_scenario_generation;
    orch_room_scenario_runtime_state_t scenario_runtime_state;
    char scenario_runtime_state_text[ORCH_REGISTRY_STATE_MAX_LEN];
    uint16_t scenario_current_step_index;
    uint16_t scenario_total_steps;
    uint16_t scenario_done_steps;
    char scenario_current_step_text[ORCH_ROOM_SCENARIO_STEP_TEXT_MAX_LEN];
    orch_room_scenario_wait_type_t scenario_wait_type;
    char scenario_wait_type_text[ORCH_REGISTRY_STATE_MAX_LEN];
    char scenario_wait_summary[ORCH_ROOM_SCENARIO_WAIT_SUMMARY_MAX_LEN];
    uint32_t scenario_wait_until_ms;
    uint32_t scenario_wait_started_at_ms;
    char scenario_wait_event_type[ORCH_ROOM_SCENARIO_EVENT_TYPE_MAX_LEN];
    char scenario_wait_source_id[ORCH_ROOM_SCENARIO_EVENT_SOURCE_ID_MAX_LEN];
    orch_room_wait_event_entry_t scenario_wait_events[ORCH_ROOM_SCENARIO_MAX_EVENT_REFS];
    uint8_t scenario_wait_event_count;
    orch_room_scenario_flag_ref_t scenario_wait_flags[ORCH_ROOM_SCENARIO_MAX_FLAG_REFS];
    uint8_t scenario_wait_flag_count;
    char scenario_wait_operator_prompt[ORCH_ROOM_SCENARIO_OPERATOR_PROMPT_MAX_LEN];
    char scenario_wait_operator_label[ORCH_ROOM_SCENARIO_OPERATOR_LABEL_MAX_LEN];
    bool scenario_wait_operator_skip_allowed;
    char scenario_wait_operator_skip_label[ORCH_ROOM_SCENARIO_OPERATOR_LABEL_MAX_LEN];
    char scenario_operator_message[ROOM_SCENARIO_OPERATOR_MESSAGE_MAX_LEN];
    char scenario_device_ids[ORCH_ROOM_SCENARIO_MAX_DEVICE_REFS][ORCH_ROOM_SCENARIO_DEVICE_ID_MAX_LEN];
    uint8_t scenario_device_count;
    char related_issue_ids[ORCH_REGISTRY_MAX_ISSUES][ORCH_REGISTRY_ISSUE_ID_MAX_LEN];
    uint8_t related_issue_count;
    orch_room_scenario_flag_entry_t scenario_flags[ORCH_ROOM_SCENARIO_MAX_FLAGS];
    uint8_t scenario_flag_count;
    orch_room_scenario_branch_entry_t scenario_branches[ORCH_ROOM_SCENARIO_MAX_BRANCHES];
    uint8_t scenario_branch_count;
    char scenario_last_error[96];
    orch_health_t health;
    char health_text[ORCH_REGISTRY_STATE_MAX_LEN];
    bool session_present;
    bool hint_active;
} orch_room_entry_t;

esp_err_t orchestrator_registry_list_rooms(orch_room_entry_t *out_rooms,
                                           size_t max_rooms,
                                           size_t *out_count);
esp_err_t orchestrator_registry_get_room(const char *room_id, orch_room_entry_t *out);

#ifdef __cplusplus
}
#endif
