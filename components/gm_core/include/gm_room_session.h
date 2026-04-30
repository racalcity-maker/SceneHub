#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "event_bus.h"
#include "gm_game_profile.h"
#include "gm_hint.h"
#include "gm_timer.h"
#include "room_catalog.h"
#include "room_scenario.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GM_SESSION_MAX_ROOMS ROOM_CATALOG_MAX_ROOMS
#define GM_ROOM_SCENARIO_MAX_FLAGS 16

typedef enum {
    GM_SESSION_IDLE = 0,
    GM_SESSION_RUNNING,
    GM_SESSION_PAUSED,
    GM_SESSION_FINISHED,
} gm_session_state_t;

typedef enum {
    GM_ROOM_SCENARIO_IDLE = 0,
    GM_ROOM_SCENARIO_RUNNING,
    GM_ROOM_SCENARIO_WAITING,
    GM_ROOM_SCENARIO_PAUSED,
    GM_ROOM_SCENARIO_DONE,
    GM_ROOM_SCENARIO_STOPPED,
    GM_ROOM_SCENARIO_COOLDOWN,
    GM_ROOM_SCENARIO_ERROR,
} gm_room_scenario_state_t;

typedef enum {
    GM_ROOM_SCENARIO_WAIT_NONE = 0,
    GM_ROOM_SCENARIO_WAIT_TIME,
    GM_ROOM_SCENARIO_WAIT_DEVICE_EVENT,
    GM_ROOM_SCENARIO_WAIT_ANY_DEVICE_EVENT,
    GM_ROOM_SCENARIO_WAIT_OPERATOR,
    GM_ROOM_SCENARIO_WAIT_FLAGS,
    GM_ROOM_SCENARIO_WAIT_ALL_DEVICE_EVENTS,
} gm_room_scenario_wait_type_t;

typedef struct {
    char name[ROOM_SCENARIO_FLAG_NAME_MAX_LEN];
    bool value;
} gm_room_scenario_flag_t;

typedef struct {
    char event_type[ROOM_SCENARIO_EVENT_TYPE_MAX_LEN];
    char source_id[ROOM_SCENARIO_EVENT_SOURCE_ID_MAX_LEN];
} gm_room_scenario_wait_event_match_t;

typedef struct {
    bool active;
    room_scenario_branch_type_t type;
    bool required_for_completion;
    uint32_t cooldown_ms;
    uint32_t cooldown_until_ms;
    bool run_once;
    bool fired_once;
    uint16_t branch_index;
    uint16_t step_start_index;
    uint16_t step_count;
    gm_room_scenario_state_t scenario_state;
    uint16_t current_step_index;
    gm_room_scenario_wait_type_t wait_type;
    uint32_t wait_until_ms;
    uint32_t wait_started_at_ms;
    char wait_event_type[ROOM_SCENARIO_EVENT_TYPE_MAX_LEN];
    char wait_source_id[ROOM_SCENARIO_EVENT_SOURCE_ID_MAX_LEN];
    gm_room_scenario_wait_event_match_t wait_events[ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS];
    bool wait_event_matched[ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS];
    uint8_t wait_event_count;
    gm_room_scenario_flag_t wait_flags[ROOM_SCENARIO_WAIT_FLAGS_MAX_FLAGS];
    uint8_t wait_flag_count;
    char wait_operator_prompt[ROOM_SCENARIO_OPERATOR_PROMPT_MAX_LEN];
    char wait_operator_label[ROOM_SCENARIO_OPERATOR_LABEL_MAX_LEN];
    bool wait_operator_skip_allowed;
    char wait_operator_skip_label[ROOM_SCENARIO_OPERATOR_LABEL_MAX_LEN];
} gm_room_scenario_branch_runtime_t;

typedef struct {
    bool in_use;
    gm_session_state_t state;
    uint32_t generation;
    uint64_t started_at_ms;
    uint64_t finished_at_ms;
    char room_id[QUEST_ROOM_ID_MAX_LEN];
    char selected_profile_id[GM_GAME_PROFILE_ID_MAX_LEN];
    char selected_profile_name[GM_GAME_PROFILE_NAME_MAX_LEN];
    char selected_profile_scenario_id[GM_GAME_PROFILE_SCENARIO_ID_MAX_LEN];
    uint32_t selected_profile_duration_ms;
    char selected_scenario_id[ROOM_SCENARIO_ID_MAX_LEN];
    char selected_scenario_name[ROOM_SCENARIO_NAME_MAX_LEN];
    uint32_t selected_scenario_generation;
    room_scenario_t running_scenario;
    bool running_scenario_valid;
    uint32_t running_scenario_generation;
    gm_room_scenario_state_t scenario_state;
    uint16_t current_step_index;
    gm_room_scenario_wait_type_t wait_type;
    uint32_t wait_until_ms;
    uint32_t wait_started_at_ms;
    char wait_event_type[ROOM_SCENARIO_EVENT_TYPE_MAX_LEN];
    char wait_source_id[ROOM_SCENARIO_EVENT_SOURCE_ID_MAX_LEN];
    gm_room_scenario_wait_event_match_t wait_events[ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS];
    bool wait_event_matched[ROOM_SCENARIO_WAIT_EVENT_GROUP_MAX_EVENTS];
    uint8_t wait_event_count;
    gm_room_scenario_flag_t wait_flags[ROOM_SCENARIO_WAIT_FLAGS_MAX_FLAGS];
    uint8_t wait_flag_count;
    char wait_operator_prompt[ROOM_SCENARIO_OPERATOR_PROMPT_MAX_LEN];
    char wait_operator_label[ROOM_SCENARIO_OPERATOR_LABEL_MAX_LEN];
    bool wait_operator_skip_allowed;
    char wait_operator_skip_label[ROOM_SCENARIO_OPERATOR_LABEL_MAX_LEN];
    char scenario_operator_message[ROOM_SCENARIO_OPERATOR_MESSAGE_MAX_LEN];
    gm_room_scenario_flag_t scenario_flags[GM_ROOM_SCENARIO_MAX_FLAGS];
    uint8_t scenario_flag_count;
    gm_room_scenario_branch_runtime_t branch_runtimes[ROOM_SCENARIO_MAX_BRANCHES];
    uint8_t branch_runtime_count;
    char scenario_last_error[96];
    gm_timer_t timer;
    gm_hint_state_t hint;
} gm_room_session_t;

esp_err_t gm_room_session_init(void);
void gm_room_session_reset_all(void);
uint32_t gm_room_session_generation(void);
esp_err_t gm_room_session_get(const char *room_id, gm_room_session_t *out_session);
esp_err_t gm_room_session_start(const char *room_id, uint32_t duration_ms, uint64_t now_ms);
esp_err_t gm_room_session_pause(const char *room_id, uint64_t now_ms);
esp_err_t gm_room_session_resume(const char *room_id, uint64_t now_ms);
esp_err_t gm_room_session_finish(const char *room_id, uint64_t now_ms);
esp_err_t gm_room_session_reset(const char *room_id, uint32_t duration_ms, uint64_t now_ms);
esp_err_t gm_room_session_add_time(const char *room_id, int32_t delta_ms, uint64_t now_ms);
esp_err_t gm_room_session_set_hint(const char *room_id, const char *message, uint64_t now_ms);
esp_err_t gm_room_session_clear_hint(const char *room_id, uint64_t now_ms);
esp_err_t gm_room_session_select_profile(const char *room_id, const char *profile_id);
esp_err_t gm_room_session_select_scenario(const char *room_id, const char *scenario_id);
esp_err_t gm_room_session_game_start(const char *room_id, uint64_t now_ms);
esp_err_t gm_room_session_game_stop(const char *room_id, uint64_t now_ms);
esp_err_t gm_room_session_game_reset(const char *room_id, uint64_t now_ms);
esp_err_t gm_room_session_get_selected_scenario(const char *room_id,
                                                char *out_id,
                                                size_t out_id_size,
                                                char *out_name,
                                                size_t out_name_size);
esp_err_t gm_room_session_scenario_start(const char *room_id);
esp_err_t gm_room_session_scenario_stop(const char *room_id);
esp_err_t gm_room_session_scenario_next(const char *room_id);
esp_err_t gm_room_session_scenario_next_branch(const char *room_id, const char *branch_id);
esp_err_t gm_room_session_scenario_approve(const char *room_id);
esp_err_t gm_room_session_scenario_reset(const char *room_id);
esp_err_t gm_room_session_execute_device_command(const char *device_id,
                                                 const char *command_id,
                                                 const char *params_json);
void gm_room_session_scenario_tick(void);
esp_err_t gm_room_session_scenario_on_event(const event_bus_message_t *message);

#ifdef __cplusplus
}
#endif
