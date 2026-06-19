#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "node_config.h"
#include "node_rule_model.h"

typedef enum {
    NODE_RULE_COMPILE_STATUS_INACTIVE = 0,
    NODE_RULE_COMPILE_STATUS_READY,
    NODE_RULE_COMPILE_STATUS_ERROR,
} node_rule_compile_status_t;

typedef enum {
    NODE_RULE_TRIGGER_NONE = 0,
    NODE_RULE_TRIGGER_BOOT,
    NODE_RULE_TRIGGER_INPUT_EDGE,
    NODE_RULE_TRIGGER_INPUT_LEVEL,
    NODE_RULE_TRIGGER_INPUT_HOLD,
    NODE_RULE_TRIGGER_ALL_INPUTS_LEVEL,
    NODE_RULE_TRIGGER_TIMER,
    NODE_RULE_TRIGGER_LOCAL_EVENT,
    NODE_RULE_TRIGGER_STATE_CHANGED,
    NODE_RULE_TRIGGER_MQTT_COMMAND,
} node_rule_compiled_trigger_kind_t;

typedef enum {
    NODE_RULE_SCALAR_TYPE_NONE = 0,
    NODE_RULE_SCALAR_TYPE_BOOL,
    NODE_RULE_SCALAR_TYPE_INT32,
} node_rule_scalar_type_t;

typedef struct {
    node_rule_scalar_type_t type;
    int32_t int_value;
} node_rule_scalar_value_t;

typedef enum {
    NODE_RULE_CONDITION_NONE = 0,
    NODE_RULE_CONDITION_STATE_EQUALS,
    NODE_RULE_CONDITION_PHASE_IS,
    NODE_RULE_CONDITION_INPUT_EQUALS,
    NODE_RULE_CONDITION_EVENT_FIELD_EQUALS,
    NODE_RULE_CONDITION_ALL_INPUTS_EQUAL,
    NODE_RULE_CONDITION_NOT,
    NODE_RULE_CONDITION_ALL,
    NODE_RULE_CONDITION_ANY,
} node_rule_compiled_condition_kind_t;

typedef enum {
    NODE_RULE_EVENT_FIELD_NONE = 0,
    NODE_RULE_EVENT_FIELD_TOKEN_ID,
} node_rule_compiled_event_field_t;

typedef struct {
    node_rule_compiled_condition_kind_t kind;
    uint16_t state_index;
    uint16_t phase_index;
    uint16_t first_child_index;
    uint16_t next_sibling_index;
    uint8_t input_channel;
    uint8_t input_count;
    uint8_t input_channels[NODE_RULE_MAX_GROUP_INPUTS];
    uint8_t child_count;
    node_rule_compiled_event_field_t event_field;
    node_rule_scalar_value_t value;
} node_rule_compiled_condition_t;

typedef enum {
    NODE_RULE_ACTION_NONE = 0,
    NODE_RULE_ACTION_COMMAND,
    NODE_RULE_ACTION_SET_STATE,
    NODE_RULE_ACTION_SET_PHASE,
    NODE_RULE_ACTION_EMIT_EVENT,
    NODE_RULE_ACTION_START_TIMER,
    NODE_RULE_ACTION_CANCEL_TIMER,
    NODE_RULE_ACTION_CHOOSE,
} node_rule_compiled_action_kind_t;

typedef enum {
    NODE_RULE_TIMER_MODE_ONESHOT = 0,
    NODE_RULE_TIMER_MODE_REPEAT,
    NODE_RULE_TIMER_MODE_COOLDOWN,
} node_rule_compiled_timer_mode_t;

typedef struct {
    node_rule_compiled_action_kind_t kind;
    uint16_t state_index;
    uint16_t phase_index;
    uint16_t timer_index;
    uint16_t condition_index;
    uint16_t then_action_start;
    uint16_t then_action_count;
    uint16_t else_action_start;
    uint16_t else_action_count;
    uint16_t next_action_index;
    node_rule_scalar_value_t value;
    node_rule_compiled_timer_mode_t timer_mode;
    uint32_t duration_ms;
    uint32_t interval_ms;
    char command[NODE_DRIVER_EVENT_NAME_MAX_LEN];
    char event_name[NODE_DRIVER_EVENT_NAME_MAX_LEN];
    char payload_json[96];
} node_rule_compiled_action_t;

typedef struct {
    char id[NODE_DRIVER_EVENT_NAME_MAX_LEN];
    bool enabled;
    bool has_conditions;
    node_rule_compiled_trigger_kind_t trigger_kind;
    uint8_t input_channel;
    int32_t trigger_value;
    uint16_t timer_index;
    uint16_t condition_index;
    uint16_t action_start;
    uint16_t action_count;
    uint32_t trigger_duration_ms;
    char trigger_event_name[NODE_DRIVER_EVENT_NAME_MAX_LEN];
} node_rule_compiled_rule_t;

typedef struct {
    char id[NODE_DRIVER_EVENT_NAME_MAX_LEN];
    char label[NODE_RULE_EXPORT_LABEL_MAX_LEN + 1];
    uint8_t claim_count;
    char claims[NODE_RULE_MAX_EXPORT_CLAIMS][NODE_DRIVER_EVENT_NAME_MAX_LEN];
} node_rule_exported_command_t;

typedef struct {
    char id[NODE_DRIVER_EVENT_NAME_MAX_LEN];
    char label[NODE_RULE_EXPORT_LABEL_MAX_LEN + 1];
} node_rule_exported_event_t;

typedef struct {
    node_rule_compile_status_t status;
    char error_code[NODE_RULE_API_ERROR_MAX_LEN];
    node_rule_bundle_metadata_t metadata;
    size_t rule_count;
    size_t enabled_rule_count;
    size_t total_action_count;
    size_t max_rule_action_count;
    size_t emit_count;
    size_t driver_count;
    size_t state_key_count;
    size_t initial_state_count;
    size_t timer_count;
    size_t phase_count;
    size_t condition_count;
    size_t export_command_count;
    size_t export_event_count;
    char emit_names[NODE_RULE_MAX_EMIT_EVENTS][NODE_DRIVER_EVENT_NAME_MAX_LEN];
    char state_keys[NODE_RULE_MAX_STATE_KEYS][NODE_RULE_STATE_KEY_MAX_LEN + 1];
    char timer_names[NODE_RULE_MAX_TIMERS][NODE_RULE_TIMER_NAME_MAX_LEN + 1];
    char phase_names[NODE_RULE_MAX_PHASES][NODE_RULE_PHASE_NAME_MAX_LEN + 1];
    node_rule_scalar_value_t initial_state_values[NODE_RULE_MAX_STATE_KEYS];
    node_rule_compiled_condition_t conditions[NODE_RULE_MAX_ACTIONS_TOTAL];
    node_rule_compiled_action_t actions[NODE_RULE_MAX_ACTIONS_TOTAL];
    node_rule_compiled_rule_t rules[NODE_RULE_MAX_RULES];
    node_rule_exported_command_t export_commands[NODE_RULE_MAX_EXPORT_COMMANDS];
    node_rule_exported_event_t export_events[NODE_RULE_MAX_EXPORT_EVENTS];
} node_rule_compiled_bundle_t;

const char *node_rule_compile_status_name(node_rule_compile_status_t status);
esp_err_t node_rule_compile_bundle_for_config(const char *raw_json,
                                              const node_config_t *config,
                                              node_rule_compiled_bundle_t *out_bundle,
                                              char *out_error_code,
                                              size_t out_error_code_size);
esp_err_t node_rule_compile_bootstrap(const node_config_t *config);
void node_rule_compile_get_active(node_rule_compiled_bundle_t *out_bundle);
const node_rule_compiled_bundle_t *node_rule_compile_peek_active(void);
bool node_rule_compile_has_ready_bundle(void);
