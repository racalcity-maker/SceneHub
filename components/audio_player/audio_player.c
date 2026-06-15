#include "audio_player.h"
#include "audio_player_internal.h"

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "event_bus.h"
#include "sd_storage.h"

static const char *TAG = "audio_player";

static bool s_event_handler_registered = false;

static bool path_has_ext(const char *path, const char *ext)
{
    size_t path_len = path ? strlen(path) : 0;
    size_t ext_len = ext ? strlen(ext) : 0;
    if (path_len < ext_len || ext_len == 0) {
        return false;
    }
    const char *tail = path + path_len - ext_len;
    for (size_t i = 0; i < ext_len; ++i) {
        char a = tail[i];
        char b = ext[i];
        if (a >= 'A' && a <= 'Z') {
            a = (char)(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

static esp_err_t enqueue_control_cmd(audio_cmd_type_t type, const char *label)
{
    audio_cmd_t cmd = {
        .type = type,
        .volume = -1,
        .seek_ratio = -1.0f,
    };
    esp_err_t err = audio_player_runtime_enqueue(&cmd);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s enqueue failed: %s", label, esp_err_to_name(err));
    }
    return err;
}

static void on_event(const scenehub_event_t *msg)
{
    if (!msg) {
        return;
    }
    switch (msg->type) {
    case SCENEHUB_EVENT_AUDIO_PLAY:
        audio_player_play(msg->payload);
        break;
    case SCENEHUB_EVENT_VOLUME_SET:
        ESP_LOGW(TAG, "ignoring volume cmd topic='%s' payload='%s'", msg->topic, msg->payload);
        break;
    case SCENEHUB_EVENT_WEB_COMMAND:
    case SCENEHUB_EVENT_NONE:
    case SCENEHUB_EVENT_CARD_OK:
    case SCENEHUB_EVENT_CARD_BAD:
    case SCENEHUB_EVENT_RELAY_CMD:
    case SCENEHUB_EVENT_SYSTEM_STATUS:
        break;
    default:
        break;
    }
}

esp_err_t audio_player_init(void)
{
    ESP_RETURN_ON_ERROR(sd_storage_init(), TAG, "storage init failed");
    ESP_RETURN_ON_ERROR(audio_player_volume_init(), TAG, "volume init failed");
    ESP_RETURN_ON_ERROR(audio_player_status_init(), TAG, "status init failed");
    ESP_RETURN_ON_ERROR(audio_player_output_init(), TAG, "audio output init failed");
    ESP_RETURN_ON_ERROR(audio_player_mixer_init(), TAG, "audio mixer init failed");
    ESP_RETURN_ON_ERROR(audio_player_runtime_init(), TAG, "audio runtime init failed");

    audio_player_status_reset(audio_player_runtime_volume());

    esp_err_t sd_err = sd_storage_mount();
    if (sd_err != ESP_OK) {
        ESP_LOGW(TAG, "sd mount failed, audio will beep only: %s", esp_err_to_name(sd_err));
    }

    if (!s_event_handler_registered) {
        esp_err_t err = event_bus_register_handler(on_event);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "failed to register event handler: %s", esp_err_to_name(err));
            return err;
        }
        s_event_handler_registered = true;
    }
    return ESP_OK;
}

esp_err_t audio_player_start(void)
{
    ESP_RETURN_ON_ERROR(audio_player_mixer_start(), TAG, "audio mixer start failed");
    return audio_player_runtime_start();
}

esp_err_t audio_player_play(const char *path)
{
    if (!path || !path[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    audio_cmd_t cmd = {
        .type = AUDIO_CMD_PLAY,
        .channel = AUDIO_PLAYER_CHANNEL_EFFECT,
        .volume = -1,
        .seek_ratio = -1.0f,
    };
    strncpy(cmd.path, path, sizeof(cmd.path) - 1);
    cmd.path[sizeof(cmd.path) - 1] = 0;
    return audio_player_runtime_enqueue(&cmd);
}

esp_err_t audio_player_play_seek(const char *path, float seek_ratio)
{
    if (!path || !path[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    audio_cmd_t cmd = {
        .type = AUDIO_CMD_PLAY,
        .channel = AUDIO_PLAYER_CHANNEL_EFFECT,
        .volume = -1,
        .seek_ratio = -1.0f,
    };
    strncpy(cmd.path, path, sizeof(cmd.path) - 1);
    cmd.path[sizeof(cmd.path) - 1] = 0;
    if (seek_ratio >= 0.0f) {
        if (seek_ratio > 1.0f) {
            seek_ratio = 1.0f;
        }
        cmd.seek_ratio = seek_ratio;
    }
    return audio_player_runtime_enqueue(&cmd);
}

esp_err_t audio_player_play_background_wav(const char *path, int volume_percent)
{
    return audio_player_play_background_wav_repeat(path, volume_percent, false);
}

esp_err_t audio_player_play_background_wav_repeat(const char *path, int volume_percent, bool repeat)
{
    if (!path || !path[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!path_has_ext(path, ".wav")) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    audio_cmd_t cmd = {
        .type = AUDIO_CMD_PLAY_BACKGROUND,
        .channel = AUDIO_PLAYER_CHANNEL_BACKGROUND,
        .volume = volume_percent,
        .seek_ratio = -1.0f,
        .repeat = repeat,
    };
    strncpy(cmd.path, path, sizeof(cmd.path) - 1);
    cmd.path[sizeof(cmd.path) - 1] = 0;
    return audio_player_runtime_enqueue(&cmd);
}

esp_err_t audio_player_play_effect(const char *path, int volume_percent)
{
    if (!path || !path[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    audio_cmd_t cmd = {
        .type = AUDIO_CMD_PLAY_EFFECT,
        .channel = AUDIO_PLAYER_CHANNEL_EFFECT,
        .volume = volume_percent,
        .seek_ratio = -1.0f,
    };
    strncpy(cmd.path, path, sizeof(cmd.path) - 1);
    cmd.path[sizeof(cmd.path) - 1] = 0;
    return audio_player_runtime_enqueue(&cmd);
}

esp_err_t audio_player_seek(uint32_t pos_ms)
{
    audio_cmd_t cmd = {0};
    esp_err_t err = audio_player_runtime_prepare_seek(&cmd, pos_ms);
    if (err != ESP_OK) {
        return err;
    }
    return audio_player_runtime_enqueue(&cmd);
}

void audio_player_stop(void)
{
    enqueue_control_cmd(AUDIO_CMD_STOP, "stop");
}

void audio_player_stop_background(void)
{
    enqueue_control_cmd(AUDIO_CMD_STOP_BACKGROUND, "stop background");
}

void audio_player_stop_effect(void)
{
    enqueue_control_cmd(AUDIO_CMD_STOP_EFFECT, "stop effect");
}

void audio_player_stop_all(void)
{
    audio_player_stop();
}

esp_err_t audio_player_stop_all_wait(uint32_t timeout_ms)
{
    audio_cmd_t cmd = {
        .type = AUDIO_CMD_STOP,
        .volume = -1,
        .seek_ratio = -1.0f,
    };
    return audio_player_runtime_enqueue_wait(&cmd, timeout_ms);
}

void audio_player_pause(void)
{
    enqueue_control_cmd(AUDIO_CMD_PAUSE, "pause");
}

void audio_player_resume(void)
{
    enqueue_control_cmd(AUDIO_CMD_RESUME, "resume");
}

void audio_player_get_status(audio_player_status_t *out)
{
    audio_player_status_get(out);
}
