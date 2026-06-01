#include "gm_room_session.h"
#include "gm_room_session_commands_internal.h"
#include "gm_room_session_internal.h"

#include <string.h>

#include "esp_attr.h"
#include "quest_common_utils.h"
#include "room_scenario.h"
#include "scenehub_scenario_validation.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static EXT_RAM_BSS_ATTR room_scenario_validation_report_t s_game_report;
static SemaphoreHandle_t s_game_scratch_mutex = NULL;
static StaticSemaphore_t s_game_scratch_mutex_storage;
static portMUX_TYPE s_game_scratch_mutex_init_lock = portMUX_INITIALIZER_UNLOCKED;

static esp_err_t game_start_prepared_locked(const char *room_id,
                                            uint64_t now_ms,
                                            const gm_room_session_game_start_prepared_t *request,
                                            room_scenario_validation_report_t *report,
                                            gm_room_session_command_plan_t *out_plan);

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

esp_err_t gm_room_session_select_profile_prepared(const char *room_id,
                                                  const gm_room_session_profile_t *profile,
                                                  const room_scenario_t *scenario,
                                                  uint32_t duration_ms)
{
    gm_room_session_t *session = NULL;
    esp_err_t err = ESP_OK;

    if (!room_id || !room_id[0] || !profile || !scenario || duration_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(profile->room_id, room_id) != 0 ||
        strcmp(scenario->room_id, room_id) != 0 ||
        strcmp(profile->scenario_id, scenario->id) != 0) {
        return ESP_ERR_INVALID_STATE;
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
                   profile->id);
    quest_str_copy(session->selected_profile_name,
                   sizeof(session->selected_profile_name),
                   profile->name);
    quest_str_copy(session->selected_profile_scenario_id,
                   sizeof(session->selected_profile_scenario_id),
                   profile->scenario_id);
    session->selected_profile_duration_ms = duration_ms;
    quest_str_copy(session->selected_scenario_id,
                   sizeof(session->selected_scenario_id),
                   profile->scenario_id);
    quest_str_copy(session->selected_scenario_name,
                   sizeof(session->selected_scenario_name),
                   scenario->name);
    session->selected_scenario_generation++;
    gm_timer_reset(&session->timer, duration_ms, gm_room_session_now_ms());
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

esp_err_t gm_room_session_select_scenario_prepared(const char *room_id,
                                                   const room_scenario_t *scenario)
{
    gm_room_session_t *session = NULL;
    esp_err_t err = ESP_OK;
    if (!room_id || !room_id[0] || !scenario || !scenario->id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(scenario->room_id, room_id) != 0) {
        return ESP_ERR_INVALID_STATE;
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
    return ESP_OK;
}

static esp_err_t game_start_prepared_locked(const char *room_id,
                                            uint64_t now_ms,
                                            const gm_room_session_game_start_prepared_t *request,
                                            room_scenario_validation_report_t *report,
                                            gm_room_session_command_plan_t *out_plan)
{
    const gm_room_session_profile_t *profile = NULL;
    const room_scenario_t *scenario = NULL;
    gm_room_session_t *session = NULL;
    uint32_t duration_ms = 0;
    esp_err_t err = ESP_OK;

    if (!out_plan) {
        return ESP_ERR_INVALID_ARG;
    }
    gm_room_session_command_plan_clear(out_plan);
    if (!room_id || !room_id[0] || !request || !request->profile || !request->scenario ||
        !request->prepared_scenario) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!report) {
        return ESP_ERR_INVALID_ARG;
    }
    profile = request->profile;
    scenario = request->scenario;
    duration_ms = request->duration_ms ? request->duration_ms : profile->duration_ms;
    if (!profile->id[0] || !profile->name[0] || !profile->room_id[0] ||
        !profile->scenario_id[0] || duration_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(profile->room_id, room_id) != 0 ||
        strcmp(scenario->room_id, room_id) != 0 ||
        strcmp(profile->scenario_id, scenario->id) != 0) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(report, 0, sizeof(*report));
    err = scenehub_scenario_validate(scenario, report);
    if (err != ESP_OK || !report->valid) {
        esp_err_t lock_err = gm_room_session_sessions_lock();
        if (lock_err == ESP_OK) {
            session = find_session_mutable_locked(room_id);
            if (session) {
                scenario_clear_running_snapshot_locked(session);
                scenario_set_error_locked(session, scenario_validation_error_message(report));
            }
            gm_room_session_sessions_unlock();
        }
        err = err != ESP_OK ? err : ESP_ERR_INVALID_ARG;
    }
    if (err != ESP_OK) {
        return err;
    }

    err = gm_room_session_select_profile_prepared(room_id, profile, scenario, duration_ms);
    if (err != ESP_OK) {
        return err;
    }
    err = gm_room_session_start(room_id, duration_ms, now_ms);
    if (err != ESP_OK) {
        return err;
    }
    return gm_room_session_scenario_start_prepared_plan(room_id,
                                                        scenario,
                                                        request->prepared_scenario,
                                                        0,
                                                        out_plan);
}

esp_err_t gm_room_session_game_start_prepared(const char *room_id,
                                              uint64_t now_ms,
                                              const gm_room_session_game_start_prepared_t *request,
                                              gm_room_session_command_plan_t *out_plan)
{
    room_scenario_validation_report_t *report = &s_game_report;
    esp_err_t err = game_scratch_lock();

    if (err != ESP_OK) return err;
    err = game_start_prepared_locked(room_id, now_ms, request, report, out_plan);
    game_scratch_unlock();
    return err;
}
