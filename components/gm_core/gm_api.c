#include "gm_api.h"

#include <string.h>

#include "audio_player.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "gm_game_profile.h"
#include "quest_common_utils.h"
#include "quest_device.h"
#include "room_catalog.h"
#include "room_scenario.h"

static void *gm_api_alloc(size_t size)
{
    void *ptr = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
        ptr = heap_caps_calloc(1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return ptr;
}

static uint64_t gm_api_now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static esp_err_t gm_api_require_room(const char *room_id)
{
    if (!room_id || !room_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    if (room_catalog_init() != ESP_OK) {
        return ESP_FAIL;
    }
    if (!room_catalog_exists(room_id)) {
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

static void gm_api_prepare_audio_args_json(const char *args_json)
{
    cJSON *root = NULL;
    cJSON *file = NULL;
    if (!args_json || !args_json[0]) {
        return;
    }
    root = cJSON_Parse(args_json);
    if (!root) {
        return;
    }
    file = cJSON_GetObjectItem(root, "file");
    if (cJSON_IsString(file) && file->valuestring && file->valuestring[0]) {
        (void)audio_player_prepare_path(file->valuestring, NULL);
    }
    cJSON_Delete(root);
}

static void gm_api_prepare_audio_command(const char *device_id,
                                         const char *command_id,
                                         const char *params_json)
{
    quest_device_command_t command = {0};
    if (!device_id || !command_id || strcmp(device_id, QUEST_DEVICE_SYSTEM_AUDIO_ID) != 0) {
        return;
    }
    if (quest_device_get_command(device_id, command_id, &command) != ESP_OK) {
        return;
    }
    if (strcmp(command.command, "audio.play") != 0) {
        return;
    }
    gm_api_prepare_audio_args_json(command.default_args_json);
    gm_api_prepare_audio_args_json(params_json);
}

static void gm_api_prepare_profile_assets(const char *profile_id)
{
    gm_game_profile_t profile = {0};
    room_scenario_t *scenario = NULL;
    if (!profile_id || !profile_id[0]) {
        return;
    }
    if (gm_game_profile_get(profile_id, &profile) != ESP_OK || !profile.scenario_id[0]) {
        return;
    }
    scenario = gm_api_alloc(sizeof(*scenario));
    if (!scenario) {
        return;
    }
    if (room_scenario_get(profile.scenario_id, scenario) != ESP_OK) {
        heap_caps_free(scenario);
        return;
    }
    for (size_t i = 0; i < scenario->step_count; ++i) {
        const room_scenario_step_t *step = &scenario->steps[i];
        if (!step->enabled) {
            continue;
        }
        if (step->type == ROOM_SCENARIO_STEP_DEVICE_COMMAND) {
            gm_api_prepare_audio_command(step->data.device_command.device_id,
                                         step->data.device_command.command_id,
                                         step->data.device_command.params_json);
        } else if (step->type == ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP) {
            for (uint8_t j = 0; j < step->data.device_command_group.command_count; ++j) {
                gm_api_prepare_audio_command(step->data.device_command_group.commands[j].device_id,
                                             step->data.device_command_group.commands[j].command_id,
                                             NULL);
            }
        }
    }
    heap_caps_free(scenario);
}

esp_err_t gm_api_get_room_state(const char *room_id, gm_room_state_view_t *out)
{
    gm_room_session_t *session = NULL;
    esp_err_t err;
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    err = gm_api_require_room(room_id);
    if (err != ESP_OK) {
        return err;
    }

    memset(out, 0, sizeof(*out));
    out->exists = true;
    out->session_state = GM_SESSION_IDLE;
    out->timer_state = GM_TIMER_IDLE;
    quest_str_copy(out->room_id, sizeof(out->room_id), room_id);

    session = gm_api_alloc(sizeof(*session));
    if (!session) {
        return ESP_ERR_NO_MEM;
    }
    err = gm_room_session_get(room_id, session);
    if (err == ESP_ERR_NOT_FOUND) {
        heap_caps_free(session);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        heap_caps_free(session);
        return err;
    }

    out->session_present = true;
    out->session_state = session->state;
    out->session_active =
        (session->state == GM_SESSION_RUNNING || session->state == GM_SESSION_PAUSED);
    out->timer_state = session->timer.state;
    out->duration_ms = session->timer.duration_ms;
    out->remaining_ms = gm_timer_get_remaining(&session->timer, gm_api_now_ms());
    out->started_at_ms = session->started_at_ms;
    out->paused_at_ms = session->timer.paused_at_ms;
    out->hint_active = session->hint.active;
    out->hint_count = session->hint.sent_count;
    out->hint_updated_at_ms = session->hint.last_changed_ms;
    quest_str_copy(out->hint_text, sizeof(out->hint_text), session->hint.message);
    quest_str_copy(out->selected_profile_id,
                sizeof(out->selected_profile_id),
                session->selected_profile_id);
    quest_str_copy(out->selected_profile_name,
                sizeof(out->selected_profile_name),
                session->selected_profile_name);
    quest_str_copy(out->selected_profile_scenario_id,
                sizeof(out->selected_profile_scenario_id),
                session->selected_profile_scenario_id);
    out->selected_profile_duration_ms = session->selected_profile_duration_ms;
    quest_str_copy(out->selected_scenario_id,
                sizeof(out->selected_scenario_id),
                session->selected_scenario_id);
    quest_str_copy(out->selected_scenario_name,
                sizeof(out->selected_scenario_name),
                session->selected_scenario_name);
    if (session->running_scenario_valid) {
        quest_str_copy(out->running_scenario_id,
                    sizeof(out->running_scenario_id),
                    session->running_scenario.id);
        quest_str_copy(out->running_scenario_name,
                    sizeof(out->running_scenario_name),
                    session->running_scenario.name);
        out->running_scenario_generation = session->running_scenario_generation;
    }
    out->scenario_runtime_state = session->scenario_state;
    out->scenario_current_step_index = session->current_step_index;
    out->scenario_wait_type = session->wait_type;
    out->scenario_wait_until_ms = session->wait_until_ms;
    out->scenario_wait_started_at_ms = session->wait_started_at_ms;
    quest_str_copy(out->scenario_wait_event_type,
                sizeof(out->scenario_wait_event_type),
                session->wait_event_type);
    quest_str_copy(out->scenario_wait_source_id,
                sizeof(out->scenario_wait_source_id),
                session->wait_source_id);
    quest_str_copy(out->scenario_wait_operator_prompt,
                sizeof(out->scenario_wait_operator_prompt),
                session->wait_operator_prompt);
    quest_str_copy(out->scenario_wait_operator_label,
                sizeof(out->scenario_wait_operator_label),
                session->wait_operator_label);
    out->scenario_wait_operator_skip_allowed = session->wait_operator_skip_allowed;
    quest_str_copy(out->scenario_wait_operator_skip_label,
                sizeof(out->scenario_wait_operator_skip_label),
                session->wait_operator_skip_label);
    quest_str_copy(out->scenario_operator_message,
                sizeof(out->scenario_operator_message),
                session->scenario_operator_message);
    out->scenario_flag_count = session->scenario_flag_count;
    memcpy(out->scenario_flags,
           session->scenario_flags,
           sizeof(out->scenario_flags));
    quest_str_copy(out->scenario_last_error,
                sizeof(out->scenario_last_error),
                session->scenario_last_error);
    heap_caps_free(session);
    return ESP_OK;
}

esp_err_t gm_api_room_session_get(const char *room_id, gm_room_session_t *out_session)
{
    esp_err_t err = gm_api_require_room(room_id);
    if (err != ESP_OK) {
        return err;
    }
    return gm_room_session_get(room_id, out_session);
}

esp_err_t gm_api_room_session_finish(const char *room_id)
{
    esp_err_t err = gm_api_require_room(room_id);
    if (err != ESP_OK) {
        return err;
    }
    return gm_room_session_finish(room_id, gm_api_now_ms());
}

esp_err_t gm_api_timer_start(const char *room_id, uint32_t duration_ms)
{
    esp_err_t err = gm_api_require_room(room_id);
    if (err != ESP_OK) {
        return err;
    }
    return gm_room_session_start(room_id, duration_ms, gm_api_now_ms());
}

esp_err_t gm_api_timer_pause(const char *room_id)
{
    esp_err_t err = gm_api_require_room(room_id);
    if (err != ESP_OK) {
        return err;
    }
    return gm_room_session_pause(room_id, gm_api_now_ms());
}

esp_err_t gm_api_timer_resume(const char *room_id)
{
    esp_err_t err = gm_api_require_room(room_id);
    if (err != ESP_OK) {
        return err;
    }
    return gm_room_session_resume(room_id, gm_api_now_ms());
}

esp_err_t gm_api_timer_reset(const char *room_id, bool has_duration, uint32_t duration_ms)
{
    gm_room_session_t *session = NULL;
    esp_err_t err = gm_api_require_room(room_id);
    if (err != ESP_OK) {
        return err;
    }
    if (!has_duration) {
        session = gm_api_alloc(sizeof(*session));
        if (!session) {
            return ESP_ERR_NO_MEM;
        }
        err = gm_room_session_get(room_id, session);
        if (err != ESP_OK) {
            heap_caps_free(session);
            return err;
        }
        duration_ms = session->timer.duration_ms;
        heap_caps_free(session);
    }
    return gm_room_session_reset(room_id, duration_ms, gm_api_now_ms());
}

esp_err_t gm_api_timer_add(const char *room_id, int32_t delta_ms)
{
    esp_err_t err = gm_api_require_room(room_id);
    if (err != ESP_OK) {
        return err;
    }
    return gm_room_session_add_time(room_id, delta_ms, gm_api_now_ms());
}

esp_err_t gm_api_hint_send(const char *room_id, const char *message)
{
    esp_err_t err = gm_api_require_room(room_id);
    if (err != ESP_OK) {
        return err;
    }
    return gm_room_session_set_hint(room_id, message, gm_api_now_ms());
}

esp_err_t gm_api_hint_clear(const char *room_id)
{
    esp_err_t err = gm_api_require_room(room_id);
    if (err != ESP_OK) {
        return err;
    }
    return gm_room_session_clear_hint(room_id, gm_api_now_ms());
}

esp_err_t gm_api_select_profile(const char *room_id, const char *profile_id)
{
    esp_err_t err = gm_api_require_room(room_id);
    if (err != ESP_OK) {
        return err;
    }
    err = gm_room_session_select_profile(room_id, profile_id);
    if (err == ESP_OK) {
        gm_api_prepare_profile_assets(profile_id);
    }
    return err;
}

esp_err_t gm_api_select_scenario(const char *room_id, const char *scenario_id)
{
    esp_err_t err = gm_api_require_room(room_id);
    if (err != ESP_OK) {
        return err;
    }
    return gm_room_session_select_scenario(room_id, scenario_id);
}

esp_err_t gm_api_game_start(const char *room_id)
{
    esp_err_t err = gm_api_require_room(room_id);
    if (err != ESP_OK) {
        return err;
    }
    return gm_room_session_game_start(room_id, gm_api_now_ms());
}

esp_err_t gm_api_game_stop(const char *room_id)
{
    esp_err_t err = gm_api_require_room(room_id);
    if (err != ESP_OK) {
        return err;
    }
    return gm_room_session_game_stop(room_id, gm_api_now_ms());
}

esp_err_t gm_api_game_reset(const char *room_id)
{
    esp_err_t err = gm_api_require_room(room_id);
    if (err != ESP_OK) {
        return err;
    }
    return gm_room_session_game_reset(room_id, gm_api_now_ms());
}

esp_err_t gm_api_scenario_start(const char *room_id)
{
    esp_err_t err = gm_api_require_room(room_id);
    if (err != ESP_OK) {
        return err;
    }
    return gm_room_session_scenario_start(room_id);
}

esp_err_t gm_api_scenario_stop(const char *room_id)
{
    esp_err_t err = gm_api_require_room(room_id);
    if (err != ESP_OK) {
        return err;
    }
    return gm_room_session_scenario_stop(room_id);
}

esp_err_t gm_api_scenario_next(const char *room_id)
{
    esp_err_t err = gm_api_require_room(room_id);
    if (err != ESP_OK) {
        return err;
    }
    return gm_room_session_scenario_next(room_id);
}

esp_err_t gm_api_scenario_next_branch(const char *room_id, const char *branch_id)
{
    esp_err_t err = gm_api_require_room(room_id);
    if (err != ESP_OK) {
        return err;
    }
    return gm_room_session_scenario_next_branch(room_id, branch_id);
}

esp_err_t gm_api_scenario_approve(const char *room_id)
{
    esp_err_t err = gm_api_require_room(room_id);
    if (err != ESP_OK) {
        return err;
    }
    return gm_room_session_scenario_approve(room_id);
}

esp_err_t gm_api_scenario_reset(const char *room_id)
{
    esp_err_t err = gm_api_require_room(room_id);
    if (err != ESP_OK) {
        return err;
    }
    return gm_room_session_scenario_reset(room_id);
}

esp_err_t gm_api_device_command_run(const char *device_id,
                                    const char *command_id,
                                    const char *params_json)
{
    quest_device_command_t command = {0};
    esp_err_t err = quest_device_get_command(device_id, command_id, &command);
    if (err != ESP_OK) {
        return err;
    }
    if (!command.manual_allowed) {
        return ESP_ERR_INVALID_STATE;
    }
    return gm_room_session_execute_device_command(device_id, command_id, params_json);
}
