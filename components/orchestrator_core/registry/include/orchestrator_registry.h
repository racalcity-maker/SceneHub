#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "device_control_ingest.h"
#include "gm_room_session.h"
#include "quest_common_limits.h"
#include "quest_device.h"
#include "room_catalog.h"

#define ORCH_REGISTRY_SERVICE_ID_MAX_LEN   16
#define ORCH_REGISTRY_STATE_MAX_LEN        16
#define ORCH_REGISTRY_ISSUE_ID_MAX_LEN     96
#define ORCH_REGISTRY_ISSUE_CODE_MAX_LEN   32
#define ORCH_REGISTRY_ISSUE_TITLE_MAX_LEN  64
#define ORCH_REGISTRY_ISSUE_DETAILS_MAX_LEN 160
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
#define ORCH_ROOM_SCENARIO_MAX_STEPS       ROOM_SCENARIO_MAX_STEPS
#define ORCH_ROOM_SCENARIO_MAX_EVENT_REFS  ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS
#define ORCH_ROOM_SCENARIO_MAX_FLAG_REFS   ROOM_SCENARIO_WAIT_FLAGS_MAX_FLAGS
#define ORCH_ROOM_SCENARIO_MAX_BRANCHES    ROOM_SCENARIO_MAX_BRANCHES

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

typedef struct {
    char service_id[ORCH_REGISTRY_SERVICE_ID_MAX_LEN];
    bool init_attempted;
    bool init_ok;
    bool start_attempted;
    bool start_ok;
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
    char name[ROOM_SCENARIO_FLAG_NAME_MAX_LEN];
    bool value;
} orch_room_scenario_flag_ref_t;

typedef struct {
    char id[ROOM_SCENARIO_BRANCH_ID_MAX_LEN];
    char name[ROOM_SCENARIO_BRANCH_NAME_MAX_LEN];
    bool active;
    room_scenario_branch_type_t type;
    bool required_for_completion;
    uint32_t cooldown_ms;
    uint32_t cooldown_until_ms;
    bool run_once;
    bool fired_once;
    uint16_t step_start_index;
    uint16_t step_count;
    uint16_t current_step_index;
    gm_room_scenario_state_t state;
    gm_room_scenario_wait_type_t wait_type;
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
    gm_room_scenario_state_t scenario_runtime_state;
    uint16_t scenario_current_step_index;
    gm_room_scenario_wait_type_t scenario_wait_type;
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
    gm_room_scenario_flag_t scenario_flags[GM_ROOM_SCENARIO_MAX_FLAGS];
    uint8_t scenario_flag_count;
    orch_room_scenario_branch_entry_t scenario_branches[ORCH_ROOM_SCENARIO_MAX_BRANCHES];
    uint8_t scenario_branch_count;
    char scenario_last_error[96];
    orch_health_t health;
    bool session_present;
    bool hint_active;
} orch_room_entry_t;

typedef struct {
    char id[ORCH_ROOM_SCENARIO_ID_MAX_LEN];
    char label[ORCH_ROOM_SCENARIO_STEP_LABEL_MAX_LEN];
    orch_room_scenario_step_type_t type;
    bool enabled;
    char device_id[ORCH_ROOM_SCENARIO_DEVICE_ID_MAX_LEN];
    char scenario_id[ORCH_ROOM_SCENARIO_ID_MAX_LEN];
    char command_id[ORCH_ROOM_SCENARIO_DEVICE_COMMAND_ID_MAX_LEN];
    char event_id[ORCH_ROOM_SCENARIO_DEVICE_EVENT_ID_MAX_LEN];
    char params_json[ORCH_ROOM_SCENARIO_COMMAND_PARAMS_JSON_MAX_LEN];
    uint32_t duration_ms;
    char event_type[ORCH_ROOM_SCENARIO_EVENT_TYPE_MAX_LEN];
    char source_id[ORCH_ROOM_SCENARIO_EVENT_SOURCE_ID_MAX_LEN];
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
    char room_id[QUEST_ROOM_ID_MAX_LEN];
    char id[ORCH_ROOM_SCENARIO_ID_MAX_LEN];
    char name[ORCH_ROOM_SCENARIO_NAME_MAX_LEN];
    size_t step_count;
    bool valid;
    size_t validation_issue_count;
} orch_room_scenario_entry_t;

typedef struct {
    orch_room_scenario_entry_t summary;
    orch_room_scenario_step_entry_t steps[ORCH_ROOM_SCENARIO_MAX_STEPS];
    room_scenario_validation_issue_t validation_issues[ROOM_SCENARIO_VALIDATION_MAX_ISSUES];
} orch_room_scenario_detail_t;

typedef struct {
    char issue_id[ORCH_REGISTRY_ISSUE_ID_MAX_LEN];
    char room_id[QUEST_ROOM_ID_MAX_LEN];
    char device_id[QUEST_ID_MAX_LEN];
    char code[ORCH_REGISTRY_ISSUE_CODE_MAX_LEN];
    char title[ORCH_REGISTRY_ISSUE_TITLE_MAX_LEN];
    char details[ORCH_REGISTRY_ISSUE_DETAILS_MAX_LEN];
    orch_issue_scope_t scope;
    orch_issue_severity_t severity;
    bool active;
} orch_issue_entry_t;

typedef struct {
    char device_id[QUEST_ID_MAX_LEN];
    char client_id[QUEST_ID_MAX_LEN];
    char display_name[QUEST_NAME_MAX_LEN];
    char room_id[QUEST_ROOM_ID_MAX_LEN];
    char state[ORCH_REGISTRY_STATE_MAX_LEN];
    orch_connectivity_t connectivity;
    orch_health_t health;
    orch_runtime_state_t runtime_state;
    uint64_t last_seen_ms;
    char fw_version[DEVICE_CONTROL_INGEST_FW_VERSION_MAX_LEN];
    char boot_id[DEVICE_CONTROL_INGEST_BOOT_ID_MAX_LEN];
    char last_diag_code[DEVICE_CONTROL_INGEST_CODE_MAX_LEN];
    char last_diag_message[DEVICE_CONTROL_INGEST_MESSAGE_MAX_LEN];
    char last_result_status[DEVICE_CONTROL_INGEST_RESULT_STATUS_MAX_LEN];
    char last_result_error_code[DEVICE_CONTROL_INGEST_ERROR_CODE_MAX_LEN];
    bool has_runtime;
    bool has_fault;
    bool has_degraded;
} orch_device_entry_t;

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
    bool has_fault;
    bool has_degraded;
    orch_service_entry_t services[8];
    orch_room_entry_t rooms[ORCH_REGISTRY_MAX_ROOMS];
    orch_device_entry_t devices[ORCH_REGISTRY_MAX_DEVICES];
    orch_issue_entry_t issues[ORCH_REGISTRY_MAX_ISSUES];
    orch_room_scenario_entry_t room_scenarios[ORCH_REGISTRY_MAX_ROOM_SCENARIOS];
} orch_registry_snapshot_t;

esp_err_t orchestrator_registry_init(void);
esp_err_t orchestrator_registry_build_snapshot(orch_registry_snapshot_t *out);
void orchestrator_registry_invalidate(void);
esp_err_t orchestrator_registry_get_device(const char *device_id, orch_device_entry_t *out);
esp_err_t orchestrator_registry_list_room_scenarios(const char *room_id,
                                                    orch_room_scenario_entry_t *out_scenarios,
                                                    size_t max_scenarios,
                                                    size_t *out_count);
esp_err_t orchestrator_registry_list_room_scenario_details(const char *room_id,
                                                           orch_room_scenario_detail_t *out_scenarios,
                                                           size_t max_scenarios,
                                                           size_t *out_count);

#ifdef __cplusplus
}
#endif
