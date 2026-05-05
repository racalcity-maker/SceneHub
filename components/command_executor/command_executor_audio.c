#include "command_executor_internal.h"

#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "audio_player.h"

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

esp_err_t command_executor_execute_audio(const command_executor_request_t *request,
                                         const quest_device_command_t *command,
                                         char *error,
                                         size_t error_size)
{
    if (!request || !command) {
        return command_executor_fail(error, error_size, ESP_ERR_INVALID_ARG, "audio_command_invalid");
    }
    if (strcmp(command->id, "play") == 0 || command_executor_command_name_is(command, "audio.play")) {
        char file[256] = {0};
        char channel[24] = {0};
        struct stat st = {0};
        int volume = -1;
        bool repeat = false;
        esp_err_t err = command_executor_params_get_string(request->params_json,
                                                           "file",
                                                           file,
                                                           sizeof(file),
                                                           true);
        if (err != ESP_OK) {
            return command_executor_fail(error, error_size, err, "audio_file_param_missing");
        }
        if (!file[0]) {
            return command_executor_fail(error, error_size, ESP_ERR_INVALID_ARG, "audio_file_empty");
        }
        (void)command_executor_params_get_int(request->params_json, "volume", &volume, false);
        (void)command_executor_params_get_string(request->params_json,
                                                 "channel",
                                                 channel,
                                                 sizeof(channel),
                                                 false);
        (void)command_executor_params_get_bool(request->params_json, "repeat", &repeat, false);
        if (audio_channel_is_background(channel)) {
            if (!audio_path_has_ext(file, ".wav")) {
                return command_executor_fail(error,
                                             error_size,
                                             ESP_ERR_NOT_SUPPORTED,
                                             "audio_background_requires_wav");
            }
            if (stat(file, &st) != 0) {
                return command_executor_fail(error, error_size, ESP_ERR_NOT_FOUND, "audio_file_not_found");
            }
            err = audio_player_play_background_wav_repeat(file, volume, repeat);
        } else if (audio_channel_is_effect(channel)) {
            if (stat(file, &st) != 0) {
                return command_executor_fail(error, error_size, ESP_ERR_NOT_FOUND, "audio_file_not_found");
            }
            err = audio_player_play_effect(file, volume);
        } else {
            return command_executor_fail(error, error_size, ESP_ERR_INVALID_ARG, "audio_channel_invalid");
        }
        return err == ESP_OK ? ESP_OK : command_executor_fail(error, error_size, err, "audio_play_failed");
    }
    if (strcmp(command->id, "stop") == 0 || command_executor_command_name_is(command, "audio.stop")) {
        char channel[24] = {0};
        (void)command_executor_params_get_string(request->params_json,
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
            return command_executor_fail(error, error_size, ESP_ERR_INVALID_ARG, "audio_channel_invalid");
        }
        return ESP_OK;
    }
    if (strcmp(command->id, "pause") == 0 || command_executor_command_name_is(command, "audio.pause")) {
        audio_player_pause();
        return ESP_OK;
    }
    if (strcmp(command->id, "resume") == 0 || command_executor_command_name_is(command, "audio.resume")) {
        audio_player_resume();
        return ESP_OK;
    }
    if (strcmp(command->id, "set_volume") == 0 ||
        command_executor_command_name_is(command, "audio.set_volume")) {
        int volume = 0;
        esp_err_t err = command_executor_params_get_int(request->params_json,
                                                        "volume",
                                                        &volume,
                                                        true);
        if (err != ESP_OK) {
            return command_executor_fail(error, error_size, err, "audio_volume_param_missing");
        }
        err = audio_player_set_volume(volume);
        return err == ESP_OK ? ESP_OK : command_executor_fail(error, error_size, err, "audio_volume_failed");
    }
    return command_executor_fail(error, error_size, ESP_ERR_NOT_SUPPORTED, "audio_command_unsupported");
}
