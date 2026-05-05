#include "quest_device_internal.h"

#include <stdio.h>
#include <string.h>

static void qd_system_copy(char *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_len, "%s", src);
}

static void qd_system_command_set_policy(quest_device_command_t *cmd,
                                         bool manual_allowed,
                                         bool scenario_allowed,
                                         bool requires_confirmation,
                                         bool result_required,
                                         uint32_t timeout_ms,
                                         const char *danger_level)
{
    if (!cmd) {
        return;
    }
    cmd->manual_allowed = manual_allowed;
    cmd->scenario_allowed = scenario_allowed;
    cmd->requires_confirmation = requires_confirmation;
    cmd->result_required = result_required;
    cmd->timeout_ms = timeout_ms ? timeout_ms : QUEST_DEVICE_COMMAND_TIMEOUT_DEFAULT_MS;
    qd_system_copy(cmd->danger_level,
                   sizeof(cmd->danger_level),
                   danger_level && danger_level[0] ? danger_level : "normal");
}

static void qd_system_audio_play_command(quest_device_command_t *cmd)
{
    qd_system_copy(cmd->id, sizeof(cmd->id), "play");
    qd_system_copy(cmd->label, sizeof(cmd->label), "Play audio");
    qd_system_copy(cmd->capability, sizeof(cmd->capability), "audio");
    qd_system_copy(cmd->command, sizeof(cmd->command), "audio.play");
    qd_system_command_set_policy(cmd, false, true, false, false, QUEST_DEVICE_COMMAND_TIMEOUT_DEFAULT_MS, "normal");
    cmd->params[cmd->param_count++] = (quest_device_command_param_t) {
        .type = QUEST_DEVICE_COMMAND_PARAM_AUDIO_FILE_SELECT,
        .optional = false,
    };
    qd_system_copy(cmd->params[0].key, sizeof(cmd->params[0].key), "file");
    qd_system_copy(cmd->params[0].label, sizeof(cmd->params[0].label), "File");
    cmd->params[cmd->param_count++] = (quest_device_command_param_t) {
        .type = QUEST_DEVICE_COMMAND_PARAM_NUMBER,
        .optional = true,
    };
    qd_system_copy(cmd->params[1].key, sizeof(cmd->params[1].key), "volume");
    qd_system_copy(cmd->params[1].label, sizeof(cmd->params[1].label), "Volume");
    cmd->params[cmd->param_count++] = (quest_device_command_param_t) {
        .type = QUEST_DEVICE_COMMAND_PARAM_TEXT,
        .optional = true,
    };
    qd_system_copy(cmd->params[2].key, sizeof(cmd->params[2].key), "channel");
    qd_system_copy(cmd->params[2].label, sizeof(cmd->params[2].label), "Channel");
    cmd->params[cmd->param_count++] = (quest_device_command_param_t) {
        .type = QUEST_DEVICE_COMMAND_PARAM_CHECKBOX,
        .optional = true,
    };
    qd_system_copy(cmd->params[3].key, sizeof(cmd->params[3].key), "repeat");
    qd_system_copy(cmd->params[3].label, sizeof(cmd->params[3].label), "Repeat background");
}

static void qd_system_audio_stop_command(quest_device_command_t *cmd)
{
    qd_system_copy(cmd->id, sizeof(cmd->id), "stop");
    qd_system_copy(cmd->label, sizeof(cmd->label), "Stop audio");
    qd_system_copy(cmd->capability, sizeof(cmd->capability), "audio");
    qd_system_copy(cmd->command, sizeof(cmd->command), "audio.stop");
    qd_system_command_set_policy(cmd, true, true, false, false, QUEST_DEVICE_COMMAND_TIMEOUT_DEFAULT_MS, "normal");
}

static void qd_system_audio_pause_command(quest_device_command_t *cmd)
{
    qd_system_copy(cmd->id, sizeof(cmd->id), "pause");
    qd_system_copy(cmd->label, sizeof(cmd->label), "Pause audio");
    qd_system_copy(cmd->capability, sizeof(cmd->capability), "audio");
    qd_system_copy(cmd->command, sizeof(cmd->command), "audio.pause");
    qd_system_command_set_policy(cmd, true, true, false, false, QUEST_DEVICE_COMMAND_TIMEOUT_DEFAULT_MS, "normal");
}

static void qd_system_audio_resume_command(quest_device_command_t *cmd)
{
    qd_system_copy(cmd->id, sizeof(cmd->id), "resume");
    qd_system_copy(cmd->label, sizeof(cmd->label), "Resume audio");
    qd_system_copy(cmd->capability, sizeof(cmd->capability), "audio");
    qd_system_copy(cmd->command, sizeof(cmd->command), "audio.resume");
    qd_system_command_set_policy(cmd, true, true, false, false, QUEST_DEVICE_COMMAND_TIMEOUT_DEFAULT_MS, "normal");
}

static void qd_system_audio_set_volume_command(quest_device_command_t *cmd)
{
    qd_system_copy(cmd->id, sizeof(cmd->id), "set_volume");
    qd_system_copy(cmd->label, sizeof(cmd->label), "Set volume");
    qd_system_copy(cmd->capability, sizeof(cmd->capability), "audio");
    qd_system_copy(cmd->command, sizeof(cmd->command), "audio.set_volume");
    qd_system_command_set_policy(cmd, false, true, false, false, QUEST_DEVICE_COMMAND_TIMEOUT_DEFAULT_MS, "normal");
    cmd->params[cmd->param_count++] = (quest_device_command_param_t) {
        .type = QUEST_DEVICE_COMMAND_PARAM_NUMBER,
        .optional = false,
    };
    qd_system_copy(cmd->params[0].key, sizeof(cmd->params[0].key), "volume");
    qd_system_copy(cmd->params[0].label, sizeof(cmd->params[0].label), "Volume");
}

void quest_device_fill_system_audio(quest_device_t *out)
{
    quest_device_event_t *event = NULL;
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    qd_system_copy(out->id, sizeof(out->id), QUEST_DEVICE_SYSTEM_AUDIO_ID);
    qd_system_copy(out->client_id, sizeof(out->client_id), "internal");
    qd_system_copy(out->name, sizeof(out->name), "System Audio");
    out->enabled = true;
    out->system_device = true;

    qd_system_audio_play_command(&out->commands[out->command_count++]);
    qd_system_audio_stop_command(&out->commands[out->command_count++]);
    qd_system_audio_pause_command(&out->commands[out->command_count++]);
    qd_system_audio_resume_command(&out->commands[out->command_count++]);
    qd_system_audio_set_volume_command(&out->commands[out->command_count++]);

    event = &out->events[out->event_count++];
    qd_system_copy(event->id, sizeof(event->id), "playback_finished");
    qd_system_copy(event->label, sizeof(event->label), "Playback finished");
    qd_system_copy(event->capability, sizeof(event->capability), "audio");
    qd_system_copy(event->event, sizeof(event->event), "audio_finished");

    event = &out->events[out->event_count++];
    qd_system_copy(event->id, sizeof(event->id), "playback_failed");
    qd_system_copy(event->label, sizeof(event->label), "Playback failed");
    qd_system_copy(event->capability, sizeof(event->capability), "audio");
    qd_system_copy(event->event, sizeof(event->event), "playback_failed");
}

esp_err_t quest_device_system_audio_command(const char *command_id,
                                            quest_device_command_t *out)
{
    quest_device_command_t cmd = {0};
    if (!command_id || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(command_id, "play") == 0) {
        qd_system_audio_play_command(&cmd);
    } else if (strcmp(command_id, "stop") == 0) {
        qd_system_audio_stop_command(&cmd);
    } else if (strcmp(command_id, "pause") == 0) {
        qd_system_audio_pause_command(&cmd);
    } else if (strcmp(command_id, "resume") == 0) {
        qd_system_audio_resume_command(&cmd);
    } else if (strcmp(command_id, "set_volume") == 0) {
        qd_system_audio_set_volume_command(&cmd);
    } else {
        return ESP_ERR_NOT_FOUND;
    }
    *out = cmd;
    return ESP_OK;
}

esp_err_t quest_device_system_audio_event(const char *event_id,
                                          quest_device_event_t *out)
{
    quest_device_event_t event = {0};
    if (!event_id || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(event_id, "playback_finished") == 0) {
        qd_system_copy(event.id, sizeof(event.id), "playback_finished");
        qd_system_copy(event.label, sizeof(event.label), "Playback finished");
        qd_system_copy(event.capability, sizeof(event.capability), "audio");
        qd_system_copy(event.event, sizeof(event.event), "audio_finished");
    } else if (strcmp(event_id, "playback_failed") == 0) {
        qd_system_copy(event.id, sizeof(event.id), "playback_failed");
        qd_system_copy(event.label, sizeof(event.label), "Playback failed");
        qd_system_copy(event.capability, sizeof(event.capability), "audio");
        qd_system_copy(event.event, sizeof(event.event), "playback_failed");
    } else {
        return ESP_ERR_NOT_FOUND;
    }
    *out = event;
    return ESP_OK;
}
