#include "scenehub_events.h"

#include <string.h>

#include "quest_common_utils.h"

static bool scenehub_streq(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

static bool scenehub_event_is_text_event_type(scenehub_event_type_t type)
{
    switch (type) {
    case SCENEHUB_EVENT_CARD_OK:
    case SCENEHUB_EVENT_CARD_BAD:
    case SCENEHUB_EVENT_RELAY_CMD:
    case SCENEHUB_EVENT_AUDIO_PLAY:
    case SCENEHUB_EVENT_AUDIO_FINISHED:
    case SCENEHUB_EVENT_VOLUME_SET:
    case SCENEHUB_EVENT_WEB_COMMAND:
    case SCENEHUB_EVENT_SYSTEM_STATUS:
    case SCENEHUB_EVENT_SCENARIO_TRIGGER:
    case SCENEHUB_EVENT_DEVICE_CONFIG_CHANGED:
    case SCENEHUB_EVENT_MQTT_MESSAGE:
    case SCENEHUB_EVENT_FLAG_CHANGED:
    case SCENEHUB_EVENT_RUNTIME_CONTROL:
        return true;
    case SCENEHUB_EVENT_NONE:
    case SCENEHUB_EVENT_DEVICE_STATUS:
    case SCENEHUB_EVENT_DEVICE_RUNTIME:
    case SCENEHUB_EVENT_DEVICE_CONTROL:
    default:
        return false;
    }
}

static esp_err_t scenehub_event_make_device_control(scenehub_event_t *out,
                                                    const char *device_id,
                                                    const char *action_id,
                                                    const char *source,
                                                    const char *payload,
                                                    const char *args_json,
                                                    uint64_t timestamp_ms)
{
    if (!out || !device_id || !device_id[0] || !action_id || !action_id[0] || !source || !source[0]) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    out->type = SCENEHUB_EVENT_DEVICE_CONTROL;
    out->payload_type = SCENEHUB_EVENT_PAYLOAD_DEVICE_CONTROL;
    quest_str_copy(out->payload, sizeof(out->payload), payload);
    quest_str_copy(out->data.device_control.device_id,
                   sizeof(out->data.device_control.device_id),
                   device_id);
    quest_str_copy(out->data.device_control.action_id,
                   sizeof(out->data.device_control.action_id),
                   action_id);
    quest_str_copy(out->data.device_control.source,
                   sizeof(out->data.device_control.source),
                   source);
    quest_str_copy(out->data.device_control.args_json,
                   sizeof(out->data.device_control.args_json),
                   args_json);
    out->data.device_control.timestamp_ms = timestamp_ms;
    return scenehub_event_is_valid(out) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

const char *scenehub_event_type_to_string(scenehub_event_type_t type)
{
    switch (type) {
    case SCENEHUB_EVENT_CARD_OK:
        return "card_ok";
    case SCENEHUB_EVENT_CARD_BAD:
        return "card_bad";
    case SCENEHUB_EVENT_RELAY_CMD:
        return "relay_cmd";
    case SCENEHUB_EVENT_AUDIO_PLAY:
        return "audio_play";
    case SCENEHUB_EVENT_AUDIO_FINISHED:
        return "audio_finished";
    case SCENEHUB_EVENT_VOLUME_SET:
        return "volume_set";
    case SCENEHUB_EVENT_WEB_COMMAND:
        return "web_command";
    case SCENEHUB_EVENT_SYSTEM_STATUS:
        return "system_status";
    case SCENEHUB_EVENT_SCENARIO_TRIGGER:
        return "scenario_trigger";
    case SCENEHUB_EVENT_DEVICE_CONFIG_CHANGED:
        return "device_config_changed";
    case SCENEHUB_EVENT_MQTT_MESSAGE:
        return "mqtt_message";
    case SCENEHUB_EVENT_FLAG_CHANGED:
        return "flag_changed";
    case SCENEHUB_EVENT_DEVICE_STATUS:
        return "device_status";
    case SCENEHUB_EVENT_DEVICE_RUNTIME:
        return "device_runtime";
    case SCENEHUB_EVENT_DEVICE_CONTROL:
        return "device_control";
    case SCENEHUB_EVENT_RUNTIME_CONTROL:
        return "runtime_control";
    case SCENEHUB_EVENT_NONE:
    default:
        return "none";
    }
}

const char *scenehub_event_payload_type_to_string(scenehub_event_payload_type_t type)
{
    switch (type) {
    case SCENEHUB_EVENT_PAYLOAD_TEXT:
        return "text";
    case SCENEHUB_EVENT_PAYLOAD_DEVICE_STATUS:
        return "device_status";
    case SCENEHUB_EVENT_PAYLOAD_DEVICE_RUNTIME:
        return "device_runtime";
    case SCENEHUB_EVENT_PAYLOAD_DEVICE_CONTROL:
        return "device_control";
    default:
        return "unknown";
    }
}

const char *scenehub_event_source_id(const scenehub_event_t *event)
{
    if (!event) {
        return "";
    }
    switch (event->payload_type) {
    case SCENEHUB_EVENT_PAYLOAD_DEVICE_STATUS:
        return event->data.device_status.device_id;
    case SCENEHUB_EVENT_PAYLOAD_DEVICE_RUNTIME:
        return event->data.device_runtime.device_id;
    case SCENEHUB_EVENT_PAYLOAD_DEVICE_CONTROL:
        return event->data.device_control.device_id;
    case SCENEHUB_EVENT_PAYLOAD_TEXT:
    default:
        return event->topic;
    }
}

bool scenehub_event_is_device_status(const scenehub_event_t *event)
{
    return event &&
           event->type == SCENEHUB_EVENT_DEVICE_STATUS &&
           event->payload_type == SCENEHUB_EVENT_PAYLOAD_DEVICE_STATUS;
}

bool scenehub_event_is_device_runtime(const scenehub_event_t *event)
{
    return event &&
           event->type == SCENEHUB_EVENT_DEVICE_RUNTIME &&
           event->payload_type == SCENEHUB_EVENT_PAYLOAD_DEVICE_RUNTIME;
}

bool scenehub_event_is_device_control(const scenehub_event_t *event)
{
    return event &&
           event->type == SCENEHUB_EVENT_DEVICE_CONTROL &&
           event->payload_type == SCENEHUB_EVENT_PAYLOAD_DEVICE_CONTROL;
}

bool scenehub_event_is_device_control_event(const scenehub_event_t *event)
{
    return scenehub_event_is_device_control(event) &&
           scenehub_streq(event->data.device_control.source, SCENEHUB_DEVICE_CONTROL_SOURCE_EVENT);
}

bool scenehub_event_is_device_control_result(const scenehub_event_t *event)
{
    return scenehub_event_is_device_control(event) &&
           scenehub_streq(event->data.device_control.source, SCENEHUB_DEVICE_CONTROL_SOURCE_RESULT);
}

bool scenehub_event_matches_device(const scenehub_event_t *event, const char *device_id)
{
    const char *source_id = scenehub_event_source_id(event);
    return device_id && device_id[0] && source_id && strcmp(source_id, device_id) == 0;
}

bool scenehub_event_matches_action(const scenehub_event_t *event, const char *action_id)
{
    return scenehub_event_is_device_control(event) &&
           action_id &&
           action_id[0] &&
           strcmp(event->data.device_control.action_id, action_id) == 0;
}

bool scenehub_event_is_valid(const scenehub_event_t *event)
{
    if (!event || event->type == SCENEHUB_EVENT_NONE) {
        return false;
    }

    switch (event->payload_type) {
    case SCENEHUB_EVENT_PAYLOAD_TEXT:
        return scenehub_event_is_text_event_type(event->type);
    case SCENEHUB_EVENT_PAYLOAD_DEVICE_STATUS:
        return scenehub_event_is_device_status(event) &&
               event->data.device_status.device_id[0] != '\0';
    case SCENEHUB_EVENT_PAYLOAD_DEVICE_RUNTIME:
        return scenehub_event_is_device_runtime(event) &&
               event->data.device_runtime.device_id[0] != '\0';
    case SCENEHUB_EVENT_PAYLOAD_DEVICE_CONTROL:
        return scenehub_event_is_device_control(event) &&
               event->data.device_control.device_id[0] != '\0' &&
               event->data.device_control.action_id[0] != '\0' &&
               (scenehub_streq(event->data.device_control.source, SCENEHUB_DEVICE_CONTROL_SOURCE_EVENT) ||
                scenehub_streq(event->data.device_control.source, SCENEHUB_DEVICE_CONTROL_SOURCE_RESULT));
    default:
        return false;
    }
}

esp_err_t scenehub_event_make_text(scenehub_event_t *out,
                                   scenehub_event_type_t type,
                                   const char *topic,
                                   const char *payload)
{
    if (!out || !scenehub_event_is_text_event_type(type)) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    out->type = type;
    out->payload_type = SCENEHUB_EVENT_PAYLOAD_TEXT;
    quest_str_copy(out->topic, sizeof(out->topic), topic);
    quest_str_copy(out->payload, sizeof(out->payload), payload);
    return scenehub_event_is_valid(out) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t scenehub_event_make_flag_changed(scenehub_event_t *out,
                                           const char *flag_name,
                                           bool value)
{
    if (!flag_name || !flag_name[0]) {
        return ESP_ERR_INVALID_ARG;
    }

    return scenehub_event_make_text(out,
                                    SCENEHUB_EVENT_FLAG_CHANGED,
                                    flag_name,
                                    value ? "true" : "false");
}

esp_err_t scenehub_event_make_device_status(scenehub_event_t *out,
                                            const char *device_id,
                                            const char *connectivity,
                                            const char *health,
                                            const char *state,
                                            uint64_t timestamp_ms)
{
    if (!out || !device_id || !device_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    out->type = SCENEHUB_EVENT_DEVICE_STATUS;
    out->payload_type = SCENEHUB_EVENT_PAYLOAD_DEVICE_STATUS;
    quest_str_copy(out->payload, sizeof(out->payload), state);
    quest_str_copy(out->data.device_status.device_id,
                   sizeof(out->data.device_status.device_id),
                   device_id);
    quest_str_copy(out->data.device_status.connectivity,
                   sizeof(out->data.device_status.connectivity),
                   connectivity);
    quest_str_copy(out->data.device_status.health,
                   sizeof(out->data.device_status.health),
                   health);
    quest_str_copy(out->data.device_status.state,
                   sizeof(out->data.device_status.state),
                   state);
    out->data.device_status.timestamp_ms = timestamp_ms;
    return scenehub_event_is_valid(out) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t scenehub_event_make_device_runtime(scenehub_event_t *out,
                                             const char *device_id,
                                             const char *runtime_type,
                                             const char *state,
                                             bool active,
                                             uint64_t timestamp_ms)
{
    if (!out || !device_id || !device_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    out->type = SCENEHUB_EVENT_DEVICE_RUNTIME;
    out->payload_type = SCENEHUB_EVENT_PAYLOAD_DEVICE_RUNTIME;
    quest_str_copy(out->payload, sizeof(out->payload), state);
    quest_str_copy(out->data.device_runtime.device_id,
                   sizeof(out->data.device_runtime.device_id),
                   device_id);
    quest_str_copy(out->data.device_runtime.runtime_type,
                   sizeof(out->data.device_runtime.runtime_type),
                   runtime_type);
    quest_str_copy(out->data.device_runtime.state,
                   sizeof(out->data.device_runtime.state),
                   state);
    out->data.device_runtime.active = active;
    out->data.device_runtime.timestamp_ms = timestamp_ms;
    return scenehub_event_is_valid(out) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t scenehub_event_make_device_control_event(scenehub_event_t *out,
                                                   const char *device_id,
                                                   const char *action_id,
                                                   const char *args_json,
                                                   uint64_t timestamp_ms)
{
    return scenehub_event_make_device_control(out,
                                              device_id,
                                              action_id,
                                              SCENEHUB_DEVICE_CONTROL_SOURCE_EVENT,
                                              SCENEHUB_DEVICE_CONTROL_SOURCE_EVENT,
                                              args_json,
                                              timestamp_ms);
}

esp_err_t scenehub_event_make_device_control_result(scenehub_event_t *out,
                                                    const char *device_id,
                                                    const char *request_id,
                                                    const char *result_payload,
                                                    uint64_t timestamp_ms)
{
    return scenehub_event_make_device_control(out,
                                              device_id,
                                              request_id,
                                              SCENEHUB_DEVICE_CONTROL_SOURCE_RESULT,
                                              result_payload,
                                              NULL,
                                              timestamp_ms);
}
