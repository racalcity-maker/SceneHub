#include "gm_room_session.h"
#include "gm_room_session_internal.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "gm_game_profile.h"
#include "quest_common_utils.h"
#include "room_scenario.h"

esp_err_t gm_room_session_select_profile(const char *room_id, const char *profile_id)
{
    gm_game_profile_t profile = {0};
    room_scenario_t *scenario = NULL;
    gm_room_session_t *session = NULL;
    esp_err_t err = ESP_OK;
    if (!room_id || !room_id[0] || !profile_id || !profile_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    scenario = gm_room_session_heap_alloc(sizeof(*scenario));
    if (!scenario) {
        return ESP_ERR_NO_MEM;
    }
    err = gm_game_profile_get(profile_id, &profile);
    if (err != ESP_OK) {
        heap_caps_free(scenario);
        return err;
    }
    if (strcmp(profile.room_id, room_id) != 0) {
        heap_caps_free(scenario);
        return ESP_ERR_INVALID_STATE;
    }
    err = gm_game_profile_validate(&profile);
    if (err != ESP_OK) {
        heap_caps_free(scenario);
        return err;
    }
    err = room_scenario_get(profile.scenario_id, scenario);
    if (err != ESP_OK) {
        heap_caps_free(scenario);
        return err;
    }
    if (strcmp(scenario->room_id, room_id) != 0) {
        heap_caps_free(scenario);
        return ESP_ERR_INVALID_STATE;
    }

    err = gm_room_session_sessions_lock();
    if (err != ESP_OK) {
        heap_caps_free(scenario);
        return err;
    }
    session = alloc_session_locked(room_id);
    if (!session) {
        gm_room_session_sessions_unlock();
        heap_caps_free(scenario);
        return ESP_ERR_NO_MEM;
    }
    quest_str_copy(session->selected_profile_id,
                   sizeof(session->selected_profile_id),
                   profile.id);
    quest_str_copy(session->selected_profile_name,
                   sizeof(session->selected_profile_name),
                   profile.name);
    quest_str_copy(session->selected_profile_scenario_id,
                   sizeof(session->selected_profile_scenario_id),
                   profile.scenario_id);
    session->selected_profile_duration_ms = profile.duration_ms;
    quest_str_copy(session->selected_scenario_id,
                   sizeof(session->selected_scenario_id),
                   scenario->id);
    quest_str_copy(session->selected_scenario_name,
                   sizeof(session->selected_scenario_name),
                   scenario->name);
    session->selected_scenario_generation++;
    gm_timer_reset(&session->timer, profile.duration_ms, gm_room_session_now_ms());
    session->state = GM_SESSION_IDLE;
    session->started_at_ms = 0;
    session->finished_at_ms = 0;
    session->scenario_state = GM_ROOM_SCENARIO_IDLE;
    session->current_step_index = 0;
    gm_room_session_scenario_clear_wait_locked(session);
    scenario_clear_running_snapshot_locked(session);
    session->scenario_operator_message[0] = '\0';
    scenario_clear_flags_locked(session);
    session->scenario_last_error[0] = '\0';
    gm_room_session_mark_session_changed_locked(session);
    gm_room_session_sessions_unlock();
    heap_caps_free(scenario);
    return ESP_OK;
}

esp_err_t gm_room_session_select_scenario(const char *room_id, const char *scenario_id)
{
    room_scenario_t *scenario = NULL;
    gm_room_session_t *session = NULL;
    esp_err_t err = ESP_OK;
    if (!room_id || !room_id[0] || !scenario_id || !scenario_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    scenario = gm_room_session_heap_alloc(sizeof(*scenario));
    if (!scenario) {
        return ESP_ERR_NO_MEM;
    }
    err = room_scenario_get(scenario_id, scenario);
    if (err != ESP_OK) {
        heap_caps_free(scenario);
        return err;
    }
    if (strcmp(scenario->room_id, room_id) != 0) {
        heap_caps_free(scenario);
        return ESP_ERR_INVALID_STATE;
    }
    err = gm_room_session_sessions_lock();
    if (err != ESP_OK) {
        heap_caps_free(scenario);
        return err;
    }
    session = alloc_session_locked(room_id);
    if (!session) {
        gm_room_session_sessions_unlock();
        heap_caps_free(scenario);
        return ESP_ERR_NO_MEM;
    }
    session->selected_profile_id[0] = '\0';
    session->selected_profile_name[0] = '\0';
    session->selected_profile_scenario_id[0] = '\0';
    session->selected_profile_duration_ms = 0;
    quest_str_copy(session->selected_scenario_id,
                   sizeof(session->selected_scenario_id),
                   scenario->id);
    quest_str_copy(session->selected_scenario_name,
                   sizeof(session->selected_scenario_name),
                   scenario->name);
    session->selected_scenario_generation++;
    session->scenario_state = GM_ROOM_SCENARIO_IDLE;
    session->current_step_index = 0;
    gm_room_session_scenario_clear_wait_locked(session);
    scenario_clear_running_snapshot_locked(session);
    session->scenario_operator_message[0] = '\0';
    scenario_clear_flags_locked(session);
    session->scenario_last_error[0] = '\0';
    gm_room_session_mark_session_changed_locked(session);
    gm_room_session_sessions_unlock();
    heap_caps_free(scenario);
    return ESP_OK;
}

esp_err_t gm_room_session_get_selected_scenario(const char *room_id,
                                                char *out_id,
                                                size_t out_id_size,
                                                char *out_name,
                                                size_t out_name_size)
{
    gm_room_session_t *session = NULL;
    esp_err_t err = ESP_OK;
    if (!room_id || !room_id[0] || !out_id || out_id_size == 0 || !out_name || out_name_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    out_id[0] = '\0';
    out_name[0] = '\0';
    session = gm_room_session_heap_alloc(sizeof(*session));
    if (!session) {
        return ESP_ERR_NO_MEM;
    }
    err = gm_room_session_get(room_id, session);
    if (err != ESP_OK) {
        heap_caps_free(session);
        return err;
    }
    quest_str_copy(out_id, out_id_size, session->selected_scenario_id);
    quest_str_copy(out_name, out_name_size, session->selected_scenario_name);
    err = session->selected_scenario_id[0] ? ESP_OK : ESP_ERR_NOT_FOUND;
    heap_caps_free(session);
    return err;
}

esp_err_t gm_room_session_game_start(const char *room_id, uint64_t now_ms)
{
    gm_room_session_t *session = NULL;
    gm_game_profile_t profile = {0};
    room_scenario_t *scenario = NULL;
    room_scenario_validation_report_t *report = NULL;
    esp_err_t err = ESP_OK;
    if (!room_id || !room_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    session = gm_room_session_heap_alloc(sizeof(*session));
    scenario = gm_room_session_heap_alloc(sizeof(*scenario));
    report = gm_room_session_heap_alloc(sizeof(*report));
    if (!session || !scenario || !report) {
        heap_caps_free(session);
        heap_caps_free(scenario);
        heap_caps_free(report);
        return ESP_ERR_NO_MEM;
    }
    err = gm_room_session_get(room_id, session);
    if (err != ESP_OK) {
        heap_caps_free(session);
        heap_caps_free(scenario);
        heap_caps_free(report);
        return err;
    }
    if (!session->selected_profile_id[0]) {
        heap_caps_free(session);
        heap_caps_free(scenario);
        heap_caps_free(report);
        return ESP_ERR_INVALID_STATE;
    }
    err = gm_game_profile_get(session->selected_profile_id, &profile);
    if (err != ESP_OK) {
        heap_caps_free(session);
        heap_caps_free(scenario);
        heap_caps_free(report);
        return err;
    }
    if (strcmp(profile.room_id, room_id) != 0) {
        heap_caps_free(session);
        heap_caps_free(scenario);
        heap_caps_free(report);
        return ESP_ERR_INVALID_STATE;
    }
    err = gm_game_profile_validate(&profile);
    if (err != ESP_OK) {
        heap_caps_free(session);
        heap_caps_free(scenario);
        heap_caps_free(report);
        return err;
    }
    err = room_scenario_get(profile.scenario_id, scenario);
    if (err != ESP_OK) {
        heap_caps_free(session);
        heap_caps_free(scenario);
        heap_caps_free(report);
        return err;
    }
    err = room_scenario_validate(scenario, report);
    if (err != ESP_OK || !report->valid) {
        err = err != ESP_OK ? err : ESP_ERR_INVALID_ARG;
        heap_caps_free(session);
        heap_caps_free(scenario);
        heap_caps_free(report);
        return err;
    }
    heap_caps_free(session);
    heap_caps_free(scenario);
    heap_caps_free(report);
    err = gm_room_session_select_profile(room_id, profile.id);
    if (err != ESP_OK) {
        return err;
    }
    err = gm_room_session_start(room_id, profile.duration_ms, now_ms);
    if (err != ESP_OK) {
        return err;
    }
    return gm_room_session_scenario_start(room_id);
}

esp_err_t gm_room_session_game_stop(const char *room_id, uint64_t now_ms)
{
    esp_err_t err = ESP_OK;
    if (!room_id || !room_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    err = gm_room_session_scenario_stop(room_id);
    if (err != ESP_OK) {
        return err;
    }
    return gm_room_session_finish(room_id, now_ms);
}

esp_err_t gm_room_session_game_reset(const char *room_id, uint64_t now_ms)
{
    gm_room_session_t *session = NULL;
    char profile_id[GM_GAME_PROFILE_ID_MAX_LEN] = {0};
    uint32_t duration_ms = 0;
    esp_err_t err = ESP_OK;
    if (!room_id || !room_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    session = gm_room_session_heap_alloc(sizeof(*session));
    if (!session) {
        return ESP_ERR_NO_MEM;
    }
    err = gm_room_session_get(room_id, session);
    if (err != ESP_OK) {
        heap_caps_free(session);
        return err;
    }
    if (session->selected_profile_id[0]) {
        quest_str_copy(profile_id, sizeof(profile_id), session->selected_profile_id);
        err = gm_room_session_select_profile(room_id, profile_id);
        if (err != ESP_OK) {
            heap_caps_free(session);
            return err;
        }
        memset(session, 0, sizeof(*session));
        err = gm_room_session_get(room_id, session);
        if (err != ESP_OK) {
            heap_caps_free(session);
            return err;
        }
        duration_ms = session->timer.duration_ms;
        heap_caps_free(session);
        gm_room_session_stop_audio();
        return gm_room_session_reset(room_id, duration_ms, now_ms);
    }
    duration_ms = session->timer.duration_ms;
    heap_caps_free(session);
    err = gm_room_session_scenario_reset(room_id);
    if (err != ESP_OK) {
        return err;
    }
    gm_room_session_stop_audio();
    return gm_room_session_reset(room_id, duration_ms, now_ms);
}
