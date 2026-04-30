#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "gm_room_session.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char room_id[QUEST_ROOM_ID_MAX_LEN];
    bool exists;
    bool session_present;
    bool session_active;
    gm_session_state_t session_state;
    gm_timer_state_t timer_state;
    uint32_t duration_ms;
    uint32_t remaining_ms;
    uint64_t started_at_ms;
    uint64_t paused_at_ms;
    bool hint_active;
    uint32_t hint_count;
    uint64_t hint_updated_at_ms;
    char hint_text[QUEST_PAYLOAD_MAX_LEN];
    char selected_profile_id[GM_GAME_PROFILE_ID_MAX_LEN];
    char selected_profile_name[GM_GAME_PROFILE_NAME_MAX_LEN];
    char selected_profile_scenario_id[GM_GAME_PROFILE_SCENARIO_ID_MAX_LEN];
    uint32_t selected_profile_duration_ms;
    char selected_scenario_id[ROOM_SCENARIO_ID_MAX_LEN];
    char selected_scenario_name[ROOM_SCENARIO_NAME_MAX_LEN];
    char running_scenario_id[ROOM_SCENARIO_ID_MAX_LEN];
    char running_scenario_name[ROOM_SCENARIO_NAME_MAX_LEN];
    uint32_t running_scenario_generation;
    gm_room_scenario_state_t scenario_runtime_state;
    uint16_t scenario_current_step_index;
    gm_room_scenario_wait_type_t scenario_wait_type;
    uint32_t scenario_wait_until_ms;
    uint32_t scenario_wait_started_at_ms;
    char scenario_wait_event_type[ROOM_SCENARIO_EVENT_TYPE_MAX_LEN];
    char scenario_wait_source_id[ROOM_SCENARIO_EVENT_SOURCE_ID_MAX_LEN];
    char scenario_wait_operator_prompt[ROOM_SCENARIO_OPERATOR_PROMPT_MAX_LEN];
    char scenario_wait_operator_label[ROOM_SCENARIO_OPERATOR_LABEL_MAX_LEN];
    bool scenario_wait_operator_skip_allowed;
    char scenario_wait_operator_skip_label[ROOM_SCENARIO_OPERATOR_LABEL_MAX_LEN];
    char scenario_operator_message[ROOM_SCENARIO_OPERATOR_MESSAGE_MAX_LEN];
    gm_room_scenario_flag_t scenario_flags[GM_ROOM_SCENARIO_MAX_FLAGS];
    uint8_t scenario_flag_count;
    char scenario_last_error[96];
} gm_room_state_view_t;

esp_err_t gm_api_get_room_state(const char *room_id, gm_room_state_view_t *out);
esp_err_t gm_api_room_session_get(const char *room_id, gm_room_session_t *out_session);
esp_err_t gm_api_room_session_finish(const char *room_id);
esp_err_t gm_api_timer_start(const char *room_id, uint32_t duration_ms);
esp_err_t gm_api_timer_pause(const char *room_id);
esp_err_t gm_api_timer_resume(const char *room_id);
esp_err_t gm_api_timer_reset(const char *room_id, bool has_duration, uint32_t duration_ms);
esp_err_t gm_api_timer_add(const char *room_id, int32_t delta_ms);
esp_err_t gm_api_hint_send(const char *room_id, const char *message);
esp_err_t gm_api_hint_clear(const char *room_id);
esp_err_t gm_api_select_profile(const char *room_id, const char *profile_id);
esp_err_t gm_api_select_scenario(const char *room_id, const char *scenario_id);
esp_err_t gm_api_game_start(const char *room_id);
esp_err_t gm_api_game_stop(const char *room_id);
esp_err_t gm_api_game_reset(const char *room_id);
esp_err_t gm_api_scenario_start(const char *room_id);
esp_err_t gm_api_scenario_stop(const char *room_id);
esp_err_t gm_api_scenario_next(const char *room_id);
esp_err_t gm_api_scenario_next_branch(const char *room_id, const char *branch_id);
esp_err_t gm_api_scenario_approve(const char *room_id);
esp_err_t gm_api_scenario_reset(const char *room_id);
esp_err_t gm_api_device_command_run(const char *device_id,
                                    const char *command_id,
                                    const char *params_json);

#ifdef __cplusplus
}
#endif
