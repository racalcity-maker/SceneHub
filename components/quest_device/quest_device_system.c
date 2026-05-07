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

static void qd_system_relay_channel_param(quest_device_command_t *cmd)
{
    if (!cmd || cmd->param_count >= QUEST_DEVICE_MAX_COMMAND_PARAMS) {
        return;
    }
    cmd->params[cmd->param_count++] = (quest_device_command_param_t) {
        .type = QUEST_DEVICE_COMMAND_PARAM_NUMBER,
        .optional = false,
    };
    qd_system_copy(cmd->params[cmd->param_count - 1].key,
                   sizeof(cmd->params[cmd->param_count - 1].key),
                   "channel");
    qd_system_copy(cmd->params[cmd->param_count - 1].label,
                   sizeof(cmd->params[cmd->param_count - 1].label),
                   "Channel");
}

static void qd_system_relay_set_command(quest_device_command_t *cmd)
{
    qd_system_copy(cmd->id, sizeof(cmd->id), "set");
    qd_system_copy(cmd->label, sizeof(cmd->label), "Relay set");
    qd_system_copy(cmd->capability, sizeof(cmd->capability), "relay");
    qd_system_copy(cmd->command, sizeof(cmd->command), "relay.set");
    qd_system_copy(cmd->default_args_json, sizeof(cmd->default_args_json), "{\"channel\":1,\"on\":true}");
    qd_system_command_set_policy(cmd, true, true, false, false, QUEST_DEVICE_COMMAND_TIMEOUT_DEFAULT_MS, "normal");
    qd_system_relay_channel_param(cmd);
    cmd->params[cmd->param_count++] = (quest_device_command_param_t) {
        .type = QUEST_DEVICE_COMMAND_PARAM_CHECKBOX,
        .optional = false,
    };
    qd_system_copy(cmd->params[cmd->param_count - 1].key,
                   sizeof(cmd->params[cmd->param_count - 1].key),
                   "on");
    qd_system_copy(cmd->params[cmd->param_count - 1].label,
                   sizeof(cmd->params[cmd->param_count - 1].label),
                   "On");
}

static void qd_system_relay_pulse_command(quest_device_command_t *cmd)
{
    qd_system_copy(cmd->id, sizeof(cmd->id), "pulse");
    qd_system_copy(cmd->label, sizeof(cmd->label), "Relay pulse");
    qd_system_copy(cmd->capability, sizeof(cmd->capability), "relay");
    qd_system_copy(cmd->command, sizeof(cmd->command), "relay.pulse");
    qd_system_copy(cmd->default_args_json, sizeof(cmd->default_args_json), "{\"channel\":1,\"duration_ms\":1000}");
    qd_system_command_set_policy(cmd, true, true, false, false, QUEST_DEVICE_COMMAND_TIMEOUT_DEFAULT_MS, "normal");
    qd_system_relay_channel_param(cmd);
    cmd->params[cmd->param_count++] = (quest_device_command_param_t) {
        .type = QUEST_DEVICE_COMMAND_PARAM_NUMBER,
        .optional = false,
    };
    qd_system_copy(cmd->params[cmd->param_count - 1].key,
                   sizeof(cmd->params[cmd->param_count - 1].key),
                   "duration_ms");
    qd_system_copy(cmd->params[cmd->param_count - 1].label,
                   sizeof(cmd->params[cmd->param_count - 1].label),
                   "Duration ms");
}

static void qd_system_relay_toggle_command(quest_device_command_t *cmd)
{
    qd_system_copy(cmd->id, sizeof(cmd->id), "toggle");
    qd_system_copy(cmd->label, sizeof(cmd->label), "Relay toggle");
    qd_system_copy(cmd->capability, sizeof(cmd->capability), "relay");
    qd_system_copy(cmd->command, sizeof(cmd->command), "relay.toggle");
    qd_system_copy(cmd->default_args_json, sizeof(cmd->default_args_json), "{\"channel\":1}");
    qd_system_command_set_policy(cmd, true, false, false, false, QUEST_DEVICE_COMMAND_TIMEOUT_DEFAULT_MS, "normal");
    qd_system_relay_channel_param(cmd);
}

void quest_device_fill_system_relay(quest_device_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    qd_system_copy(out->id, sizeof(out->id), QUEST_DEVICE_SYSTEM_RELAY_ID);
    qd_system_copy(out->client_id, sizeof(out->client_id), "internal");
    qd_system_copy(out->name, sizeof(out->name), "System Relay");
    out->enabled = true;
    out->system_device = true;

    qd_system_relay_set_command(&out->commands[out->command_count++]);
    qd_system_relay_pulse_command(&out->commands[out->command_count++]);
    qd_system_relay_toggle_command(&out->commands[out->command_count++]);
}

esp_err_t quest_device_system_relay_command(const char *command_id,
                                            quest_device_command_t *out)
{
    quest_device_command_t cmd = {0};
    if (!command_id || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(command_id, "set") == 0) {
        qd_system_relay_set_command(&cmd);
    } else if (strcmp(command_id, "pulse") == 0) {
        qd_system_relay_pulse_command(&cmd);
    } else if (strcmp(command_id, "toggle") == 0) {
        qd_system_relay_toggle_command(&cmd);
    } else {
        return ESP_ERR_NOT_FOUND;
    }
    *out = cmd;
    return ESP_OK;
}

static void qd_system_mosfet_channel_param(quest_device_command_t *cmd)
{
    if (!cmd || cmd->param_count >= QUEST_DEVICE_MAX_COMMAND_PARAMS) {
        return;
    }
    cmd->params[cmd->param_count++] = (quest_device_command_param_t) {
        .type = QUEST_DEVICE_COMMAND_PARAM_NUMBER,
        .optional = false,
    };
    qd_system_copy(cmd->params[cmd->param_count - 1].key,
                   sizeof(cmd->params[cmd->param_count - 1].key),
                   "channel");
    qd_system_copy(cmd->params[cmd->param_count - 1].label,
                   sizeof(cmd->params[cmd->param_count - 1].label),
                   "Channel");
}

static void qd_system_mosfet_value_param(quest_device_command_t *cmd, const char *key, const char *label)
{
    if (!cmd || cmd->param_count >= QUEST_DEVICE_MAX_COMMAND_PARAMS) {
        return;
    }
    cmd->params[cmd->param_count++] = (quest_device_command_param_t) {
        .type = QUEST_DEVICE_COMMAND_PARAM_NUMBER,
        .optional = false,
    };
    qd_system_copy(cmd->params[cmd->param_count - 1].key,
                   sizeof(cmd->params[cmd->param_count - 1].key),
                   key);
    qd_system_copy(cmd->params[cmd->param_count - 1].label,
                   sizeof(cmd->params[cmd->param_count - 1].label),
                   label);
}

static void qd_system_mosfet_set_command(quest_device_command_t *cmd)
{
    qd_system_copy(cmd->id, sizeof(cmd->id), "set");
    qd_system_copy(cmd->label, sizeof(cmd->label), "MOSFET set");
    qd_system_copy(cmd->capability, sizeof(cmd->capability), "mosfet");
    qd_system_copy(cmd->command, sizeof(cmd->command), "mosfet.set");
    qd_system_copy(cmd->default_args_json, sizeof(cmd->default_args_json), "{\"channel\":1,\"value\":255}");
    qd_system_command_set_policy(cmd, true, true, false, false, QUEST_DEVICE_COMMAND_TIMEOUT_DEFAULT_MS, "normal");
    qd_system_mosfet_channel_param(cmd);
    qd_system_mosfet_value_param(cmd, "value", "Value 0-255");
}

static void qd_system_mosfet_fade_command(quest_device_command_t *cmd)
{
    qd_system_copy(cmd->id, sizeof(cmd->id), "fade");
    qd_system_copy(cmd->label, sizeof(cmd->label), "MOSFET fade");
    qd_system_copy(cmd->capability, sizeof(cmd->capability), "mosfet");
    qd_system_copy(cmd->command, sizeof(cmd->command), "mosfet.fade");
    qd_system_copy(cmd->default_args_json,
                   sizeof(cmd->default_args_json),
                   "{\"channel\":1,\"target\":255,\"duration_ms\":1000}");
    qd_system_command_set_policy(cmd, true, true, false, false, QUEST_DEVICE_COMMAND_TIMEOUT_DEFAULT_MS, "normal");
    qd_system_mosfet_channel_param(cmd);
    qd_system_mosfet_value_param(cmd, "target", "Target 0-255");
    qd_system_mosfet_value_param(cmd, "duration_ms", "Duration ms");
}

static void qd_system_mosfet_pulse_command(quest_device_command_t *cmd)
{
    qd_system_copy(cmd->id, sizeof(cmd->id), "pulse");
    qd_system_copy(cmd->label, sizeof(cmd->label), "MOSFET pulse");
    qd_system_copy(cmd->capability, sizeof(cmd->capability), "mosfet");
    qd_system_copy(cmd->command, sizeof(cmd->command), "mosfet.pulse");
    qd_system_copy(cmd->default_args_json,
                   sizeof(cmd->default_args_json),
                   "{\"channel\":1,\"value\":255,\"duration_ms\":1000}");
    qd_system_command_set_policy(cmd, true, true, false, false, QUEST_DEVICE_COMMAND_TIMEOUT_DEFAULT_MS, "normal");
    qd_system_mosfet_channel_param(cmd);
    qd_system_mosfet_value_param(cmd, "value", "Value 0-255");
    qd_system_mosfet_value_param(cmd, "duration_ms", "Duration ms");
}

static void qd_system_mosfet_all_off_command(quest_device_command_t *cmd)
{
    qd_system_copy(cmd->id, sizeof(cmd->id), "all_off");
    qd_system_copy(cmd->label, sizeof(cmd->label), "MOSFET all off");
    qd_system_copy(cmd->capability, sizeof(cmd->capability), "mosfet");
    qd_system_copy(cmd->command, sizeof(cmd->command), "mosfet.all_off");
    qd_system_copy(cmd->default_args_json, sizeof(cmd->default_args_json), "{}");
    qd_system_command_set_policy(cmd, true, true, false, false, QUEST_DEVICE_COMMAND_TIMEOUT_DEFAULT_MS, "normal");
}

void quest_device_fill_system_mosfet(quest_device_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    qd_system_copy(out->id, sizeof(out->id), QUEST_DEVICE_SYSTEM_MOSFET_ID);
    qd_system_copy(out->client_id, sizeof(out->client_id), "internal");
    qd_system_copy(out->name, sizeof(out->name), "System MOSFET");
    out->enabled = true;
    out->system_device = true;

    qd_system_mosfet_set_command(&out->commands[out->command_count++]);
    qd_system_mosfet_fade_command(&out->commands[out->command_count++]);
    qd_system_mosfet_pulse_command(&out->commands[out->command_count++]);
    qd_system_mosfet_all_off_command(&out->commands[out->command_count++]);
}

esp_err_t quest_device_system_mosfet_command(const char *command_id,
                                             quest_device_command_t *out)
{
    quest_device_command_t cmd = {0};
    if (!command_id || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(command_id, "set") == 0) {
        qd_system_mosfet_set_command(&cmd);
    } else if (strcmp(command_id, "fade") == 0) {
        qd_system_mosfet_fade_command(&cmd);
    } else if (strcmp(command_id, "pulse") == 0) {
        qd_system_mosfet_pulse_command(&cmd);
    } else if (strcmp(command_id, "all_off") == 0) {
        qd_system_mosfet_all_off_command(&cmd);
    } else {
        return ESP_ERR_NOT_FOUND;
    }
    *out = cmd;
    return ESP_OK;
}

static void qd_system_input_get_state_command(quest_device_command_t *cmd)
{
    qd_system_copy(cmd->id, sizeof(cmd->id), "get_state");
    qd_system_copy(cmd->label, sizeof(cmd->label), "Input get state");
    qd_system_copy(cmd->capability, sizeof(cmd->capability), "input");
    qd_system_copy(cmd->command, sizeof(cmd->command), "input.get_state");
    qd_system_copy(cmd->default_args_json, sizeof(cmd->default_args_json), "{\"channel\":1}");
    qd_system_command_set_policy(cmd, true, false, false, false, QUEST_DEVICE_COMMAND_TIMEOUT_DEFAULT_MS, "normal");
    qd_system_relay_channel_param(cmd);
}

static void qd_system_add_channel_event(quest_device_t *out,
                                        const char *capability,
                                        const char *prefix,
                                        uint8_t channel,
                                        const char *suffix,
                                        const char *label_suffix)
{
    if (!out || out->event_count >= QUEST_DEVICE_MAX_EVENTS) {
        return;
    }
    quest_device_event_t *event = &out->events[out->event_count++];
    snprintf(event->id, sizeof(event->id), "ch%u_%s", (unsigned)channel, suffix);
    snprintf(event->label, sizeof(event->label), "Channel %u %s", (unsigned)channel, label_suffix);
    qd_system_copy(event->capability, sizeof(event->capability), capability);
    snprintf(event->event, sizeof(event->event), "%s.ch%u_%s", prefix, (unsigned)channel, suffix);
}

static esp_err_t qd_system_get_channel_event(const char *event_id,
                                             const char *capability,
                                             const char *prefix,
                                             bool include_pressed,
                                             quest_device_event_t *out)
{
    static const char *const suffixes[] = {
        "changed",
        "high",
        "low",
        "pressed",
        "released",
        "active",
        "inactive",
    };
    static const char *const labels[] = {
        "changed",
        "HIGH",
        "LOW",
        "pressed",
        "released",
        "active",
        "inactive",
    };
    if (!event_id || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    for (uint8_t channel = 1; channel <= 4; ++channel) {
        for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i) {
            if (!include_pressed &&
                (strcmp(suffixes[i], "pressed") == 0 || strcmp(suffixes[i], "released") == 0)) {
                continue;
            }
            if (include_pressed &&
                (strcmp(suffixes[i], "active") == 0 || strcmp(suffixes[i], "inactive") == 0)) {
                continue;
            }
            char id[QUEST_DEVICE_EVENT_ID_MAX_LEN] = {0};
            snprintf(id, sizeof(id), "ch%u_%s", (unsigned)channel, suffixes[i]);
            if (strcmp(event_id, id) != 0) {
                continue;
            }
            memset(out, 0, sizeof(*out));
            qd_system_copy(out->id, sizeof(out->id), id);
            snprintf(out->label, sizeof(out->label), "Channel %u %s", (unsigned)channel, labels[i]);
            qd_system_copy(out->capability, sizeof(out->capability), capability);
            snprintf(out->event, sizeof(out->event), "%s.%s", prefix, id);
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

void quest_device_fill_system_input(quest_device_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    qd_system_copy(out->id, sizeof(out->id), QUEST_DEVICE_SYSTEM_INPUT_ID);
    qd_system_copy(out->client_id, sizeof(out->client_id), "internal");
    qd_system_copy(out->name, sizeof(out->name), "System Input");
    out->enabled = true;
    out->system_device = true;

    qd_system_input_get_state_command(&out->commands[out->command_count++]);
    for (uint8_t channel = 1; channel <= 4; ++channel) {
        qd_system_add_channel_event(out, "input", "input", channel, "changed", "changed");
        qd_system_add_channel_event(out, "input", "input", channel, "high", "HIGH");
        qd_system_add_channel_event(out, "input", "input", channel, "low", "LOW");
        qd_system_add_channel_event(out, "input", "input", channel, "pressed", "pressed");
        qd_system_add_channel_event(out, "input", "input", channel, "released", "released");
    }
}

esp_err_t quest_device_system_input_command(const char *command_id,
                                            quest_device_command_t *out)
{
    quest_device_command_t cmd = {0};
    if (!command_id || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(command_id, "get_state") != 0) {
        return ESP_ERR_NOT_FOUND;
    }
    qd_system_input_get_state_command(&cmd);
    *out = cmd;
    return ESP_OK;
}

esp_err_t quest_device_system_input_event(const char *event_id,
                                          quest_device_event_t *out)
{
    return qd_system_get_channel_event(event_id, "input", "input", true, out);
}

static void qd_system_gpio_set_command(quest_device_command_t *cmd)
{
    qd_system_copy(cmd->id, sizeof(cmd->id), "set");
    qd_system_copy(cmd->label, sizeof(cmd->label), "GPIO set");
    qd_system_copy(cmd->capability, sizeof(cmd->capability), "gpio");
    qd_system_copy(cmd->command, sizeof(cmd->command), "gpio.set");
    qd_system_copy(cmd->default_args_json, sizeof(cmd->default_args_json), "{\"channel\":1,\"active\":true}");
    qd_system_command_set_policy(cmd, true, true, false, false, QUEST_DEVICE_COMMAND_TIMEOUT_DEFAULT_MS, "normal");
    qd_system_relay_channel_param(cmd);
    cmd->params[cmd->param_count++] = (quest_device_command_param_t) {
        .type = QUEST_DEVICE_COMMAND_PARAM_CHECKBOX,
        .optional = false,
    };
    qd_system_copy(cmd->params[cmd->param_count - 1].key,
                   sizeof(cmd->params[cmd->param_count - 1].key),
                   "active");
    qd_system_copy(cmd->params[cmd->param_count - 1].label,
                   sizeof(cmd->params[cmd->param_count - 1].label),
                   "Active");
}

static void qd_system_gpio_pulse_command(quest_device_command_t *cmd)
{
    qd_system_copy(cmd->id, sizeof(cmd->id), "pulse");
    qd_system_copy(cmd->label, sizeof(cmd->label), "GPIO pulse");
    qd_system_copy(cmd->capability, sizeof(cmd->capability), "gpio");
    qd_system_copy(cmd->command, sizeof(cmd->command), "gpio.pulse");
    qd_system_copy(cmd->default_args_json, sizeof(cmd->default_args_json), "{\"channel\":1,\"active\":true,\"duration_ms\":1000}");
    qd_system_command_set_policy(cmd, true, true, false, false, QUEST_DEVICE_COMMAND_TIMEOUT_DEFAULT_MS, "normal");
    qd_system_relay_channel_param(cmd);
    cmd->params[cmd->param_count++] = (quest_device_command_param_t) {
        .type = QUEST_DEVICE_COMMAND_PARAM_CHECKBOX,
        .optional = false,
    };
    qd_system_copy(cmd->params[cmd->param_count - 1].key,
                   sizeof(cmd->params[cmd->param_count - 1].key),
                   "active");
    qd_system_copy(cmd->params[cmd->param_count - 1].label,
                   sizeof(cmd->params[cmd->param_count - 1].label),
                   "Active");
    cmd->params[cmd->param_count++] = (quest_device_command_param_t) {
        .type = QUEST_DEVICE_COMMAND_PARAM_NUMBER,
        .optional = false,
    };
    qd_system_copy(cmd->params[cmd->param_count - 1].key,
                   sizeof(cmd->params[cmd->param_count - 1].key),
                   "duration_ms");
    qd_system_copy(cmd->params[cmd->param_count - 1].label,
                   sizeof(cmd->params[cmd->param_count - 1].label),
                   "Duration ms");
}

static void qd_system_gpio_toggle_command(quest_device_command_t *cmd)
{
    qd_system_copy(cmd->id, sizeof(cmd->id), "toggle");
    qd_system_copy(cmd->label, sizeof(cmd->label), "GPIO toggle");
    qd_system_copy(cmd->capability, sizeof(cmd->capability), "gpio");
    qd_system_copy(cmd->command, sizeof(cmd->command), "gpio.toggle");
    qd_system_copy(cmd->default_args_json, sizeof(cmd->default_args_json), "{\"channel\":1}");
    qd_system_command_set_policy(cmd, true, false, false, false, QUEST_DEVICE_COMMAND_TIMEOUT_DEFAULT_MS, "normal");
    qd_system_relay_channel_param(cmd);
}

static void qd_system_gpio_get_state_command(quest_device_command_t *cmd)
{
    qd_system_copy(cmd->id, sizeof(cmd->id), "get_state");
    qd_system_copy(cmd->label, sizeof(cmd->label), "GPIO get state");
    qd_system_copy(cmd->capability, sizeof(cmd->capability), "gpio");
    qd_system_copy(cmd->command, sizeof(cmd->command), "gpio.get_state");
    qd_system_copy(cmd->default_args_json, sizeof(cmd->default_args_json), "{\"channel\":1}");
    qd_system_command_set_policy(cmd, true, false, false, false, QUEST_DEVICE_COMMAND_TIMEOUT_DEFAULT_MS, "normal");
    qd_system_relay_channel_param(cmd);
}

void quest_device_fill_system_gpio(quest_device_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    qd_system_copy(out->id, sizeof(out->id), QUEST_DEVICE_SYSTEM_GPIO_ID);
    qd_system_copy(out->client_id, sizeof(out->client_id), "internal");
    qd_system_copy(out->name, sizeof(out->name), "System GPIO");
    out->enabled = true;
    out->system_device = true;

    qd_system_gpio_set_command(&out->commands[out->command_count++]);
    qd_system_gpio_pulse_command(&out->commands[out->command_count++]);
    qd_system_gpio_toggle_command(&out->commands[out->command_count++]);
    qd_system_gpio_get_state_command(&out->commands[out->command_count++]);
    for (uint8_t channel = 1; channel <= 4; ++channel) {
        qd_system_add_channel_event(out, "gpio", "gpio", channel, "changed", "changed");
        qd_system_add_channel_event(out, "gpio", "gpio", channel, "high", "HIGH");
        qd_system_add_channel_event(out, "gpio", "gpio", channel, "low", "LOW");
        qd_system_add_channel_event(out, "gpio", "gpio", channel, "active", "active");
        qd_system_add_channel_event(out, "gpio", "gpio", channel, "inactive", "inactive");
    }
}

esp_err_t quest_device_system_gpio_command(const char *command_id,
                                           quest_device_command_t *out)
{
    quest_device_command_t cmd = {0};
    if (!command_id || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(command_id, "set") == 0) {
        qd_system_gpio_set_command(&cmd);
    } else if (strcmp(command_id, "pulse") == 0) {
        qd_system_gpio_pulse_command(&cmd);
    } else if (strcmp(command_id, "toggle") == 0) {
        qd_system_gpio_toggle_command(&cmd);
    } else if (strcmp(command_id, "get_state") == 0) {
        qd_system_gpio_get_state_command(&cmd);
    } else {
        return ESP_ERR_NOT_FOUND;
    }
    *out = cmd;
    return ESP_OK;
}

esp_err_t quest_device_system_gpio_event(const char *event_id,
                                         quest_device_event_t *out)
{
    return qd_system_get_channel_event(event_id, "gpio", "gpio", false, out);
}
