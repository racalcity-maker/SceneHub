#include "gm_room_session_internal.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "audio_player.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "mqtt_core.h"
#include "quest_common_utils.h"
#include "quest_device.h"

static esp_err_t scenario_command_fail(char *error,
                                       size_t error_size,
                                       esp_err_t err,
                                       const char *message)
{
    if (error && error_size > 0) {
        quest_str_copy(error, error_size, message ? message : "device_command_failed");
    }
    return err;
}

static esp_err_t scenario_params_get_string(const char *params_json,
                                            const char *key,
                                            char *out,
                                            size_t out_size,
                                            bool required)
{
    cJSON *root = NULL;
    const cJSON *item = NULL;
    if (!key || !out || out_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    out[0] = '\0';
    if (!params_json || !params_json[0]) {
        return required ? ESP_ERR_INVALID_ARG : ESP_OK;
    }
    root = cJSON_Parse(params_json);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!item || cJSON_IsNull(item)) {
        cJSON_Delete(root);
        return required ? ESP_ERR_INVALID_ARG : ESP_OK;
    }
    if (!cJSON_IsString(item) || !item->valuestring || strlen(item->valuestring) >= out_size) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    quest_str_copy(out, out_size, item->valuestring);
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t scenario_params_get_int(const char *params_json,
                                         const char *key,
                                         int *out,
                                         bool required)
{
    cJSON *root = NULL;
    const cJSON *item = NULL;
    if (!key || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!params_json || !params_json[0]) {
        return required ? ESP_ERR_INVALID_ARG : ESP_OK;
    }
    root = cJSON_Parse(params_json);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!item || cJSON_IsNull(item)) {
        cJSON_Delete(root);
        return required ? ESP_ERR_INVALID_ARG : ESP_OK;
    }
    if (cJSON_IsNumber(item)) {
        *out = item->valueint;
        cJSON_Delete(root);
        return ESP_OK;
    }
    if (cJSON_IsString(item) && item->valuestring && item->valuestring[0]) {
        *out = atoi(item->valuestring);
        cJSON_Delete(root);
        return ESP_OK;
    }
    cJSON_Delete(root);
    return ESP_ERR_INVALID_ARG;
}

static esp_err_t scenario_params_get_bool(const char *params_json,
                                          const char *key,
                                          bool *out,
                                          bool required)
{
    cJSON *root = NULL;
    const cJSON *item = NULL;
    if (!key || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!params_json || !params_json[0]) {
        return required ? ESP_ERR_INVALID_ARG : ESP_OK;
    }
    root = cJSON_Parse(params_json);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!item || cJSON_IsNull(item)) {
        cJSON_Delete(root);
        return required ? ESP_ERR_INVALID_ARG : ESP_OK;
    }
    if (cJSON_IsBool(item)) {
        *out = cJSON_IsTrue(item);
        cJSON_Delete(root);
        return ESP_OK;
    }
    if (cJSON_IsNumber(item)) {
        *out = item->valueint != 0;
        cJSON_Delete(root);
        return ESP_OK;
    }
    if (cJSON_IsString(item) && item->valuestring) {
        *out = strcasecmp(item->valuestring, "true") == 0 ||
               strcmp(item->valuestring, "1") == 0 ||
               strcasecmp(item->valuestring, "yes") == 0 ||
               strcasecmp(item->valuestring, "on") == 0;
        cJSON_Delete(root);
        return ESP_OK;
    }
    cJSON_Delete(root);
    return ESP_ERR_INVALID_ARG;
}

static bool audio_path_has_ext(const char *path, const char *ext)
{
    size_t path_len = 0;
    size_t ext_len = 0;
    if (!path || !ext) {
        return false;
    }
    path_len = strlen(path);
    ext_len = strlen(ext);
    if (path_len < ext_len) {
        return false;
    }
    return strcasecmp(path + path_len - ext_len, ext) == 0;
}

static bool audio_channel_is_background(const char *channel)
{
    return channel && (strcasecmp(channel, "background") == 0 ||
                       strcasecmp(channel, "bg") == 0 ||
                       strcasecmp(channel, "music") == 0);
}

static bool audio_channel_is_effect(const char *channel)
{
    return !channel || !channel[0] ||
           strcasecmp(channel, "effect") == 0 ||
           strcasecmp(channel, "fx") == 0;
}

static esp_err_t execute_system_audio_command(const room_scenario_device_command_t *step_command,
                                              const quest_device_command_t *command,
                                              char *error,
                                              size_t error_size)
{
    if (!step_command || !command) {
        return scenario_command_fail(error, error_size, ESP_ERR_INVALID_ARG, "audio_command_invalid");
    }
    if (strcmp(command->id, "play") == 0 ||
        strcmp(command->kind, "internal_audio_play") == 0) {
        char file[256] = {0};
        char channel[24] = {0};
        struct stat st = {0};
        int volume = -1;
        bool repeat = false;
        esp_err_t err = scenario_params_get_string(step_command->params_json,
                                                   "file",
                                                   file,
                                                   sizeof(file),
                                                   true);
        if (err != ESP_OK) {
            return scenario_command_fail(error, error_size, err, "audio_file_param_missing");
        }
        if (!file[0]) {
            return scenario_command_fail(error, error_size, ESP_ERR_INVALID_ARG, "audio_file_empty");
        }
        (void)scenario_params_get_int(step_command->params_json, "volume", &volume, false);
        (void)scenario_params_get_string(step_command->params_json,
                                         "channel",
                                         channel,
                                         sizeof(channel),
                                         false);
        (void)scenario_params_get_bool(step_command->params_json, "repeat", &repeat, false);
        if (audio_channel_is_background(channel)) {
            if (!audio_path_has_ext(file, ".wav")) {
                return scenario_command_fail(error,
                                             error_size,
                                             ESP_ERR_NOT_SUPPORTED,
                                             "audio_background_requires_wav");
            }
            if (stat(file, &st) != 0) {
                return scenario_command_fail(error, error_size, ESP_ERR_NOT_FOUND, "audio_file_not_found");
            }
            err = audio_player_play_background_wav_repeat(file, volume, repeat);
        } else if (audio_channel_is_effect(channel)) {
            if (stat(file, &st) != 0) {
                return scenario_command_fail(error, error_size, ESP_ERR_NOT_FOUND, "audio_file_not_found");
            }
            err = audio_player_play_effect(file, volume);
        } else {
            return scenario_command_fail(error, error_size, ESP_ERR_INVALID_ARG, "audio_channel_invalid");
        }
        if (err != ESP_OK) {
            return scenario_command_fail(error, error_size, err, "audio_play_failed");
        }
        return ESP_OK;
    }
    if (strcmp(command->id, "stop") == 0 ||
        strcmp(command->kind, "internal_audio_stop") == 0) {
        char channel[24] = {0};
        (void)scenario_params_get_string(step_command->params_json,
                                         "channel",
                                         channel,
                                         sizeof(channel),
                                         false);
        if (!channel[0] || strcasecmp(channel, "all") == 0) {
            audio_player_stop_all();
        } else if (audio_channel_is_background(channel)) {
            audio_player_stop_background();
        } else if (audio_channel_is_effect(channel)) {
            audio_player_stop_effect();
        } else {
            return scenario_command_fail(error, error_size, ESP_ERR_INVALID_ARG, "audio_channel_invalid");
        }
        return ESP_OK;
    }
    if (strcmp(command->id, "pause") == 0 ||
        strcmp(command->kind, "internal_audio_pause") == 0) {
        audio_player_pause();
        return ESP_OK;
    }
    if (strcmp(command->id, "resume") == 0 ||
        strcmp(command->kind, "internal_audio_resume") == 0) {
        audio_player_resume();
        return ESP_OK;
    }
    if (strcmp(command->id, "set_volume") == 0 ||
        strcmp(command->kind, "internal_audio_set_volume") == 0) {
        int volume = 0;
        esp_err_t err = scenario_params_get_int(step_command->params_json, "volume", &volume, true);
        if (err != ESP_OK) {
            return scenario_command_fail(error, error_size, err, "audio_volume_param_missing");
        }
        err = audio_player_set_volume(volume);
        if (err != ESP_OK) {
            return scenario_command_fail(error, error_size, err, "audio_volume_failed");
        }
        return ESP_OK;
    }
    return scenario_command_fail(error, error_size, ESP_ERR_NOT_SUPPORTED, "audio_command_unsupported");
}

esp_err_t gm_room_session_execute_quest_device_command_internal(
    const room_scenario_device_command_t *step_command,
    char *error,
    size_t error_size)
{
    quest_device_command_t *command = NULL;
    quest_device_t *device = NULL;
    esp_err_t err = ESP_OK;
    if (!step_command || !step_command->device_id[0] || !step_command->command_id[0]) {
        return scenario_command_fail(error, error_size, ESP_ERR_INVALID_ARG, "device_command_invalid");
    }
    device = (quest_device_t *)gm_room_session_heap_alloc(sizeof(*device));
    command = (quest_device_command_t *)gm_room_session_heap_alloc(sizeof(*command));
    if (!device || !command) {
        heap_caps_free(device);
        heap_caps_free(command);
        return scenario_command_fail(error, error_size, ESP_ERR_NO_MEM, "device_command_no_mem");
    }
    err = quest_device_get(step_command->device_id, device);
    if (err != ESP_OK) {
        heap_caps_free(device);
        heap_caps_free(command);
        return scenario_command_fail(error, error_size, err, "device_not_found");
    }
    if (!device->enabled) {
        heap_caps_free(device);
        heap_caps_free(command);
        return scenario_command_fail(error, error_size, ESP_ERR_INVALID_STATE, "device_disabled");
    }
    err = quest_device_get_command(step_command->device_id,
                                   step_command->command_id,
                                   command);
    if (err != ESP_OK) {
        heap_caps_free(device);
        heap_caps_free(command);
        return scenario_command_fail(error, error_size, err, "device_command_not_found");
    }
    if (strcmp(step_command->device_id, QUEST_DEVICE_SYSTEM_AUDIO_ID) == 0 ||
        strncmp(command->kind, "internal_audio_", strlen("internal_audio_")) == 0) {
        err = execute_system_audio_command(step_command, command, error, error_size);
        heap_caps_free(device);
        heap_caps_free(command);
        return err;
    }
    if (!command->topic[0]) {
        heap_caps_free(device);
        heap_caps_free(command);
        return scenario_command_fail(error, error_size, ESP_ERR_INVALID_ARG, "device_command_topic_missing");
    }
    err = mqtt_core_publish(command->topic, command->payload);
    heap_caps_free(device);
    heap_caps_free(command);
    if (err != ESP_OK) {
        return scenario_command_fail(error, error_size, err, "device_command_publish_failed");
    }
    return ESP_OK;
}

esp_err_t gm_room_session_execute_device_command(const char *device_id,
                                                 const char *command_id,
                                                 const char *params_json)
{
    room_scenario_device_command_t command = {0};
    if (!device_id || !device_id[0] || !command_id || !command_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(device_id) >= sizeof(command.device_id) ||
        strlen(command_id) >= sizeof(command.command_id)) {
        return ESP_ERR_INVALID_SIZE;
    }
    quest_str_copy(command.device_id, sizeof(command.device_id), device_id);
    quest_str_copy(command.command_id, sizeof(command.command_id), command_id);
    if (params_json && params_json[0]) {
        if (strlen(params_json) >= sizeof(command.params_json)) {
            return ESP_ERR_INVALID_SIZE;
        }
        quest_str_copy(command.params_json, sizeof(command.params_json), params_json);
    }
    return gm_room_session_execute_quest_device_command_internal(&command, NULL, 0);
}
