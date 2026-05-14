#include "command_executor_internal.h"

#include <stdio.h>
#include <string.h>

#include "esp_timer.h"
#include "mqtt_core.h"
#include "quest_common_utils.h"

#define COMMAND_EXECUTOR_MQTT_PAYLOAD_MAX_LEN 512

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} mqtt_json_writer_t;

typedef struct {
    const char *pair_start;
    size_t pair_len;
    const char *key;
    size_t key_len;
} mqtt_json_pair_t;

static const char *json_skip_ws(const char *p)
{
    while (p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
        ++p;
    }
    return p;
}

static esp_err_t json_skip_string(const char **cursor)
{
    const char *p = cursor ? *cursor : NULL;
    if (!p || *p != '"') {
        return ESP_ERR_INVALID_ARG;
    }
    for (++p; *p; ++p) {
        if (*p == '\\') {
            if (!p[1]) {
                return ESP_ERR_INVALID_ARG;
            }
            ++p;
            continue;
        }
        if (*p == '"') {
            *cursor = p + 1;
            return ESP_OK;
        }
    }
    return ESP_ERR_INVALID_ARG;
}

static esp_err_t json_skip_value(const char **cursor)
{
    const char *p = json_skip_ws(cursor ? *cursor : NULL);
    if (!p || !*p) {
        return ESP_ERR_INVALID_ARG;
    }
    if (*p == '"') {
        esp_err_t err = json_skip_string(&p);
        if (err == ESP_OK) {
            *cursor = p;
        }
        return err;
    }
    if (*p == '{' || *p == '[') {
        char stack[8] = {0};
        int depth = 1;
        stack[0] = *p;
        for (++p; *p; ++p) {
            if (*p == '"') {
                esp_err_t err = json_skip_string(&p);
                if (err != ESP_OK) {
                    return err;
                }
                --p;
                continue;
            }
            if (*p == '{' || *p == '[') {
                if (depth >= (int)(sizeof(stack) / sizeof(stack[0]))) {
                    return ESP_ERR_INVALID_SIZE;
                }
                stack[depth++] = *p;
            } else if (*p == '}' || *p == ']') {
                char expected = stack[depth - 1] == '{' ? '}' : ']';
                if (*p != expected) {
                    return ESP_ERR_INVALID_ARG;
                }
                depth--;
                if (depth == 0) {
                    *cursor = p + 1;
                    return ESP_OK;
                }
            }
        }
        return ESP_ERR_INVALID_ARG;
    }
    while (*p && *p != ',' && *p != '}') {
        ++p;
    }
    *cursor = p;
    return ESP_OK;
}

static esp_err_t json_next_pair(const char **cursor, mqtt_json_pair_t *out)
{
    const char *p = json_skip_ws(cursor ? *cursor : NULL);
    const char *key_start = NULL;
    const char *value_end = NULL;
    if (!out || !p || !*p) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (*p == '}') {
        *cursor = p + 1;
        return ESP_ERR_NOT_FOUND;
    }
    if (*p != '"') {
        return ESP_ERR_INVALID_ARG;
    }
    out->pair_start = p;
    key_start = p + 1;
    for (++p; *p; ++p) {
        if (*p == '\\') {
            return ESP_ERR_NOT_SUPPORTED;
        }
        if (*p == '"') {
            out->key = key_start;
            out->key_len = (size_t)(p - key_start);
            p++;
            break;
        }
    }
    if (!out->key) {
        return ESP_ERR_INVALID_ARG;
    }
    p = json_skip_ws(p);
    if (*p != ':') {
        return ESP_ERR_INVALID_ARG;
    }
    p++;
    if (json_skip_value(&p) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    value_end = p;
    p = json_skip_ws(p);
    out->pair_len = (size_t)(value_end - out->pair_start);
    if (*p == ',') {
        *cursor = p + 1;
        return ESP_OK;
    }
    if (*p == '}') {
        *cursor = p;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_ARG;
}

static bool json_object_has_key(const char *json, const char *key, size_t key_len)
{
    const char *p = json_skip_ws(json);
    if (!p || !*p || *p != '{') {
        return false;
    }
    p++;
    for (;;) {
        mqtt_json_pair_t pair = {0};
        esp_err_t err = json_next_pair(&p, &pair);
        if (err == ESP_ERR_NOT_FOUND) {
            return false;
        }
        if (err != ESP_OK) {
            return false;
        }
        if (pair.key_len == key_len && strncmp(pair.key, key, key_len) == 0) {
            return true;
        }
    }
}

static esp_err_t writer_putn(mqtt_json_writer_t *writer, const char *s, size_t len)
{
    if (!writer || !s) {
        return ESP_ERR_INVALID_ARG;
    }
    if (writer->len + len >= writer->cap) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(writer->buf + writer->len, s, len);
    writer->len += len;
    writer->buf[writer->len] = '\0';
    return ESP_OK;
}

static esp_err_t writer_puts(mqtt_json_writer_t *writer, const char *s)
{
    return writer_putn(writer, s, strlen(s));
}

static esp_err_t writer_put_escaped(mqtt_json_writer_t *writer, const char *s)
{
    esp_err_t err = writer_puts(writer, "\"");
    if (err != ESP_OK) {
        return err;
    }
    for (const char *p = s ? s : ""; *p; ++p) {
        switch (*p) {
        case '"':
            err = writer_puts(writer, "\\\"");
            break;
        case '\\':
            err = writer_puts(writer, "\\\\");
            break;
        case '\n':
            err = writer_puts(writer, "\\n");
            break;
        case '\r':
            err = writer_puts(writer, "\\r");
            break;
        case '\t':
            err = writer_puts(writer, "\\t");
            break;
        default:
            err = writer_putn(writer, p, 1);
            break;
        }
        if (err != ESP_OK) {
            return err;
        }
    }
    return writer_puts(writer, "\"");
}

static esp_err_t writer_put_pair(mqtt_json_writer_t *writer, const mqtt_json_pair_t *pair, bool *first)
{
    esp_err_t err;
    if (!writer || !pair || !first) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!*first) {
        err = writer_puts(writer, ",");
        if (err != ESP_OK) {
            return err;
        }
    }
    err = writer_putn(writer, pair->pair_start, pair->pair_len);
    if (err == ESP_OK) {
        *first = false;
    }
    return err;
}

static esp_err_t writer_put_object_pairs(mqtt_json_writer_t *writer,
                                         const char *json,
                                         const char *override_json,
                                         bool skip_overridden,
                                         bool *first)
{
    const char *p = json_skip_ws(json);
    if (!json || !json[0]) {
        return ESP_OK;
    }
    if (!p || *p != '{') {
        return ESP_ERR_INVALID_ARG;
    }
    p++;
    for (;;) {
        mqtt_json_pair_t pair = {0};
        esp_err_t err = json_next_pair(&p, &pair);
        if (err == ESP_ERR_NOT_FOUND) {
            return ESP_OK;
        }
        if (err != ESP_OK) {
            return err;
        }
        if (skip_overridden && json_object_has_key(override_json, pair.key, pair.key_len)) {
            continue;
        }
        err = writer_put_pair(writer, &pair, first);
        if (err != ESP_OK) {
            return err;
        }
    }
}

static esp_err_t build_command_payload(char *out,
                                       size_t out_size,
                                       const char *request_id,
                                       const char *command,
                                       const char *default_args_json,
                                       const char *params_json,
                                       int64_t now_ms)
{
    char ts_buf[32] = {0};
    bool first_arg = true;
    mqtt_json_writer_t writer = {
        .buf = out,
        .cap = out_size,
    };
    esp_err_t err = writer_puts(&writer, "{\"request_id\":");
    if (err == ESP_OK) err = writer_put_escaped(&writer, request_id);
    if (err == ESP_OK) err = writer_puts(&writer, ",\"command\":");
    if (err == ESP_OK) err = writer_put_escaped(&writer, command);
    if (err == ESP_OK) err = writer_puts(&writer, ",\"args\":{");
    if (err == ESP_OK) {
        err = writer_put_object_pairs(&writer, default_args_json, params_json, true, &first_arg);
    }
    if (err == ESP_OK) {
        err = writer_put_object_pairs(&writer, params_json, NULL, false, &first_arg);
    }
    if (err == ESP_OK) err = writer_puts(&writer, "},\"ts_ms\":");
    snprintf(ts_buf, sizeof(ts_buf), "%lld", (long long)now_ms);
    if (err == ESP_OK) err = writer_puts(&writer, ts_buf);
    if (err == ESP_OK) err = writer_puts(&writer, "}");
    return err;
}

esp_err_t command_executor_execute_mqtt(const char *client_id,
                                        const quest_device_command_t *command,
                                        const command_executor_request_t *request,
                                        command_executor_dispatch_t *out_dispatch,
                                        char *error,
                                        size_t error_size)
{
    char topic[96] = {0};
    char request_id[COMMAND_EXECUTOR_REQUEST_ID_MAX_LEN] = {0};
    char mqtt_payload[COMMAND_EXECUTOR_MQTT_PAYLOAD_MAX_LEN] = {0};
    esp_err_t err = ESP_OK;
    int64_t now_ms = esp_timer_get_time() / 1000;

    if (!client_id || !client_id[0] || !command || !request || !command->command[0]) {
        return command_executor_fail(error, error_size, ESP_ERR_INVALID_ARG, "device_command_invalid");
    }
    if (snprintf(topic, sizeof(topic), "cp/v1/dev/%s/control/command", client_id) >= (int)sizeof(topic)) {
        return command_executor_fail(error,
                                     error_size,
                                     ESP_ERR_INVALID_SIZE,
                                     "device_command_topic_too_long");
    }
    snprintf(request_id, sizeof(request_id), "req-%08llx", (unsigned long long)now_ms);

    err = build_command_payload(mqtt_payload,
                                sizeof(mqtt_payload),
                                request_id,
                                command->command,
                                command->default_args_json,
                                request->params_json,
                                now_ms);
    if (err != ESP_OK) {
        return command_executor_fail(error, error_size, err, "device_command_args_invalid");
    }
    err = mqtt_core_publish(topic, mqtt_payload);
    if (err != ESP_OK) {
        return command_executor_fail(error, error_size, err, "device_command_publish_failed");
    }
    if (command->result_required) {
        err = command_executor_track_pending(request_id,
                                             client_id,
                                             command->command,
                                             command->timeout_ms ? command->timeout_ms
                                                                 : QUEST_DEVICE_COMMAND_TIMEOUT_DEFAULT_MS);
        if (err != ESP_OK) {
            return command_executor_fail(error, error_size, err, "device_command_result_track_failed");
        }
    }
    if (out_dispatch) {
        memset(out_dispatch, 0, sizeof(*out_dispatch));
        out_dispatch->result_required = command->result_required;
        out_dispatch->timeout_ms = command->timeout_ms ? command->timeout_ms
                                                       : QUEST_DEVICE_COMMAND_TIMEOUT_DEFAULT_MS;
        quest_str_copy(out_dispatch->request_id, sizeof(out_dispatch->request_id), request_id);
        quest_str_copy(out_dispatch->source_id, sizeof(out_dispatch->source_id), client_id);
        quest_str_copy(out_dispatch->command, sizeof(out_dispatch->command), command->command);
    }
    return ESP_OK;
}
