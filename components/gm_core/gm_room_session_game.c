#include "gm_room_session.h"
#include "gm_room_session_internal.h"

#include <string.h>

#include "esp_attr.h"
#include "gm_game_profile.h"
#include "hardware_io.h"
#include "quest_common_utils.h"
#include "room_scenario.h"
#include "service_status.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static EXT_RAM_BSS_ATTR gm_room_session_t s_game_session;
static EXT_RAM_BSS_ATTR room_scenario_t s_game_scenario;
static EXT_RAM_BSS_ATTR room_scenario_validation_report_t s_game_report;
static SemaphoreHandle_t s_game_scratch_mutex = NULL;
static StaticSemaphore_t s_game_scratch_mutex_storage;
static portMUX_TYPE s_game_scratch_mutex_init_lock = portMUX_INITIALIZER_UNLOCKED;

static esp_err_t game_scratch_lock(void)
{
    if (!s_game_scratch_mutex) {
        portENTER_CRITICAL(&s_game_scratch_mutex_init_lock);
        if (!s_game_scratch_mutex) {
            s_game_scratch_mutex = xSemaphoreCreateMutexStatic(&s_game_scratch_mutex_storage);
        }
        portEXIT_CRITICAL(&s_game_scratch_mutex_init_lock);
        if (!s_game_scratch_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }
    return (xSemaphoreTake(s_game_scratch_mutex, portMAX_DELAY) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void game_scratch_unlock(void)
{
    if (s_game_scratch_mutex) {
        xSemaphoreGive(s_game_scratch_mutex);
    }
}

static esp_err_t game_safe_off_hardware_optional(void)
{
    esp_err_t err = ESP_OK;
    if (!hardware_io_is_available()) {
        return ESP_OK;
    }
    err = hardware_io_safe_off_all();
    service_status_mark_fault(SERVICE_STATUS_HARDWARE_IO, err);
    return err;
}

esp_err_t gm_room_session_select_profile(const char *room_id, const char *profile_id)
{
    gm_game_profile_t profile = {0};
    char scenario_name[ROOM_SCENARIO_NAME_MAX_LEN] = {0};
    gm_room_session_t *session = NULL;
    esp_err_t err = ESP_OK;
    if (!room_id || !room_id[0] || !profile_id || !profile_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    err = gm_game_profile_get(profile_id, &profile);
    if (err != ESP_OK) {
        return err;
    }
    if (strcmp(profile.room_id, room_id) != 0) {
        return ESP_ERR_INVALID_STATE;
    }
    err = gm_game_profile_validate_reference(&profile);
    if (err != ESP_OK) {
        return err;
    }
    err = room_scenario_get_name_in_room(profile.scenario_id,
                                         room_id,
                                         scenario_name,
                                         sizeof(scenario_name));
    if (err != ESP_OK) {
        return err;
    }

    err = gm_room_session_sessions_lock();
    if (err != ESP_OK) {
        return err;
    }
    session = alloc_session_locked(room_id);
    if (!session) {
        gm_room_session_sessions_unlock();
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
                   profile.scenario_id);
    quest_str_copy(session->selected_scenario_name,
                   sizeof(session->selected_scenario_name),
                   scenario_name);
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
    return ESP_OK;
}

esp_err_t gm_room_session_select_scenario(const char *room_id, const char *scenario_id)
{
    room_scenario_t *scenario = &s_game_scenario;
    gm_room_session_t *session = NULL;
    esp_err_t err = ESP_OK;
    if (!room_id || !room_id[0] || !scenario_id || !scenario_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    err = game_scratch_lock();
    if (err != ESP_OK) return err;
    memset(scenario, 0, sizeof(*scenario));
    err = room_scenario_get(scenario_id, scenario);
    if (err != ESP_OK) {
        goto cleanup;
    }
    if (strcmp(scenario->room_id, room_id) != 0) {
        err = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }
    err = gm_room_session_sessions_lock();
    if (err != ESP_OK) {
        goto cleanup;
    }
    session = alloc_session_locked(room_id);
    if (!session) {
        gm_room_session_sessions_unlock();
        err = ESP_ERR_NO_MEM;
        goto cleanup;
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
cleanup:
    game_scratch_unlock();
    return err;
}

esp_err_t gm_room_session_get_selected_scenario(const char *room_id,
                                                char *out_id,
                                                size_t out_id_size,
                                                char *out_name,
                                                size_t out_name_size)
{
    gm_room_session_t *session = &s_game_session;
    esp_err_t err = ESP_OK;
    if (!room_id || !room_id[0] || !out_id || out_id_size == 0 || !out_name || out_name_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    out_id[0] = '\0';
    out_name[0] = '\0';
    err = game_scratch_lock();
    if (err != ESP_OK) return err;
    memset(session, 0, sizeof(*session));
    err = gm_room_session_get(room_id, session);
    if (err != ESP_OK) {
        game_scratch_unlock();
        return err;
    }
    quest_str_copy(out_id, out_id_size, session->selected_scenario_id);
    quest_str_copy(out_name, out_name_size, session->selected_scenario_name);
    err = session->selected_scenario_id[0] ? ESP_OK : ESP_ERR_NOT_FOUND;
    game_scratch_unlock();
    return err;
}

esp_err_t gm_room_session_game_start(const char *room_id, uint64_t now_ms)
{
    gm_room_session_t *session = &s_game_session;
    gm_game_profile_t profile = {0};
    room_scenario_t *scenario = &s_game_scenario;
    room_scenario_validation_report_t *report = &s_game_report;
    char profile_id[GM_GAME_PROFILE_ID_MAX_LEN] = {0};
    uint32_t duration_ms = 0;
    esp_err_t err = ESP_OK;
    if (!room_id || !room_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    err = game_scratch_lock();
    if (err != ESP_OK) return err;
    memset(session, 0, sizeof(*session));
    memset(scenario, 0, sizeof(*scenario));
    memset(report, 0, sizeof(*report));
    err = gm_room_session_get(room_id, session);
    if (err != ESP_OK) {
        goto cleanup;
    }
    if (!session->selected_profile_id[0]) {
        err = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }
    err = gm_game_profile_get(session->selected_profile_id, &profile);
    if (err != ESP_OK) {
        goto cleanup;
    }
    if (strcmp(profile.room_id, room_id) != 0) {
        err = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }
    err = gm_game_profile_validate_reference(&profile);
    if (err != ESP_OK) {
        goto cleanup;
    }
    err = room_scenario_get(profile.scenario_id, scenario);
    if (err != ESP_OK) {
        goto cleanup;
    }
    err = room_scenario_validate(scenario, report);
    if (err != ESP_OK || !report->valid) {
        err = err != ESP_OK ? err : ESP_ERR_INVALID_ARG;
        goto cleanup;
    }
    quest_str_copy(profile_id, sizeof(profile_id), profile.id);
    duration_ms = profile.duration_ms;
cleanup:
    game_scratch_unlock();
    if (err != ESP_OK) {
        return err;
    }
    err = gm_room_session_select_profile(room_id, profile_id);
    if (err != ESP_OK) {
        return err;
    }
    err = gm_room_session_start(room_id, duration_ms, now_ms);
    if (err != ESP_OK) {
        return err;
    }
    return gm_room_session_scenario_start(room_id);
}

esp_err_t gm_room_session_game_stop(const char *room_id, uint64_t now_ms)
{
    esp_err_t err = ESP_OK;
    esp_err_t safe_err = ESP_OK;
    if (!room_id || !room_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    err = gm_room_session_scenario_stop(room_id);
    if (err != ESP_OK) {
        return err;
    }
    err = gm_room_session_finish(room_id, now_ms);
    if (err != ESP_OK) {
        return err;
    }
    safe_err = game_safe_off_hardware_optional();
    return safe_err == ESP_OK ? ESP_OK : safe_err;
}

esp_err_t gm_room_session_game_reset(const char *room_id, uint64_t now_ms)
{
    gm_room_session_t *session = &s_game_session;
    char profile_id[GM_GAME_PROFILE_ID_MAX_LEN] = {0};
    uint32_t duration_ms = 0;
    esp_err_t err = ESP_OK;
    if (!room_id || !room_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    err = game_scratch_lock();
    if (err != ESP_OK) return err;
    memset(session, 0, sizeof(*session));
    err = gm_room_session_get(room_id, session);
    if (err != ESP_OK) {
        game_scratch_unlock();
        return err;
    }
    if (session->selected_profile_id[0]) {
        quest_str_copy(profile_id, sizeof(profile_id), session->selected_profile_id);
        game_scratch_unlock();
        err = gm_room_session_select_profile(room_id, profile_id);
        if (err != ESP_OK) {
            return err;
        }
        err = game_scratch_lock();
        if (err != ESP_OK) return err;
        memset(session, 0, sizeof(*session));
        err = gm_room_session_get(room_id, session);
        if (err != ESP_OK) {
            game_scratch_unlock();
            return err;
        }
        duration_ms = session->timer.duration_ms;
        game_scratch_unlock();
        gm_room_session_stop_audio();
        err = gm_room_session_reset(room_id, duration_ms, now_ms);
        if (err != ESP_OK) {
            return err;
        }
        return game_safe_off_hardware_optional();
    }
    duration_ms = session->timer.duration_ms;
    game_scratch_unlock();
    err = gm_room_session_scenario_reset(room_id);
    if (err != ESP_OK) {
        return err;
    }
    gm_room_session_stop_audio();
    err = gm_room_session_reset(room_id, duration_ms, now_ms);
    if (err != ESP_OK) {
        return err;
    }
    return game_safe_off_hardware_optional();
}
