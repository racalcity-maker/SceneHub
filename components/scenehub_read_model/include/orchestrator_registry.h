#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "device_control_ingest.h"
#include "gm_game_profile.h"
#include "quest_common_limits.h"
#include "quest_device.h"
#include "room_catalog.h"
#include "room_scenario.h"

#define ORCH_REGISTRY_SERVICE_ID_MAX_LEN   16
#define ORCH_REGISTRY_STATE_MAX_LEN        16
#define ORCH_REGISTRY_ISSUE_ID_MAX_LEN     96
#define ORCH_REGISTRY_ISSUE_CODE_MAX_LEN   32
#define ORCH_REGISTRY_ISSUE_TITLE_MAX_LEN  64
#define ORCH_REGISTRY_ISSUE_DETAILS_MAX_LEN 160
#define ORCH_REGISTRY_DEVICE_BADGE_MAX_LEN 16
#define ORCH_REGISTRY_DEVICE_MAX_BADGES    2
#define ORCH_REGISTRY_MAX_ROOMS            ROOM_CATALOG_MAX_ROOMS
#define ORCH_REGISTRY_MAX_DEVICES          QUEST_DEVICE_MAX_DEVICES
#define ORCH_REGISTRY_MAX_ISSUES           (ORCH_REGISTRY_MAX_DEVICES + ORCH_REGISTRY_MAX_ROOMS + 8)
#define ORCH_REGISTRY_MAX_ROOM_SCENARIOS   ROOM_SCENARIO_MAX_SCENARIOS
#define ORCH_ROOM_SCENARIO_ID_MAX_LEN      32
#define ORCH_ROOM_SCENARIO_NAME_MAX_LEN    64
#define ORCH_ROOM_SCENARIO_STEP_ID_MAX_LEN 32
#define ORCH_ROOM_SCENARIO_STEP_LABEL_MAX_LEN 64
#define ORCH_ROOM_SCENARIO_DEVICE_ID_MAX_LEN 32
#define ORCH_ROOM_SCENARIO_DEVICE_COMMAND_ID_MAX_LEN 32
#define ORCH_ROOM_SCENARIO_DEVICE_EVENT_ID_MAX_LEN 32
#define ORCH_ROOM_SCENARIO_COMMAND_PARAMS_JSON_MAX_LEN 384
#define ORCH_ROOM_SCENARIO_EVENT_TYPE_MAX_LEN 32
#define ORCH_ROOM_SCENARIO_EVENT_SOURCE_ID_MAX_LEN 32
#define ORCH_ROOM_SCENARIO_OPERATOR_PROMPT_MAX_LEN 96
#define ORCH_ROOM_SCENARIO_OPERATOR_LABEL_MAX_LEN 32
#define ORCH_ROOM_SCENARIO_STEP_TEXT_MAX_LEN 96
#define ORCH_ROOM_SCENARIO_WAIT_SUMMARY_MAX_LEN 96
#define ORCH_ROOM_SCENARIO_MAX_STEPS       ROOM_SCENARIO_MAX_STEPS
#define ORCH_ROOM_SCENARIO_MAX_EVENT_REFS  ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS
#define ORCH_ROOM_SCENARIO_MAX_FLAG_REFS   ROOM_SCENARIO_WAIT_FLAGS_MAX_FLAGS
#define ORCH_ROOM_SCENARIO_MAX_DEVICE_REFS ORCH_REGISTRY_MAX_DEVICES
#define ORCH_ROOM_SCENARIO_MAX_FLAGS       16
#define ORCH_ROOM_SCENARIO_MAX_BRANCHES    ROOM_SCENARIO_MAX_BRANCHES
#define ORCH_ROOM_SCENARIO_MAX_COMMAND_GROUP_COMMANDS ROOM_SCENARIO_COMMAND_GROUP_MAX_COMMANDS
#define ORCH_ROOM_SCENARIO_MAX_REACTIVE_VARIANTS ROOM_SCENARIO_MAX_REACTIVE_VARIANTS
#define ORCH_ROOM_SCENARIO_MAX_REACTIVE_ACTIONS ROOM_SCENARIO_MAX_REACTIVE_ACTIONS
#define ORCH_ROOM_SCENARIO_MAX_REACTIVE_GROUP_COMMANDS ROOM_SCENARIO_MAX_REACTIVE_GROUP_COMMANDS
#define ORCH_ROOM_SCENARIO_MAX_STEP_SCHEMAS 11
#define ORCH_ROOM_SCENARIO_MAX_SCHEMA_FIELDS 6
#define ORCH_ROOM_SCENARIO_SCHEMA_KEY_MAX_LEN 24
#define ORCH_ROOM_SCENARIO_SCHEMA_FIELD_TYPE_MAX_LEN 32
#define ORCH_ROOM_SCENARIO_SCHEMA_LABEL_MAX_LEN 32
#define ORCH_ROOM_SCENARIO_SCHEMA_DESCRIPTION_MAX_LEN 96

typedef enum {
    ORCH_CONNECTIVITY_UNKNOWN = 0,
    ORCH_CONNECTIVITY_ONLINE,
    ORCH_CONNECTIVITY_OFFLINE,
} orch_connectivity_t;

typedef enum {
    ORCH_HEALTH_OK = 0,
    ORCH_HEALTH_DEGRADED,
    ORCH_HEALTH_FAULT,
} orch_health_t;

typedef enum {
    ORCH_ISSUE_SCOPE_SYSTEM = 0,
    ORCH_ISSUE_SCOPE_ROOM,
    ORCH_ISSUE_SCOPE_DEVICE,
} orch_issue_scope_t;

typedef enum {
    ORCH_ISSUE_SEVERITY_INFO = 0,
    ORCH_ISSUE_SEVERITY_WARNING,
    ORCH_ISSUE_SEVERITY_ERROR,
} orch_issue_severity_t;

typedef enum {
    ORCH_ROOM_SCENARIO_STEP_WAIT_TIME = 0,
    ORCH_ROOM_SCENARIO_STEP_OPERATOR_APPROVAL,
    ORCH_ROOM_SCENARIO_STEP_DEVICE_COMMAND,
    ORCH_ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT,
    ORCH_ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP,
    ORCH_ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE,
    ORCH_ROOM_SCENARIO_STEP_SET_FLAG,
    ORCH_ROOM_SCENARIO_STEP_WAIT_FLAGS,
    ORCH_ROOM_SCENARIO_STEP_WAIT_ANY_DEVICE_EVENT,
    ORCH_ROOM_SCENARIO_STEP_WAIT_ALL_DEVICE_EVENTS,
    ORCH_ROOM_SCENARIO_STEP_END_GAME,
} orch_room_scenario_step_type_t;

typedef enum {
    ORCH_RUNTIME_STATE_UNKNOWN = 0,
    ORCH_RUNTIME_STATE_IDLE,
    ORCH_RUNTIME_STATE_ARMED,
    ORCH_RUNTIME_STATE_ACTIVE,
    ORCH_RUNTIME_STATE_PAUSED,
    ORCH_RUNTIME_STATE_COMPLETED,
    ORCH_RUNTIME_STATE_TIMEOUT,
    ORCH_RUNTIME_STATE_FAILED,
} orch_runtime_state_t;

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
    char service_id[ORCH_REGISTRY_SERVICE_ID_MAX_LEN];
    bool init_attempted;
    bool init_ok;
    bool start_attempted;
    bool start_ok;
    bool fault;
    esp_err_t last_error;
    orch_health_t health;
} orch_service_entry_t;

typedef struct {
    char event_type[ORCH_ROOM_SCENARIO_EVENT_TYPE_MAX_LEN];
    char source_id[ORCH_ROOM_SCENARIO_EVENT_SOURCE_ID_MAX_LEN];
} orch_room_wait_event_entry_t;

typedef struct {
    char device_id[ORCH_ROOM_SCENARIO_DEVICE_ID_MAX_LEN];
    char event_id[ORCH_ROOM_SCENARIO_DEVICE_EVENT_ID_MAX_LEN];
} orch_room_scenario_event_ref_t;

typedef struct {
    char device_id[ORCH_ROOM_SCENARIO_DEVICE_ID_MAX_LEN];
    char command_id[ORCH_ROOM_SCENARIO_DEVICE_COMMAND_ID_MAX_LEN];
    char params_json[ORCH_ROOM_SCENARIO_COMMAND_PARAMS_JSON_MAX_LEN];
} orch_room_scenario_command_entry_t;

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

typedef struct {
    orch_room_entry_t room;
    uint64_t runtime_now_ms;
    char asset_prepare_state[16];
    uint16_t asset_audio_total;
    uint16_t asset_audio_ready;
    uint16_t asset_audio_missing;
    uint16_t asset_audio_bad;
    uint16_t asset_audio_unsupported;
    uint16_t asset_audio_io_error;
    uint16_t asset_audio_unknown;
    struct {
        uint8_t step_count;
        struct {
            uint16_t index;
            uint16_t global_index;
            orch_room_scenario_step_runtime_state_t state;
            char state_text[ORCH_REGISTRY_STATE_MAX_LEN];
            bool enabled;
            char text[ORCH_ROOM_SCENARIO_STEP_TEXT_MAX_LEN];
        } steps[ORCH_ROOM_SCENARIO_MAX_STEPS];
    } scenario_branch_steps[ORCH_ROOM_SCENARIO_MAX_BRANCHES];
} orch_room_runtime_view_t;

typedef struct {
    char id[ORCH_ROOM_SCENARIO_ID_MAX_LEN];
    char label[ORCH_ROOM_SCENARIO_STEP_LABEL_MAX_LEN];
    orch_room_scenario_step_type_t type;
    char type_text[ORCH_REGISTRY_STATE_MAX_LEN];
    bool enabled;
    bool allow_operator_skip;
    char operator_skip_label[ORCH_ROOM_SCENARIO_OPERATOR_LABEL_MAX_LEN];
    char device_id[ORCH_ROOM_SCENARIO_DEVICE_ID_MAX_LEN];
    char scenario_id[ORCH_ROOM_SCENARIO_ID_MAX_LEN];
    char command_id[ORCH_ROOM_SCENARIO_DEVICE_COMMAND_ID_MAX_LEN];
    char event_id[ORCH_ROOM_SCENARIO_DEVICE_EVENT_ID_MAX_LEN];
    char params_json[ORCH_ROOM_SCENARIO_COMMAND_PARAMS_JSON_MAX_LEN];
    orch_room_scenario_command_entry_t commands[ORCH_ROOM_SCENARIO_MAX_COMMAND_GROUP_COMMANDS];
    uint32_t duration_ms;
    uint32_t timeout_ms;
    char event_type[ORCH_ROOM_SCENARIO_EVENT_TYPE_MAX_LEN];
    char source_id[ORCH_ROOM_SCENARIO_EVENT_SOURCE_ID_MAX_LEN];
    char timeout_message[ROOM_SCENARIO_OPERATOR_MESSAGE_MAX_LEN];
    char operator_prompt[ORCH_ROOM_SCENARIO_OPERATOR_PROMPT_MAX_LEN];
    char operator_approve_label[ORCH_ROOM_SCENARIO_OPERATOR_LABEL_MAX_LEN];
    char operator_message[ROOM_SCENARIO_OPERATOR_MESSAGE_MAX_LEN];
    char flag_name[ROOM_SCENARIO_FLAG_NAME_MAX_LEN];
    bool flag_value;
    orch_room_scenario_event_ref_t events[ORCH_ROOM_SCENARIO_MAX_EVENT_REFS];
    uint8_t event_count;
    orch_room_scenario_flag_ref_t flags[ORCH_ROOM_SCENARIO_MAX_FLAG_REFS];
    uint8_t flag_count;
    uint8_t command_count;
} orch_room_scenario_step_entry_t;

typedef struct {
    room_scenario_reactive_trigger_kind_t kind;
    char kind_text[ORCH_REGISTRY_STATE_MAX_LEN];
    char device_id[ORCH_ROOM_SCENARIO_DEVICE_ID_MAX_LEN];
    char event_id[ORCH_ROOM_SCENARIO_DEVICE_EVENT_ID_MAX_LEN];
    char flag_name[ROOM_SCENARIO_FLAG_NAME_MAX_LEN];
    char runtime_event[ORCH_ROOM_SCENARIO_EVENT_TYPE_MAX_LEN];
    char operator_event[ORCH_ROOM_SCENARIO_EVENT_TYPE_MAX_LEN];
} orch_room_scenario_reactive_trigger_entry_t;

typedef struct {
    char id[ORCH_ROOM_SCENARIO_STEP_ID_MAX_LEN];
    char label[ORCH_ROOM_SCENARIO_STEP_LABEL_MAX_LEN];
    orch_room_scenario_step_type_t type;
    char type_text[ORCH_REGISTRY_STATE_MAX_LEN];
    room_scenario_command_group_mode_t group_mode;
    char group_mode_text[ORCH_REGISTRY_STATE_MAX_LEN];
    char device_id[ORCH_ROOM_SCENARIO_DEVICE_ID_MAX_LEN];
    char command_id[ORCH_ROOM_SCENARIO_DEVICE_COMMAND_ID_MAX_LEN];
    char params_json[ORCH_ROOM_SCENARIO_COMMAND_PARAMS_JSON_MAX_LEN];
    uint32_t duration_ms;
    char operator_message[ROOM_SCENARIO_OPERATOR_MESSAGE_MAX_LEN];
    char flag_name[ROOM_SCENARIO_FLAG_NAME_MAX_LEN];
    bool flag_value;
    uint16_t group_command_start_index;
    uint8_t group_command_count;
} orch_room_scenario_reactive_action_entry_t;

typedef struct {
    char id[ORCH_ROOM_SCENARIO_STEP_ID_MAX_LEN];
    char label[ORCH_ROOM_SCENARIO_STEP_LABEL_MAX_LEN];
    uint16_t action_start_index;
    uint8_t action_count;
} orch_room_scenario_reactive_variant_entry_t;

typedef struct {
    room_scenario_reactive_result_action_t on_done;
    char on_done_text[ORCH_REGISTRY_STATE_MAX_LEN];
    room_scenario_reactive_result_action_t on_fail;
    char on_fail_text[ORCH_REGISTRY_STATE_MAX_LEN];
    room_scenario_reactive_result_action_t on_timeout;
    char on_timeout_text[ORCH_REGISTRY_STATE_MAX_LEN];
    char flag[ROOM_SCENARIO_FLAG_NAME_MAX_LEN];
} orch_room_scenario_result_policy_entry_t;

typedef struct {
    char id[ROOM_SCENARIO_BRANCH_ID_MAX_LEN];
    char name[ROOM_SCENARIO_BRANCH_NAME_MAX_LEN];
    room_scenario_branch_type_t type;
    char type_text[ORCH_REGISTRY_STATE_MAX_LEN];
    bool enabled;
    bool required_for_completion;
    uint16_t priority;
    room_scenario_reactive_policy_mode_t policy_mode;
    char policy_mode_text[ORCH_REGISTRY_STATE_MAX_LEN];
    uint32_t cooldown_ms;
    uint32_t max_fire_count;
    bool run_once;
    room_scenario_reentry_mode_t reentry_mode;
    char reentry_mode_text[ORCH_REGISTRY_STATE_MAX_LEN];
    orch_room_scenario_reactive_trigger_entry_t trigger;
    orch_room_scenario_flag_ref_t guard_flags[ORCH_ROOM_SCENARIO_MAX_FLAG_REFS];
    uint8_t guard_flag_count;
    orch_room_scenario_result_policy_entry_t result_policy;
    uint16_t variant_start_index;
    uint8_t variant_count;
    uint16_t on_complete_action_start_index;
    uint8_t on_complete_action_count;
    uint16_t step_start_index;
    uint16_t step_count;
} orch_room_scenario_branch_detail_t;

typedef struct {
    char room_id[QUEST_ROOM_ID_MAX_LEN];
    char id[ORCH_ROOM_SCENARIO_ID_MAX_LEN];
    char name[ORCH_ROOM_SCENARIO_NAME_MAX_LEN];
    size_t step_count;
    bool valid;
    size_t validation_issue_count;
} orch_room_scenario_entry_t;

typedef struct {
    room_scenario_validation_level_t level;
    char level_text[ORCH_REGISTRY_STATE_MAX_LEN];
    size_t step_index;
    char code[ROOM_SCENARIO_VALIDATION_CODE_MAX_LEN];
    char message[ROOM_SCENARIO_VALIDATION_MESSAGE_MAX_LEN];
} orch_room_scenario_validation_issue_entry_t;

typedef struct {
    char id[GM_GAME_PROFILE_ID_MAX_LEN];
    char name[GM_GAME_PROFILE_NAME_MAX_LEN];
    char room_id[GM_GAME_PROFILE_ROOM_ID_MAX_LEN];
    char scenario_id[GM_GAME_PROFILE_SCENARIO_ID_MAX_LEN];
    uint32_t duration_ms;
    char hint_pack_id[GM_GAME_PROFILE_PACK_ID_MAX_LEN];
    char audio_pack_id[GM_GAME_PROFILE_PACK_ID_MAX_LEN];
    bool enabled;
    bool valid;
} orch_room_profile_entry_t;

typedef struct {
    orch_room_scenario_entry_t summary;
    orch_room_scenario_step_entry_t steps[ORCH_ROOM_SCENARIO_MAX_STEPS];
    orch_room_scenario_reactive_variant_entry_t reactive_variants[ORCH_ROOM_SCENARIO_MAX_REACTIVE_VARIANTS];
    orch_room_scenario_reactive_action_entry_t reactive_actions[ORCH_ROOM_SCENARIO_MAX_REACTIVE_ACTIONS];
    orch_room_scenario_command_entry_t reactive_group_commands[ORCH_ROOM_SCENARIO_MAX_REACTIVE_GROUP_COMMANDS];
    orch_room_scenario_branch_detail_t branches[ORCH_ROOM_SCENARIO_MAX_BRANCHES];
    uint8_t branch_count;
    uint8_t reactive_variant_count;
    uint8_t reactive_action_count;
    uint8_t reactive_group_command_count;
    orch_room_scenario_validation_issue_entry_t validation_issues[ROOM_SCENARIO_VALIDATION_MAX_ISSUES];
} orch_room_scenario_detail_t;

typedef struct {
    char key[ORCH_ROOM_SCENARIO_SCHEMA_KEY_MAX_LEN];
    char type[ORCH_ROOM_SCENARIO_SCHEMA_FIELD_TYPE_MAX_LEN];
    char label[ORCH_ROOM_SCENARIO_SCHEMA_LABEL_MAX_LEN];
    char depends_on[ORCH_ROOM_SCENARIO_SCHEMA_KEY_MAX_LEN];
    bool required;
} orch_room_scenario_field_schema_t;

typedef struct {
    char type[ORCH_ROOM_SCENARIO_SCHEMA_FIELD_TYPE_MAX_LEN];
    char label[ORCH_ROOM_SCENARIO_SCHEMA_LABEL_MAX_LEN];
    char description[ORCH_ROOM_SCENARIO_SCHEMA_DESCRIPTION_MAX_LEN];
    orch_room_scenario_field_schema_t fields[ORCH_ROOM_SCENARIO_MAX_SCHEMA_FIELDS];
    uint8_t field_count;
} orch_room_scenario_step_schema_t;

typedef struct {
    char issue_id[ORCH_REGISTRY_ISSUE_ID_MAX_LEN];
    char room_id[QUEST_ROOM_ID_MAX_LEN];
    char device_id[QUEST_ID_MAX_LEN];
    char code[ORCH_REGISTRY_ISSUE_CODE_MAX_LEN];
    char title[ORCH_REGISTRY_ISSUE_TITLE_MAX_LEN];
    char details[ORCH_REGISTRY_ISSUE_DETAILS_MAX_LEN];
    orch_issue_scope_t scope;
    char scope_text[ORCH_REGISTRY_STATE_MAX_LEN];
    orch_issue_severity_t severity;
    char severity_text[ORCH_REGISTRY_STATE_MAX_LEN];
    bool active;
} orch_issue_entry_t;

typedef struct {
    char device_id[QUEST_ID_MAX_LEN];
    char client_id[QUEST_ID_MAX_LEN];
    char display_name[QUEST_NAME_MAX_LEN];
    char room_id[QUEST_ROOM_ID_MAX_LEN];
    char state[ORCH_REGISTRY_STATE_MAX_LEN];
    orch_connectivity_t connectivity;
    char connectivity_text[ORCH_REGISTRY_STATE_MAX_LEN];
    orch_health_t health;
    char health_text[ORCH_REGISTRY_STATE_MAX_LEN];
    orch_runtime_state_t runtime_state;
    char runtime_state_text[ORCH_REGISTRY_STATE_MAX_LEN];
    uint64_t last_seen_ms;
    char fw_version[DEVICE_CONTROL_INGEST_FW_VERSION_MAX_LEN];
    char boot_id[DEVICE_CONTROL_INGEST_BOOT_ID_MAX_LEN];
    char last_diag_code[DEVICE_CONTROL_INGEST_CODE_MAX_LEN];
    char last_diag_message[DEVICE_CONTROL_INGEST_MESSAGE_MAX_LEN];
    char last_result_status[DEVICE_CONTROL_INGEST_RESULT_STATUS_MAX_LEN];
    char last_result_error_code[DEVICE_CONTROL_INGEST_ERROR_CODE_MAX_LEN];
    char badges[ORCH_REGISTRY_DEVICE_MAX_BADGES][ORCH_REGISTRY_DEVICE_BADGE_MAX_LEN];
    uint8_t badge_count;
    bool has_runtime;
    bool has_fault;
    bool has_degraded;
} orch_device_entry_t;

typedef struct {
    char device_id[QUEST_ID_MAX_LEN];
    orch_connectivity_t connectivity;
    char connectivity_text[ORCH_REGISTRY_STATE_MAX_LEN];
    orch_health_t health;
    char health_text[ORCH_REGISTRY_STATE_MAX_LEN];
    uint64_t last_seen_ms;
    char fw_version[DEVICE_CONTROL_INGEST_FW_VERSION_MAX_LEN];
    char boot_id[DEVICE_CONTROL_INGEST_BOOT_ID_MAX_LEN];
    char mode[DEVICE_CONTROL_INGEST_MODE_MAX_LEN];
    char state[DEVICE_CONTROL_INGEST_STATE_MAX_LEN];
    bool has_heartbeat;
    bool has_status;
    bool has_diag;
    bool has_result;
} orch_control_device_entry_t;

typedef struct {
    uint32_t generation;
    uint32_t cache_version;
    uint64_t snapshot_built_at_ms;
    char active_profile[QUEST_ID_MAX_LEN];
    uint8_t service_count;
    uint8_t room_count;
    uint8_t device_count;
    uint8_t issue_count;
    size_t room_scenario_count;
    uint16_t active_session_count;
    uint16_t active_hint_count;
    bool has_fault;
    bool has_degraded;
    orch_service_entry_t services[8];
    orch_room_entry_t rooms[ORCH_REGISTRY_MAX_ROOMS];
    orch_device_entry_t devices[ORCH_REGISTRY_MAX_DEVICES];
    orch_issue_entry_t issues[ORCH_REGISTRY_MAX_ISSUES];
    orch_room_scenario_entry_t room_scenarios[ORCH_REGISTRY_MAX_ROOM_SCENARIOS];
} orch_registry_snapshot_t;

typedef struct {
    uint32_t generation;
    uint8_t room_count;
    uint8_t device_count;
    uint8_t online_device_count;
    uint16_t issue_count;
    uint16_t active_session_count;
    uint16_t active_hint_count;
    uint16_t degraded_count;
    uint16_t fault_count;
    bool has_fault;
    bool has_degraded;
} orch_gm_system_summary_t;

esp_err_t orchestrator_registry_init(void);
esp_err_t orchestrator_registry_build_snapshot(orch_registry_snapshot_t *out);
esp_err_t orchestrator_registry_get_system_summary(orch_gm_system_summary_t *out);
void orchestrator_registry_invalidate(void);
esp_err_t orchestrator_registry_list_rooms(orch_room_entry_t *out_rooms,
                                           size_t max_rooms,
                                           size_t *out_count);
esp_err_t orchestrator_registry_get_room(const char *room_id, orch_room_entry_t *out);
esp_err_t orchestrator_registry_get_room_runtime_view(const char *room_id,
                                                      orch_room_runtime_view_t *out);
esp_err_t orchestrator_registry_get_device(const char *device_id, orch_device_entry_t *out);
esp_err_t orchestrator_registry_list_quest_devices(quest_device_t *out_devices,
                                                   size_t max_devices,
                                                   size_t *out_count,
                                                   bool include_system);
esp_err_t orchestrator_registry_list_control_devices(orch_control_device_entry_t *out_devices,
                                                     size_t max_devices,
                                                     size_t *out_count);
esp_err_t orchestrator_registry_list_device_issues(const char *device_id,
                                                   orch_issue_entry_t *out_issues,
                                                   size_t max_issues,
                                                   size_t *out_count);
esp_err_t orchestrator_registry_list_room_scenarios(const char *room_id,
                                                    orch_room_scenario_entry_t *out_scenarios,
                                                    size_t max_scenarios,
                                                    size_t *out_count);
esp_err_t orchestrator_registry_list_scenario_step_schemas(orch_room_scenario_step_schema_t *out_schemas,
                                                           size_t max_schemas,
                                                           size_t *out_count);
esp_err_t orchestrator_registry_list_room_profiles(const char *room_id,
                                                   orch_room_profile_entry_t *out_profiles,
                                                   size_t max_profiles,
                                                   size_t *out_count);
esp_err_t orchestrator_registry_list_room_scenario_details(const char *room_id,
                                                           orch_room_scenario_detail_t *out_scenarios,
                                                           size_t max_scenarios,
                                                           size_t *out_count);

#ifdef __cplusplus
}
#endif
