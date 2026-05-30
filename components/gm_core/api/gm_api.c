#include "gm_api.h"

#include <string.h>

#include "audio_player.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "event_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "gm_game_profile.h"
#include "quest_common_utils.h"
#include "quest_device.h"
#include "room_catalog.h"
#include "room_scenario.h"
#include "scenehub_device_command_resolver.h"
#include "sd_storage.h"

static const char *TAG = "gm_api";

static EXT_RAM_BSS_ATTR room_scenario_t s_api_prepare_scenario;
static EXT_RAM_BSS_ATTR gm_game_profile_t s_api_prepare_profile;
static EXT_RAM_BSS_ATTR quest_device_command_t s_api_prepare_command;
static EXT_RAM_BSS_ATTR char s_api_prepare_profile_id[GM_GAME_PROFILE_ID_MAX_LEN];
static SemaphoreHandle_t s_api_prepare_mutex = NULL;
static StaticSemaphore_t s_api_prepare_mutex_storage;
static portMUX_TYPE s_api_prepare_mutex_init_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_api_prepare_generation = 0;
static bool s_api_prepare_job_active = false;

typedef struct {
    const char *value;
    size_t value_len;
} gm_api_json_string_view_t;

static esp_err_t gm_api_prepare_lock(void)
{
    if (!s_api_prepare_mutex) {
        portENTER_CRITICAL(&s_api_prepare_mutex_init_lock);
        if (!s_api_prepare_mutex) {
            s_api_prepare_mutex = xSemaphoreCreateMutexStatic(&s_api_prepare_mutex_storage);
        }
        portEXIT_CRITICAL(&s_api_prepare_mutex_init_lock);
        if (!s_api_prepare_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }
    return (xSemaphoreTake(s_api_prepare_mutex, portMAX_DELAY) == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void gm_api_prepare_unlock(void)
{
    if (s_api_prepare_mutex) {
        xSemaphoreGive(s_api_prepare_mutex);
    }
}

static uint64_t gm_api_now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static const char *gm_api_json_skip_ws(const char *p)
{
    while (p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
        ++p;
    }
    return p;
}

static bool gm_api_json_key_equals(const char *key, size_t key_len, const char *expected)
{
    return key && expected && strlen(expected) == key_len && strncmp(key, expected, key_len) == 0;
}

static esp_err_t gm_api_json_read_string_token(const char **cursor,
                                               const char **out_value,
                                               size_t *out_len)
{
    const char *p = gm_api_json_skip_ws(cursor ? *cursor : NULL);
    const char *start = NULL;

    if (!p || *p != '"' || !out_value || !out_len) {
        return ESP_ERR_INVALID_ARG;
    }
    ++p;
    start = p;
    while (*p) {
        if (*p == '\\') {
            ++p;
            if (!*p) {
                return ESP_ERR_INVALID_ARG;
            }
            ++p;
            continue;
        }
        if (*p == '"') {
            *out_value = start;
            *out_len = (size_t)(p - start);
            *cursor = p + 1;
            return ESP_OK;
        }
        ++p;
    }
    return ESP_ERR_INVALID_ARG;
}

static esp_err_t gm_api_json_skip_string(const char **cursor)
{
    const char *value = NULL;
    size_t value_len = 0;
    return gm_api_json_read_string_token(cursor, &value, &value_len);
}

static esp_err_t gm_api_json_skip_value(const char **cursor)
{
    const char *p = gm_api_json_skip_ws(cursor ? *cursor : NULL);
    int depth = 0;

    if (!p) {
        return ESP_ERR_INVALID_ARG;
    }
    if (*p == '"') {
        esp_err_t err = gm_api_json_skip_string(&p);
        if (err != ESP_OK) {
            return err;
        }
        *cursor = p;
        return ESP_OK;
    }
    if (*p == '{' || *p == '[') {
        char open = *p++;
        char close = open == '{' ? '}' : ']';
        depth = 1;
        while (*p && depth > 0) {
            if (*p == '"') {
                esp_err_t err = gm_api_json_skip_string(&p);
                if (err != ESP_OK) {
                    return err;
                }
                continue;
            }
            if (*p == open) {
                ++depth;
            } else if (*p == close) {
                --depth;
            }
            ++p;
        }
        if (depth != 0) {
            return ESP_ERR_INVALID_ARG;
        }
        *cursor = p;
        return ESP_OK;
    }
    while (*p && *p != ',' && *p != '}' && *p != ']') {
        ++p;
    }
    *cursor = p;
    return ESP_OK;
}

static esp_err_t gm_api_json_find_string_field(const char *json,
                                               const char *key,
                                               gm_api_json_string_view_t *out)
{
    const char *p = gm_api_json_skip_ws(json);

    if (!key || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (!p || !*p) {
        return ESP_ERR_NOT_FOUND;
    }
    if (*p != '{') {
        return ESP_ERR_INVALID_ARG;
    }
    ++p;
    for (;;) {
        const char *json_key = NULL;
        size_t json_key_len = 0;
        esp_err_t err = ESP_OK;

        p = gm_api_json_skip_ws(p);
        if (*p == '}') {
            return ESP_ERR_NOT_FOUND;
        }
        err = gm_api_json_read_string_token(&p, &json_key, &json_key_len);
        if (err != ESP_OK) {
            return err;
        }
        p = gm_api_json_skip_ws(p);
        if (*p != ':') {
            return ESP_ERR_INVALID_ARG;
        }
        ++p;
        p = gm_api_json_skip_ws(p);
        if (gm_api_json_key_equals(json_key, json_key_len, key)) {
            if (*p != '"') {
                return ESP_ERR_INVALID_ARG;
            }
            err = gm_api_json_read_string_token(&p, &out->value, &out->value_len);
            return err;
        }
        err = gm_api_json_skip_value(&p);
        if (err != ESP_OK) {
            return err;
        }
        p = gm_api_json_skip_ws(p);
        if (*p == ',') {
            ++p;
            continue;
        }
        if (*p == '}') {
            return ESP_ERR_NOT_FOUND;
        }
        return ESP_ERR_INVALID_ARG;
    }
}

static esp_err_t gm_api_json_copy_string_view(const gm_api_json_string_view_t *value,
                                              char *out,
                                              size_t out_size)
{
    const char *p = value ? value->value : NULL;
    const char *end = p ? p + value->value_len : NULL;
    size_t written = 0;

    if (!value || !out || out_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    out[0] = '\0';
    while (p < end) {
        char ch = *p++;
        if (ch == '\\') {
            if (p >= end) {
                return ESP_ERR_INVALID_ARG;
            }
            ch = *p++;
            switch (ch) {
            case '"':
            case '\\':
            case '/':
                break;
            case 'b':
                ch = '\b';
                break;
            case 'f':
                ch = '\f';
                break;
            case 'n':
                ch = '\n';
                break;
            case 'r':
                ch = '\r';
                break;
            case 't':
                ch = '\t';
                break;
            default:
                return ESP_ERR_NOT_SUPPORTED;
            }
        }
        if (written + 1 >= out_size) {
            return ESP_ERR_INVALID_SIZE;
        }
        out[written++] = ch;
    }
    out[written] = '\0';
    return written > 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
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
    gm_api_json_string_view_t file = {0};
    char path[QUEST_DEVICE_DEFAULT_ARGS_JSON_MAX_LEN] = {0};
    uint32_t started_ms = 0;
    esp_err_t err = ESP_OK;
    char detail[64] = {0};
    if (!args_json || !args_json[0]) {
        return;
    }
    if (gm_api_json_find_string_field(args_json, "file", &file) != ESP_OK) {
        return;
    }
    if (gm_api_json_copy_string_view(&file, path, sizeof(path)) == ESP_OK && path[0]) {
        started_ms = sd_storage_trace_now_ms();
        err = audio_player_prepare_path(path, NULL);
        snprintf(detail, sizeof(detail), "result=%s", esp_err_to_name(err));
        sd_storage_trace_log("gm_api", "warmup_prepare", path, sd_storage_trace_now_ms() - started_ms, detail);
    }
}

static void gm_api_prepare_audio_command_fields(const char *device_id,
                                                const char *command_id,
                                                const char *params_json)
{
    char command_name[QUEST_DEVICE_COMMAND_NAME_MAX_LEN] = {0};
    char default_args_json[QUEST_DEVICE_DEFAULT_ARGS_JSON_MAX_LEN] = {0};
    esp_err_t err = ESP_OK;
    if (!device_id || strcmp(device_id, QUEST_DEVICE_SYSTEM_AUDIO_ID) != 0 ||
        !command_id || !command_id[0]) {
        return;
    }
    if (gm_api_prepare_lock() != ESP_OK) {
        return;
    }
    memset(&s_api_prepare_command, 0, sizeof(s_api_prepare_command));
    err = quest_device_get_command(device_id, command_id, &s_api_prepare_command);
    if (err == ESP_OK) {
        quest_str_copy(command_name, sizeof(command_name), s_api_prepare_command.command);
        quest_str_copy(default_args_json, sizeof(default_args_json), s_api_prepare_command.default_args_json);
    }
    gm_api_prepare_unlock();
    if (err != ESP_OK) {
        return;
    }
    if (strcmp(command_name, "audio.play") != 0) {
        return;
    }
    gm_api_prepare_audio_args_json(default_args_json);
    gm_api_prepare_audio_args_json(params_json);
}

static void gm_api_prepare_audio_command(const room_scenario_device_command_t *command_ref)
{
    if (!command_ref) {
        return;
    }
    gm_api_prepare_audio_command_fields(command_ref->device_id,
                                        command_ref->command_id,
                                        command_ref->params_json);
}

static bool gm_api_prepare_generation_matches(uint32_t generation)
{
    bool matches = false;
    if (gm_api_prepare_lock() != ESP_OK) {
        return false;
    }
    matches = generation == s_api_prepare_generation;
    gm_api_prepare_unlock();
    return matches;
}

static void gm_api_prepare_scenario_audio_assets(const room_scenario_t *scenario,
                                                 uint32_t generation)
{
    if (!scenario) {
        return;
    }
    for (size_t i = 0; i < scenario->step_count; ++i) {
        const room_scenario_step_t *step = &scenario->steps[i];
        if (!gm_api_prepare_generation_matches(generation)) {
            return;
        }
        if (!step->enabled) {
            continue;
        }
        if (step->type == ROOM_SCENARIO_STEP_DEVICE_COMMAND) {
            gm_api_prepare_audio_command(&step->data.device_command);
        } else if (step->type == ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP) {
            for (uint8_t j = 0; j < step->data.device_command_group.command_count; ++j) {
                gm_api_prepare_audio_command_fields(step->data.device_command_group.commands[j].device_id,
                                                    step->data.device_command_group.commands[j].command_id,
                                                    step->data.device_command_group.commands[j].params_json);
            }
        }
    }
    for (size_t i = 0; i < scenario->reactive_action_count; ++i) {
        const room_scenario_reactive_action_t *action = &scenario->reactive_actions[i];
        if (!gm_api_prepare_generation_matches(generation)) {
            return;
        }
        if (action->type == ROOM_SCENARIO_STEP_DEVICE_COMMAND) {
            gm_api_prepare_audio_command(&action->data.device_command);
        } else if (action->type == ROOM_SCENARIO_STEP_DEVICE_COMMAND_GROUP) {
            size_t start = action->group_command_start_index;
            size_t end = start + action->group_command_count;
            if (end > scenario->reactive_group_command_count) {
                continue;
            }
            for (size_t j = start; j < end; ++j) {
                gm_api_prepare_audio_command(&scenario->reactive_group_commands[j]);
            }
        }
    }
}

static void gm_api_prepare_profile_assets_now(const char *profile_id,
                                              uint32_t generation)
{
    room_scenario_t *scenario = &s_api_prepare_scenario;
    char scenario_id[GM_GAME_PROFILE_SCENARIO_ID_MAX_LEN] = {0};
    esp_err_t err = ESP_OK;
    uint32_t started_ms = sd_storage_trace_now_ms();
    char detail[96] = {0};
    if (!profile_id || !profile_id[0]) {
        return;
    }
    if (gm_api_prepare_lock() != ESP_OK) {
        return;
    }
    memset(&s_api_prepare_profile, 0, sizeof(s_api_prepare_profile));
    err = gm_game_profile_get(profile_id, &s_api_prepare_profile);
    if (err == ESP_OK) {
        quest_str_copy(scenario_id, sizeof(scenario_id), s_api_prepare_profile.scenario_id);
    }
    gm_api_prepare_unlock();
    if (err != ESP_OK || !scenario_id[0]) {
        return;
    }
    if (!gm_api_prepare_generation_matches(generation)) {
        return;
    }

    if (gm_api_prepare_lock() != ESP_OK) {
        return;
    }
    memset(scenario, 0, sizeof(*scenario));
    err = room_scenario_get(scenario_id, scenario);
    gm_api_prepare_unlock();
    if (err == ESP_OK) {
        gm_api_prepare_scenario_audio_assets(scenario, generation);
    }
    snprintf(detail,
             sizeof(detail),
             "profile=%s scenario=%s result=%s",
             profile_id,
             scenario_id[0] ? scenario_id : "-",
             esp_err_to_name(err));
    sd_storage_trace_log("gm_api", "profile_warmup", profile_id, sd_storage_trace_now_ms() - started_ms, detail);
}

static void gm_api_prepare_profile_assets_job(void *ctx)
{
    (void)ctx;
    for (;;) {
        char profile_id[GM_GAME_PROFILE_ID_MAX_LEN] = {0};
        uint32_t generation = 0;

        if (gm_api_prepare_lock() != ESP_OK) {
            return;
        }
        generation = s_api_prepare_generation;
        quest_str_copy(profile_id, sizeof(profile_id), s_api_prepare_profile_id);
        gm_api_prepare_unlock();

        gm_api_prepare_profile_assets_now(profile_id, generation);

        if (gm_api_prepare_lock() != ESP_OK) {
            return;
        }
        if (generation == s_api_prepare_generation) {
            s_api_prepare_job_active = false;
            gm_api_prepare_unlock();
            return;
        }
        gm_api_prepare_unlock();
    }
}

static void gm_api_schedule_profile_asset_warmup(const char *profile_id)
{
    bool should_post = false;
    if (!profile_id || !profile_id[0]) {
        return;
    }
    if (gm_api_prepare_lock() != ESP_OK) {
        return;
    }
    quest_str_copy(s_api_prepare_profile_id, sizeof(s_api_prepare_profile_id), profile_id);
    ++s_api_prepare_generation;
    if (!s_api_prepare_job_active) {
        s_api_prepare_job_active = true;
        should_post = true;
    }
    gm_api_prepare_unlock();

    if (should_post) {
        esp_err_t err = event_bus_post_job(gm_api_prepare_profile_assets_job, NULL, 0);
        if (err != ESP_OK) {
            if (gm_api_prepare_lock() == ESP_OK) {
                s_api_prepare_job_active = false;
                gm_api_prepare_unlock();
            }
            ESP_LOGW(TAG, "profile asset warmup job post failed: %s", esp_err_to_name(err));
        }
    }
}

static void gm_api_cancel_profile_asset_warmup(void)
{
    if (gm_api_prepare_lock() != ESP_OK) {
        return;
    }
    s_api_prepare_profile_id[0] = '\0';
    ++s_api_prepare_generation;
    gm_api_prepare_unlock();
}

esp_err_t gm_api_get_room_state(const char *room_id, gm_room_state_view_t *out)
{
    gm_room_session_timer_view_t timer_view = {0};
    gm_room_session_selected_view_t selected_view = {0};
    gm_room_session_runtime_summary_t runtime_view = {0};
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

    err = gm_room_session_get_read_views(room_id,
                                         gm_api_now_ms(),
                                         &timer_view,
                                         &selected_view,
                                         &runtime_view);
    if (err == ESP_ERR_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    out->session_present = true;
    out->session_state = timer_view.session_state;
    out->session_active = timer_view.session_active;
    out->timer_state = timer_view.timer_state;
    out->duration_ms = timer_view.duration_ms;
    out->remaining_ms = timer_view.remaining_ms;
    out->started_at_ms = timer_view.started_at_ms;
    out->paused_at_ms = timer_view.paused_at_ms;
    out->hint_active = timer_view.hint_active;
    out->hint_count = timer_view.hint_count;
    out->hint_updated_at_ms = timer_view.hint_updated_at_ms;
    quest_str_copy(out->hint_text, sizeof(out->hint_text), timer_view.hint_text);
    quest_str_copy(out->selected_profile_id,
                sizeof(out->selected_profile_id),
                selected_view.selected_profile_id);
    quest_str_copy(out->selected_profile_name,
                sizeof(out->selected_profile_name),
                selected_view.selected_profile_name);
    quest_str_copy(out->selected_profile_scenario_id,
                sizeof(out->selected_profile_scenario_id),
                selected_view.selected_profile_scenario_id);
    out->selected_profile_duration_ms = selected_view.selected_profile_duration_ms;
    quest_str_copy(out->selected_scenario_id,
                sizeof(out->selected_scenario_id),
                selected_view.selected_scenario_id);
    quest_str_copy(out->selected_scenario_name,
                sizeof(out->selected_scenario_name),
                selected_view.selected_scenario_name);
    if (selected_view.running_scenario_valid) {
        quest_str_copy(out->running_scenario_id,
                    sizeof(out->running_scenario_id),
                    selected_view.running_scenario_id);
        quest_str_copy(out->running_scenario_name,
                    sizeof(out->running_scenario_name),
                    selected_view.running_scenario_name);
        out->running_scenario_generation = selected_view.running_scenario_generation;
    }
    out->scenario_runtime_state = runtime_view.scenario_state;
    out->scenario_current_step_index = runtime_view.current_step_index;
    out->scenario_wait_type = runtime_view.wait_type;
    out->scenario_wait_until_ms = runtime_view.wait_until_ms;
    out->scenario_wait_started_at_ms = runtime_view.wait_started_at_ms;
    quest_str_copy(out->scenario_wait_event_type,
                sizeof(out->scenario_wait_event_type),
                runtime_view.wait_event_type);
    quest_str_copy(out->scenario_wait_source_id,
                sizeof(out->scenario_wait_source_id),
                runtime_view.wait_source_id);
    quest_str_copy(out->scenario_wait_operator_prompt,
                sizeof(out->scenario_wait_operator_prompt),
                runtime_view.wait_operator_prompt);
    quest_str_copy(out->scenario_wait_operator_label,
                sizeof(out->scenario_wait_operator_label),
                runtime_view.wait_operator_label);
    out->scenario_wait_operator_skip_allowed = runtime_view.wait_operator_skip_allowed;
    quest_str_copy(out->scenario_wait_operator_skip_label,
                sizeof(out->scenario_wait_operator_skip_label),
                runtime_view.wait_operator_skip_label);
    quest_str_copy(out->scenario_operator_message,
                sizeof(out->scenario_operator_message),
                runtime_view.scenario_operator_message);
    out->scenario_flag_count = runtime_view.scenario_flag_count;
    memcpy(out->scenario_flags,
           runtime_view.scenario_flags,
           sizeof(out->scenario_flags));
    quest_str_copy(out->scenario_last_error,
                sizeof(out->scenario_last_error),
                runtime_view.scenario_last_error);
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
    gm_room_session_timer_view_t timer_view = {0};
    esp_err_t err = gm_api_require_room(room_id);
    if (err != ESP_OK) {
        return err;
    }
    if (!has_duration) {
        err = gm_room_session_get_timer_view(room_id, gm_api_now_ms(), &timer_view);
        if (err != ESP_OK) {
            return err;
        }
        duration_ms = timer_view.duration_ms;
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
        gm_api_schedule_profile_asset_warmup(profile_id);
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
    gm_api_cancel_profile_asset_warmup();
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
    return gm_api_device_command_dispatch_run(device_id, command_id, params_json, NULL);
}

esp_err_t gm_api_device_command_dispatch_run(const char *device_id,
                                             const char *command_id,
                                             const char *params_json,
                                             command_executor_dispatch_t *out_dispatch)
{
    command_executor_request_t request = {0};
    scenehub_resolved_device_command_t resolved = {0};
    esp_err_t err = scenehub_device_command_resolve(device_id,
                                                    command_id,
                                                    params_json,
                                                    true,
                                                    &resolved,
                                                    NULL,
                                                    0);
    if (err != ESP_OK) {
        return err;
    }
    if (!resolved.command.manual_allowed) {
        return ESP_ERR_INVALID_STATE;
    }
    quest_str_copy(request.source, sizeof(request.source), "manual");
    quest_str_copy(request.device_id, sizeof(request.device_id), device_id);
    quest_str_copy(request.command_id, sizeof(request.command_id), command_id);
    request.require_manual_allowed = true;
    if (params_json && params_json[0]) {
        if (strlen(params_json) >= sizeof(request.params_json)) {
            return ESP_ERR_INVALID_SIZE;
        }
        quest_str_copy(request.params_json, sizeof(request.params_json), params_json);
    }
    return command_executor_execute_resolved(&request,
                                             resolved.client_id,
                                             &resolved.command,
                                             out_dispatch,
                                             NULL,
                                             0);
}
