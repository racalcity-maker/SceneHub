#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "gm_game_profile.h"
#include "quest_common_limits.h"
#include "quest_device.h"
#include "room_catalog.h"
#include "room_scenario.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ORCH_REGISTRY_STATE_MAX_LEN
#define ORCH_REGISTRY_STATE_MAX_LEN 16
#endif
#ifndef ORCH_REGISTRY_MAX_DEVICES
#define ORCH_REGISTRY_MAX_DEVICES (QUEST_DEVICE_MAX_DEVICES + 4)
#endif
#ifndef ORCH_REGISTRY_MAX_ROOMS
#define ORCH_REGISTRY_MAX_ROOMS ROOM_CATALOG_MAX_ROOMS
#endif
#ifndef ORCH_REGISTRY_MAX_ISSUES
#define ORCH_REGISTRY_MAX_ISSUES (ORCH_REGISTRY_MAX_DEVICES + ORCH_REGISTRY_MAX_ROOMS + 8)
#endif
#ifndef ORCH_REGISTRY_ISSUE_ID_MAX_LEN
#define ORCH_REGISTRY_ISSUE_ID_MAX_LEN 96
#endif
#ifndef ORCH_ROOM_SCENARIO_ID_MAX_LEN
#define ORCH_ROOM_SCENARIO_ID_MAX_LEN 32
#endif
#ifndef ORCH_ROOM_SCENARIO_NAME_MAX_LEN
#define ORCH_ROOM_SCENARIO_NAME_MAX_LEN 64
#endif
#ifndef ORCH_ROOM_SCENARIO_DEVICE_ID_MAX_LEN
#define ORCH_ROOM_SCENARIO_DEVICE_ID_MAX_LEN 32
#endif
#ifndef ORCH_ROOM_SCENARIO_EVENT_TYPE_MAX_LEN
#define ORCH_ROOM_SCENARIO_EVENT_TYPE_MAX_LEN 32
#endif
#ifndef ORCH_ROOM_SCENARIO_EVENT_SOURCE_ID_MAX_LEN
#define ORCH_ROOM_SCENARIO_EVENT_SOURCE_ID_MAX_LEN 32
#endif
#ifndef ORCH_ROOM_SCENARIO_OPERATOR_PROMPT_MAX_LEN
#define ORCH_ROOM_SCENARIO_OPERATOR_PROMPT_MAX_LEN 96
#endif
#ifndef ORCH_ROOM_SCENARIO_OPERATOR_LABEL_MAX_LEN
#define ORCH_ROOM_SCENARIO_OPERATOR_LABEL_MAX_LEN 32
#endif
#ifndef ORCH_ROOM_SCENARIO_STEP_TEXT_MAX_LEN
#define ORCH_ROOM_SCENARIO_STEP_TEXT_MAX_LEN 96
#endif
#ifndef ORCH_ROOM_SCENARIO_WAIT_SUMMARY_MAX_LEN
#define ORCH_ROOM_SCENARIO_WAIT_SUMMARY_MAX_LEN 96
#endif
#ifndef ORCH_ROOM_SCENARIO_MAX_EVENT_REFS
#define ORCH_ROOM_SCENARIO_MAX_EVENT_REFS ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS
#endif
#ifndef ORCH_ROOM_SCENARIO_MAX_FLAG_REFS
#define ORCH_ROOM_SCENARIO_MAX_FLAG_REFS ROOM_SCENARIO_WAIT_FLAGS_MAX_FLAGS
#endif
#ifndef ORCH_ROOM_SCENARIO_MAX_DEVICE_REFS
#define ORCH_ROOM_SCENARIO_MAX_DEVICE_REFS ORCH_REGISTRY_MAX_DEVICES
#endif
#ifndef ORCH_ROOM_SCENARIO_MAX_FLAGS
#define ORCH_ROOM_SCENARIO_MAX_FLAGS 16
#endif
#ifndef ORCH_ROOM_SCENARIO_MAX_BRANCHES
#define ORCH_ROOM_SCENARIO_MAX_BRANCHES ROOM_SCENARIO_MAX_BRANCHES
#endif

typedef enum {
    ORCH_ROOM_SCENARIO_RUNTIME_IDLE = 0,
    ORCH_ROOM_SCENARIO_RUNTIME_RUNNING,
    ORCH_ROOM_SCENARIO_RUNTIME_WAITING,
    ORCH_ROOM_SCENARIO_RUNTIME_PAUSED,
    ORCH_ROOM_SCENARIO_RUNTIME_DONE,
    ORCH_ROOM_SCENARIO_RUNTIME_STOPPED,
    ORCH_ROOM_SCENARIO_RUNTIME_COOLDOWN,
    ORCH_ROOM_SCENARIO_RUNTIME_ERROR,
} orch_room_scenario_runtime_state_t;

typedef enum {
    ORCH_ROOM_SCENARIO_STEP_STATE_PENDING = 0,
    ORCH_ROOM_SCENARIO_STEP_STATE_CURRENT,
    ORCH_ROOM_SCENARIO_STEP_STATE_WAITING,
    ORCH_ROOM_SCENARIO_STEP_STATE_DONE,
    ORCH_ROOM_SCENARIO_STEP_STATE_ERROR,
    ORCH_ROOM_SCENARIO_STEP_STATE_SKIPPED,
} orch_room_scenario_step_runtime_state_t;

typedef enum {
    ORCH_ROOM_SCENARIO_WAIT_NONE = 0,
    ORCH_ROOM_SCENARIO_WAIT_TIME,
    ORCH_ROOM_SCENARIO_WAIT_DEVICE_EVENT,
    ORCH_ROOM_SCENARIO_WAIT_ANY_DEVICE_EVENT,
    ORCH_ROOM_SCENARIO_WAIT_OPERATOR,
    ORCH_ROOM_SCENARIO_WAIT_FLAGS,
    ORCH_ROOM_SCENARIO_WAIT_ALL_DEVICE_EVENTS,
    ORCH_ROOM_SCENARIO_WAIT_DEVICE_COMMAND_RESULT,
} orch_room_scenario_wait_type_t;

typedef struct {
    char event_type[ORCH_ROOM_SCENARIO_EVENT_TYPE_MAX_LEN];
    char source_id[ORCH_ROOM_SCENARIO_EVENT_SOURCE_ID_MAX_LEN];
} orch_room_wait_event_entry_t;

typedef struct {
    char name[ROOM_SCENARIO_FLAG_NAME_MAX_LEN];
    bool value;
} orch_room_scenario_flag_ref_t;

typedef struct {
    char name[ROOM_SCENARIO_FLAG_NAME_MAX_LEN];
    bool value;
} orch_room_scenario_flag_entry_t;

typedef struct {
    char id[ROOM_SCENARIO_BRANCH_ID_MAX_LEN];
    char name[ROOM_SCENARIO_BRANCH_NAME_MAX_LEN];
    bool active;
    room_scenario_branch_type_t type;
    char type_text[ORCH_REGISTRY_STATE_MAX_LEN];
    bool required_for_completion;
    uint16_t priority;
    uint32_t cooldown_ms;
    uint32_t cooldown_until_ms;
    uint32_t max_fire_count;
    uint32_t fire_count;
    bool run_once;
    bool fired_once;
    room_scenario_reentry_mode_t reentry_mode;
    char reentry_mode_text[ORCH_REGISTRY_STATE_MAX_LEN];
    bool pending_trigger;
    uint16_t step_start_index;
    uint16_t step_count;
    uint16_t current_step_index;
    uint16_t current_local_step_index;
    uint16_t done_steps;
    uint16_t total_steps;
    int16_t failed_step_index;
    orch_room_scenario_step_runtime_state_t current_step_state;
    char current_step_state_text[ORCH_REGISTRY_STATE_MAX_LEN];
    orch_room_scenario_runtime_state_t state;
    char state_text[ORCH_REGISTRY_STATE_MAX_LEN];
    orch_room_scenario_wait_type_t wait_type;
    char wait_type_text[ORCH_REGISTRY_STATE_MAX_LEN];
    char current_step_text[ORCH_ROOM_SCENARIO_STEP_TEXT_MAX_LEN];
    char wait_summary[ORCH_ROOM_SCENARIO_WAIT_SUMMARY_MAX_LEN];
    uint32_t wait_until_ms;
    uint32_t wait_started_at_ms;
    bool wait_operator_skip_allowed;
    char wait_operator_skip_label[ORCH_ROOM_SCENARIO_OPERATOR_LABEL_MAX_LEN];
} orch_room_scenario_branch_entry_t;

typedef struct {
    char room_id[QUEST_ROOM_ID_MAX_LEN];
    bool session_present;
    char session_state[ORCH_REGISTRY_STATE_MAX_LEN];
    char timer_state[ORCH_REGISTRY_STATE_MAX_LEN];
    uint32_t timer_duration_ms;
    uint32_t timer_remaining_ms;
    bool hint_active;
    uint32_t hint_sent_count;
    char hint_message[QUEST_PAYLOAD_MAX_LEN];
    char selected_profile_id[GM_GAME_PROFILE_ID_MAX_LEN];
    char selected_profile_name[GM_GAME_PROFILE_NAME_MAX_LEN];
    char selected_profile_scenario_id[GM_GAME_PROFILE_SCENARIO_ID_MAX_LEN];
    char selected_scenario_id[ORCH_ROOM_SCENARIO_ID_MAX_LEN];
    char selected_scenario_name[ORCH_ROOM_SCENARIO_NAME_MAX_LEN];
    char running_scenario_id[ORCH_ROOM_SCENARIO_ID_MAX_LEN];
    char running_scenario_name[ORCH_ROOM_SCENARIO_NAME_MAX_LEN];
    uint32_t running_scenario_generation;
    uint64_t runtime_now_ms;
    char scenario_runtime_state_text[ORCH_REGISTRY_STATE_MAX_LEN];
    uint16_t scenario_total_steps;
    uint16_t scenario_done_steps;
    char scenario_current_step_text[ORCH_ROOM_SCENARIO_STEP_TEXT_MAX_LEN];
    char scenario_wait_type_text[ORCH_REGISTRY_STATE_MAX_LEN];
    char scenario_wait_summary[ORCH_ROOM_SCENARIO_WAIT_SUMMARY_MAX_LEN];
    uint32_t scenario_wait_until_ms;
    uint32_t scenario_wait_started_at_ms;
    char scenario_wait_operator_prompt[ORCH_ROOM_SCENARIO_OPERATOR_PROMPT_MAX_LEN];
    char scenario_wait_operator_label[ORCH_ROOM_SCENARIO_OPERATOR_LABEL_MAX_LEN];
    bool scenario_wait_operator_skip_allowed;
    char scenario_wait_operator_skip_label[ORCH_ROOM_SCENARIO_OPERATOR_LABEL_MAX_LEN];
    char scenario_operator_message[ROOM_SCENARIO_OPERATOR_MESSAGE_MAX_LEN];
    uint8_t scenario_device_count;
    char scenario_last_error[96];
} orch_room_runtime_summary_view_t;

/* Lightweight read-model DTO for live room runtime refresh paths. */

typedef struct {
    orch_room_runtime_summary_view_t summary;
    orch_room_wait_event_entry_t scenario_wait_events[ORCH_ROOM_SCENARIO_MAX_EVENT_REFS];
    uint8_t scenario_wait_event_count;
    orch_room_scenario_flag_ref_t scenario_wait_flags[ORCH_ROOM_SCENARIO_MAX_FLAG_REFS];
    uint8_t scenario_wait_flag_count;
    char scenario_device_ids[ORCH_ROOM_SCENARIO_MAX_DEVICE_REFS][ORCH_ROOM_SCENARIO_DEVICE_ID_MAX_LEN];
    char related_issue_ids[ORCH_REGISTRY_MAX_ISSUES][ORCH_REGISTRY_ISSUE_ID_MAX_LEN];
    uint8_t related_issue_count;
    orch_room_scenario_flag_entry_t scenario_flags[ORCH_ROOM_SCENARIO_MAX_FLAGS];
    uint8_t scenario_flag_count;
    orch_room_scenario_branch_entry_t scenario_branches[ORCH_ROOM_SCENARIO_MAX_BRANCHES];
    uint8_t scenario_branch_count;
    char asset_prepare_state[16];
    uint16_t asset_audio_total;
    uint16_t asset_audio_ready;
    uint16_t asset_audio_missing;
    uint16_t asset_audio_bad;
    uint16_t asset_audio_unsupported;
    uint16_t asset_audio_io_error;
    uint16_t asset_audio_unknown;
} orch_room_runtime_detail_view_t;

/*
 * Wider room-runtime read-model DTO for detail views. Keep it off frequent
 * polling paths unless the caller explicitly needs branch/flag/asset detail.
 */

esp_err_t orchestrator_registry_get_room_runtime_detail_view(const char *room_id,
                                                             bool include_assets,
                                                             orch_room_runtime_detail_view_t *out);
esp_err_t orchestrator_registry_get_room_runtime_summary_view(const char *room_id,
                                                              orch_room_runtime_summary_view_t *out);

#ifdef __cplusplus
}
#endif
