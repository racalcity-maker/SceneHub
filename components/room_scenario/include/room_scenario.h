#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ROOM_SCENARIO_ID_MAX_LEN 32
#define ROOM_SCENARIO_NAME_MAX_LEN 64
#define ROOM_SCENARIO_ROOM_ID_MAX_LEN 32
#define ROOM_SCENARIO_STEP_ID_MAX_LEN 32
#define ROOM_SCENARIO_STEP_LABEL_MAX_LEN 64
#define ROOM_SCENARIO_DEVICE_ID_MAX_LEN 32
#define ROOM_SCENARIO_DEVICE_COMMAND_ID_MAX_LEN 32
#define ROOM_SCENARIO_DEVICE_EVENT_ID_MAX_LEN 32
#define ROOM_SCENARIO_COMMAND_PARAMS_JSON_MAX_LEN 384
#define ROOM_SCENARIO_COMMAND_GROUP_MAX_COMMANDS 4
#define ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS 4
#define ROOM_SCENARIO_EVENT_TYPE_MAX_LEN 32
#define ROOM_SCENARIO_EVENT_SOURCE_ID_MAX_LEN 32
#define ROOM_SCENARIO_OPERATOR_PROMPT_MAX_LEN 96
#define ROOM_SCENARIO_OPERATOR_LABEL_MAX_LEN 32
#define ROOM_SCENARIO_OPERATOR_MESSAGE_MAX_LEN 160
#define ROOM_SCENARIO_FLAG_NAME_MAX_LEN 32
#define ROOM_SCENARIO_WAIT_FLAGS_MAX_FLAGS 8
#define ROOM_SCENARIO_BRANCH_ID_MAX_LEN 32
#define ROOM_SCENARIO_BRANCH_NAME_MAX_LEN 64
#define ROOM_SCENARIO_MAX_BRANCHES 8
#define ROOM_SCENARIO_MAX_STEPS 48
#define ROOM_SCENARIO_MAX_SCENARIOS 24
#define ROOM_SCENARIO_VALIDATION_CODE_MAX_LEN 48
#define ROOM_SCENARIO_VALIDATION_MESSAGE_MAX_LEN 128
#define ROOM_SCENARIO_VALIDATION_MAX_ISSUES 32
#define ROOM_SCENARIO_STORAGE_PATH "/sdcard/quest/room_scenarios.json"

typedef enum {
    ROOM_SCENARIO_STEP_WAIT_TIME = 0,
    ROOM_SCENARIO_STEP_OPERATOR_APPROVAL,
    ROOM_SCENARIO_STEP_DEVICE_COMMAND,
    ROOM_SCENARIO_STEP_WAIT_DEVICE_EVENT,
    ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP,
    ROOM_SCENARIO_STEP_SHOW_OPERATOR_MESSAGE,
    ROOM_SCENARIO_STEP_SET_FLAG,
    ROOM_SCENARIO_STEP_WAIT_FLAGS,
    ROOM_SCENARIO_STEP_WAIT_ANY_DEVICE_EVENT,
    ROOM_SCENARIO_STEP_WAIT_ALL_DEVICE_EVENTS,
    ROOM_SCENARIO_STEP_END_GAME,
} room_scenario_step_type_t;

typedef enum {
    ROOM_SCENARIO_VALIDATION_ERROR = 0,
    ROOM_SCENARIO_VALIDATION_WARNING,
} room_scenario_validation_level_t;

typedef enum {
    ROOM_SCENARIO_BRANCH_NORMAL = 0,
    ROOM_SCENARIO_BRANCH_REACTIVE,
} room_scenario_branch_type_t;

typedef struct {
    char device_id[ROOM_SCENARIO_DEVICE_ID_MAX_LEN];
    char command_id[ROOM_SCENARIO_DEVICE_COMMAND_ID_MAX_LEN];
    char params_json[ROOM_SCENARIO_COMMAND_PARAMS_JSON_MAX_LEN];
} room_scenario_device_command_t;

typedef struct {
    struct {
        char device_id[ROOM_SCENARIO_DEVICE_ID_MAX_LEN];
        char command_id[ROOM_SCENARIO_DEVICE_COMMAND_ID_MAX_LEN];
    } commands[ROOM_SCENARIO_COMMAND_GROUP_MAX_COMMANDS];
    uint8_t command_count;
} room_scenario_device_command_group_t;

typedef struct {
    char device_id[ROOM_SCENARIO_DEVICE_ID_MAX_LEN];
    char event_id[ROOM_SCENARIO_DEVICE_EVENT_ID_MAX_LEN];
    uint32_t timeout_ms;
    char timeout_message[ROOM_SCENARIO_OPERATOR_MESSAGE_MAX_LEN];
} room_scenario_wait_device_event_t;

typedef room_scenario_wait_device_event_t room_scenario_device_event_ref_t;

typedef struct {
    room_scenario_device_event_ref_t events[ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS];
    uint8_t event_count;
} room_scenario_wait_any_device_event_t;

typedef room_scenario_wait_any_device_event_t room_scenario_wait_all_device_events_t;

typedef struct {
    uint32_t duration_ms;
} room_scenario_wait_time_t;

typedef struct {
    char prompt[ROOM_SCENARIO_OPERATOR_PROMPT_MAX_LEN];
    char approve_label[ROOM_SCENARIO_OPERATOR_LABEL_MAX_LEN];
} room_scenario_operator_approval_t;

typedef struct {
    char message[ROOM_SCENARIO_OPERATOR_MESSAGE_MAX_LEN];
} room_scenario_operator_message_t;

typedef struct {
    char name[ROOM_SCENARIO_FLAG_NAME_MAX_LEN];
    bool value;
} room_scenario_set_flag_t;

typedef room_scenario_set_flag_t room_scenario_flag_condition_t;

typedef struct {
    room_scenario_flag_condition_t flags[ROOM_SCENARIO_WAIT_FLAGS_MAX_FLAGS];
    uint8_t flag_count;
    uint32_t timeout_ms;
    char timeout_message[ROOM_SCENARIO_OPERATOR_MESSAGE_MAX_LEN];
} room_scenario_wait_flags_t;

typedef struct {
    char id[ROOM_SCENARIO_STEP_ID_MAX_LEN];
    char label[ROOM_SCENARIO_STEP_LABEL_MAX_LEN];
    room_scenario_step_type_t type;
    bool enabled;
    bool allow_operator_skip;
    char operator_skip_label[ROOM_SCENARIO_OPERATOR_LABEL_MAX_LEN];

    union {
        room_scenario_device_command_t device_command;
        room_scenario_device_command_group_t device_command_group;
        room_scenario_wait_device_event_t wait_device_event;
        room_scenario_wait_time_t wait_time;
        room_scenario_operator_approval_t operator_approval;
        room_scenario_operator_message_t operator_message;
        room_scenario_set_flag_t set_flag;
        room_scenario_wait_flags_t wait_flags;
        room_scenario_wait_any_device_event_t wait_any_device_event;
        room_scenario_wait_all_device_events_t wait_all_device_events;
    } data;
} room_scenario_step_t;

typedef struct {
    char id[ROOM_SCENARIO_BRANCH_ID_MAX_LEN];
    char name[ROOM_SCENARIO_BRANCH_NAME_MAX_LEN];
    room_scenario_branch_type_t type;
    bool enabled;
    bool required_for_completion;
    uint32_t cooldown_ms;
    bool run_once;
    uint16_t step_start_index;
    uint16_t step_count;
} room_scenario_branch_t;

typedef struct {
    char id[ROOM_SCENARIO_ID_MAX_LEN];
    char name[ROOM_SCENARIO_NAME_MAX_LEN];
    char room_id[ROOM_SCENARIO_ROOM_ID_MAX_LEN];

    room_scenario_step_t steps[ROOM_SCENARIO_MAX_STEPS];
    size_t step_count;
    room_scenario_branch_t branches[ROOM_SCENARIO_MAX_BRANCHES];
    size_t branch_count;
} room_scenario_t;

typedef struct {
    room_scenario_validation_level_t level;
    uint16_t step_index;
    char code[ROOM_SCENARIO_VALIDATION_CODE_MAX_LEN];
    char message[ROOM_SCENARIO_VALIDATION_MESSAGE_MAX_LEN];
} room_scenario_validation_issue_t;

typedef struct {
    bool valid;
    room_scenario_validation_issue_t issues[ROOM_SCENARIO_VALIDATION_MAX_ISSUES];
    size_t issue_count;
} room_scenario_validation_report_t;

esp_err_t room_scenario_init(void);
esp_err_t room_scenario_add(const room_scenario_t *scenario);
esp_err_t room_scenario_add_and_save(const room_scenario_t *scenario);
esp_err_t room_scenario_delete(const char *scenario_id);
esp_err_t room_scenario_delete_and_save(const char *scenario_id);
esp_err_t room_scenario_get(const char *scenario_id, room_scenario_t *out);
esp_err_t room_scenario_list_by_room(const char *room_id,
                                     room_scenario_t *out,
                                     size_t max_count,
                                     size_t *out_count);
esp_err_t room_scenario_validate(const room_scenario_t *scenario,
                                 room_scenario_validation_report_t *out);
esp_err_t room_scenario_validate_by_id(const char *scenario_id,
                                       room_scenario_validation_report_t *out);
const char *room_scenario_step_type_to_str(room_scenario_step_type_t type);
esp_err_t room_scenario_step_type_from_str(const char *s,
                                           room_scenario_step_type_t *out);
const char *room_scenario_branch_type_to_str(room_scenario_branch_type_t type);
esp_err_t room_scenario_branch_type_from_str(const char *s,
                                             room_scenario_branch_type_t *out);
esp_err_t room_scenario_to_json(const room_scenario_t *s, cJSON *out);
esp_err_t room_scenario_from_json(const cJSON *json, room_scenario_t *out);
esp_err_t room_scenario_store_export_json(cJSON **out);
esp_err_t room_scenario_store_import_json(const cJSON *root);
esp_err_t room_scenario_store_import_json_and_save(const cJSON *root);
esp_err_t room_scenario_store_save(void);
esp_err_t room_scenario_store_load(void);
esp_err_t room_scenario_store_save_to_path(const char *path);
esp_err_t room_scenario_store_load_from_path(const char *path);
esp_err_t room_scenario_export_json(cJSON **out_root);
esp_err_t room_scenario_import_json(const cJSON *root);
esp_err_t room_scenario_clear(void);
uint32_t room_scenario_generation(void);
void room_scenario_reset(void);

#ifdef __cplusplus
}
#endif
