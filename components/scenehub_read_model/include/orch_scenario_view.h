#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "gm_game_profile.h"
#include "orch_runtime_view.h"
#include "room_scenario.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ORCH_REGISTRY_STATE_MAX_LEN
#define ORCH_REGISTRY_STATE_MAX_LEN 16
#endif
#ifndef ORCH_REGISTRY_MAX_ROOM_SCENARIOS
#define ORCH_REGISTRY_MAX_ROOM_SCENARIOS ROOM_SCENARIO_MAX_SCENARIOS
#endif
#ifndef ORCH_ROOM_SCENARIO_ID_MAX_LEN
#define ORCH_ROOM_SCENARIO_ID_MAX_LEN 32
#endif
#ifndef ORCH_ROOM_SCENARIO_NAME_MAX_LEN
#define ORCH_ROOM_SCENARIO_NAME_MAX_LEN 64
#endif
#ifndef ORCH_ROOM_SCENARIO_STEP_ID_MAX_LEN
#define ORCH_ROOM_SCENARIO_STEP_ID_MAX_LEN 32
#endif
#ifndef ORCH_ROOM_SCENARIO_STEP_LABEL_MAX_LEN
#define ORCH_ROOM_SCENARIO_STEP_LABEL_MAX_LEN 64
#endif
#ifndef ORCH_ROOM_SCENARIO_DEVICE_ID_MAX_LEN
#define ORCH_ROOM_SCENARIO_DEVICE_ID_MAX_LEN 32
#endif
#ifndef ORCH_ROOM_SCENARIO_DEVICE_COMMAND_ID_MAX_LEN
#define ORCH_ROOM_SCENARIO_DEVICE_COMMAND_ID_MAX_LEN 32
#endif
#ifndef ORCH_ROOM_SCENARIO_DEVICE_EVENT_ID_MAX_LEN
#define ORCH_ROOM_SCENARIO_DEVICE_EVENT_ID_MAX_LEN 32
#endif
#ifndef ORCH_ROOM_SCENARIO_COMMAND_PARAMS_JSON_MAX_LEN
#define ORCH_ROOM_SCENARIO_COMMAND_PARAMS_JSON_MAX_LEN 384
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
#ifndef ORCH_ROOM_SCENARIO_MAX_STEPS
#define ORCH_ROOM_SCENARIO_MAX_STEPS ROOM_SCENARIO_MAX_STEPS
#endif
#ifndef ORCH_ROOM_SCENARIO_MAX_EVENT_REFS
#define ORCH_ROOM_SCENARIO_MAX_EVENT_REFS ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS
#endif
#ifndef ORCH_ROOM_SCENARIO_MAX_FLAG_REFS
#define ORCH_ROOM_SCENARIO_MAX_FLAG_REFS ROOM_SCENARIO_WAIT_FLAGS_MAX_FLAGS
#endif
#ifndef ORCH_ROOM_SCENARIO_MAX_BRANCHES
#define ORCH_ROOM_SCENARIO_MAX_BRANCHES ROOM_SCENARIO_MAX_BRANCHES
#endif
#ifndef ORCH_ROOM_SCENARIO_MAX_COMMAND_GROUP_COMMANDS
#define ORCH_ROOM_SCENARIO_MAX_COMMAND_GROUP_COMMANDS ROOM_SCENARIO_COMMAND_GROUP_MAX_COMMANDS
#endif
#ifndef ORCH_ROOM_SCENARIO_MAX_REACTIVE_VARIANTS
#define ORCH_ROOM_SCENARIO_MAX_REACTIVE_VARIANTS ROOM_SCENARIO_MAX_REACTIVE_VARIANTS
#endif
#ifndef ORCH_ROOM_SCENARIO_MAX_REACTIVE_ACTIONS
#define ORCH_ROOM_SCENARIO_MAX_REACTIVE_ACTIONS ROOM_SCENARIO_MAX_REACTIVE_ACTIONS
#endif
#ifndef ORCH_ROOM_SCENARIO_MAX_REACTIVE_GROUP_COMMANDS
#define ORCH_ROOM_SCENARIO_MAX_REACTIVE_GROUP_COMMANDS ROOM_SCENARIO_MAX_REACTIVE_GROUP_COMMANDS
#endif
#ifndef ORCH_ROOM_SCENARIO_MAX_STEP_SCHEMAS
#define ORCH_ROOM_SCENARIO_MAX_STEP_SCHEMAS 11
#endif
#ifndef ORCH_ROOM_SCENARIO_MAX_SCHEMA_FIELDS
#define ORCH_ROOM_SCENARIO_MAX_SCHEMA_FIELDS 6
#endif
#ifndef ORCH_ROOM_SCENARIO_SCHEMA_KEY_MAX_LEN
#define ORCH_ROOM_SCENARIO_SCHEMA_KEY_MAX_LEN 24
#endif
#ifndef ORCH_ROOM_SCENARIO_SCHEMA_FIELD_TYPE_MAX_LEN
#define ORCH_ROOM_SCENARIO_SCHEMA_FIELD_TYPE_MAX_LEN 32
#endif
#ifndef ORCH_ROOM_SCENARIO_SCHEMA_LABEL_MAX_LEN
#define ORCH_ROOM_SCENARIO_SCHEMA_LABEL_MAX_LEN 32
#endif
#ifndef ORCH_ROOM_SCENARIO_SCHEMA_DESCRIPTION_MAX_LEN
#define ORCH_ROOM_SCENARIO_SCHEMA_DESCRIPTION_MAX_LEN 96
#endif

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
    char branch_id[ROOM_SCENARIO_BRANCH_ID_MAX_LEN];
    int16_t variant_index;
    int16_t action_index;
    char code[ROOM_SCENARIO_VALIDATION_CODE_MAX_LEN];
    char message[ROOM_SCENARIO_VALIDATION_MESSAGE_MAX_LEN];
} orch_room_scenario_validation_issue_entry_t;

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

/*
 * Scenario editor/read-model DTOs. They are allowed to be wide for editor
 * detail and layout endpoints, but must not be used as live runtime DTOs.
 */

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

esp_err_t orchestrator_registry_list_room_scenarios(const char *room_id,
                                                    orch_room_scenario_entry_t *out_scenarios,
                                                    size_t max_scenarios,
                                                    size_t *out_count);
esp_err_t orchestrator_registry_list_scenario_step_schemas(orch_room_scenario_step_schema_t *out_schemas,
                                                           size_t max_schemas,
                                                           size_t *out_count);
esp_err_t orchestrator_registry_list_room_scenario_details(const char *room_id,
                                                           orch_room_scenario_detail_t *out_scenarios,
                                                           size_t max_scenarios,
                                                           size_t *out_count);
esp_err_t orchestrator_registry_get_room_scenario_detail(const char *room_id,
                                                         const char *scenario_id,
                                                         orch_room_scenario_detail_t *out);
esp_err_t orchestrator_registry_get_room_scenario_layout(const char *room_id,
                                                         const char *scenario_id,
                                                         room_scenario_t *out);

#ifdef __cplusplus
}
#endif
