#include "device_control_ingest_internal.h"

#include <string.h>

#include "quest_common_utils.h"
#include "scenehub_command_result.h"

typedef struct {
    const char *key;
    size_t key_len;
    const char *value;
    size_t value_len;
} dci_json_pair_t;

static esp_err_t dci_json_iterate_object(const char *json, size_t json_len, const char **cursor, const char **end);

static const char *dci_json_skip_ws(const char *p, const char *end)
{
    while (p && p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
        ++p;
    }
    return p;
}

static esp_err_t dci_json_skip_string(const char **cursor, const char *end)
{
    const char *p = cursor ? *cursor : NULL;
    if (!p || p >= end || *p != '"') {
        return ESP_ERR_INVALID_ARG;
    }
    for (++p; p < end; ++p) {
        if (*p == '\\') {
            if (p + 1 >= end) {
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

static esp_err_t dci_json_skip_value(const char **cursor, const char *end)
{
    const char *p = dci_json_skip_ws(cursor ? *cursor : NULL, end);
    if (!p || p >= end) {
        return ESP_ERR_INVALID_ARG;
    }
    if (*p == '"') {
        esp_err_t err = dci_json_skip_string(&p, end);
        if (err == ESP_OK) {
            *cursor = p;
        }
        return err;
    }
    if (*p == '{' || *p == '[') {
        char stack[8] = {0};
        int depth = 1;
        stack[0] = *p;
        for (++p; p < end; ++p) {
            if (*p == '"') {
                esp_err_t err = dci_json_skip_string(&p, end);
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
    while (p < end && *p != ',' && *p != '}') {
        ++p;
    }
    *cursor = p;
    return ESP_OK;
}

static esp_err_t dci_json_next_pair(const char **cursor, const char *end, dci_json_pair_t *out)
{
    const char *p = dci_json_skip_ws(cursor ? *cursor : NULL, end);
    const char *key_start = NULL;
    const char *value_end = NULL;
    if (!out || !p || p > end) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (p >= end) {
        return ESP_ERR_INVALID_ARG;
    }
    if (*p == '}') {
        *cursor = p + 1;
        return ESP_ERR_NOT_FOUND;
    }
    if (*p != '"') {
        return ESP_ERR_INVALID_ARG;
    }
    key_start = p + 1;
    for (++p; p < end; ++p) {
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
    p = dci_json_skip_ws(p, end);
    if (p >= end || *p != ':') {
        return ESP_ERR_INVALID_ARG;
    }
    p++;
    p = dci_json_skip_ws(p, end);
    out->value = p;
    if (dci_json_skip_value(&p, end) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    value_end = p;
    out->value_len = (size_t)(value_end - out->value);
    p = dci_json_skip_ws(p, end);
    if (p < end && *p == ',') {
        *cursor = p + 1;
        return ESP_OK;
    }
    if (p < end && *p == '}') {
        *cursor = p;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_ARG;
}

static bool dci_json_key_equals(const dci_json_pair_t *pair, const char *key)
{
    return pair && key && strlen(key) == pair->key_len && strncmp(pair->key, key, pair->key_len) == 0;
}

static esp_err_t dci_json_find_pair_in_object(const char *json,
                                              size_t json_len,
                                              const char *key,
                                              dci_json_pair_t *out)
{
    const char *cursor = NULL;
    const char *end = NULL;
    if (!json || !key || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (dci_json_iterate_object(json, json_len, &cursor, &end) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    for (;;) {
        dci_json_pair_t pair = {0};
        esp_err_t err = dci_json_next_pair(&cursor, end, &pair);
        if (err == ESP_ERR_NOT_FOUND) {
            return ESP_ERR_NOT_FOUND;
        }
        if (err != ESP_OK) {
            return err;
        }
        if (dci_json_key_equals(&pair, key)) {
            *out = pair;
            return ESP_OK;
        }
    }
}

static esp_err_t dci_json_copy_string_value(const char *value,
                                            size_t value_len,
                                            char *dst,
                                            size_t dst_size)
{
    const char *p = value;
    const char *end = value ? value + value_len : NULL;
    size_t written = 0;
    if (!value || !dst || dst_size == 0 || value_len < 2 || value[0] != '"' || value[value_len - 1] != '"') {
        return ESP_ERR_INVALID_ARG;
    }
    ++p;
    --end;
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
                if (written + 2 >= dst_size) {
                    return ESP_ERR_INVALID_ARG;
                }
                dst[written++] = '\\';
                dst[written++] = ch;
                continue;
            }
        }
        if (written + 1 >= dst_size) {
            return ESP_ERR_INVALID_ARG;
        }
        dst[written++] = ch;
    }
    dst[written] = '\0';
    return ESP_OK;
}

static esp_err_t dci_json_copy_string_field(const dci_json_pair_t *pair, char *dst, size_t dst_size)
{
    if (!pair || !dst || dst_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return dci_json_copy_string_value(pair->value, pair->value_len, dst, dst_size);
}

static esp_err_t dci_json_copy_raw_value(const char *value,
                                         size_t value_len,
                                         char *dst,
                                         size_t dst_size)
{
    const char *start = dci_json_skip_ws(value, value ? value + value_len : NULL);
    const char *end = value ? value + value_len : NULL;
    size_t copy_len = 0;
    if (!value || !dst || dst_size == 0 || !start || start > end) {
        return ESP_ERR_INVALID_ARG;
    }
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        --end;
    }
    copy_len = (size_t)(end - start);
    if (copy_len + 1 > dst_size) {
        return ESP_ERR_INVALID_ARG;
    }
    if (copy_len > 0) {
        memcpy(dst, start, copy_len);
    }
    dst[copy_len] = '\0';
    return ESP_OK;
}

static esp_err_t dci_json_copy_raw_field(const char *json,
                                         size_t json_len,
                                         const char *key,
                                         char *dst,
                                         size_t dst_size)
{
    dci_json_pair_t pair = {0};
    esp_err_t err = dci_json_find_pair_in_object(json, json_len, key, &pair);
    if (err != ESP_OK) {
        return err;
    }
    return dci_json_copy_raw_value(pair.value, pair.value_len, dst, dst_size);
}

static esp_err_t dci_json_parse_u64_value(const char *value, size_t value_len, uint64_t *out)
{
    const char *p = value;
    const char *end = value ? value + value_len : NULL;
    uint64_t result = 0;
    bool has_digit = false;
    if (!value || !out || value_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (*p == '-') {
        return ESP_ERR_INVALID_ARG;
    }
    while (p < end && *p >= '0' && *p <= '9') {
        uint64_t digit = (uint64_t)(*p - '0');
        has_digit = true;
        if (result > (UINT64_MAX - digit) / 10ULL) {
            return ESP_ERR_INVALID_ARG;
        }
        result = result * 10ULL + digit;
        ++p;
    }
    if (!has_digit || p != end) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = result;
    return ESP_OK;
}

static esp_err_t dci_json_parse_bool_value(const char *value, size_t value_len, bool *out)
{
    uint64_t numeric = 0;
    if (!value || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (value_len == 4 && strncmp(value, "true", 4) == 0) {
        *out = true;
        return ESP_OK;
    }
    if (value_len == 5 && strncmp(value, "false", 5) == 0) {
        *out = false;
        return ESP_OK;
    }
    if (dci_json_parse_u64_value(value, value_len, &numeric) == ESP_OK) {
        *out = numeric != 0;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_ARG;
}

static esp_err_t dci_json_iterate_object(const char *json, size_t json_len, const char **cursor, const char **end)
{
    const char *p = dci_json_skip_ws(json, json ? json + json_len : NULL);
    const char *object_end = json ? json + json_len : NULL;
    if (!json || !cursor || !end || !p || p >= object_end || *p != '{') {
        return ESP_ERR_INVALID_ARG;
    }
    *cursor = p + 1;
    *end = object_end;
    return ESP_OK;
}

static esp_err_t dci_json_find_bool_in_object(const char *json,
                                              size_t json_len,
                                              const char *key,
                                              bool fallback,
                                              bool *out)
{
    const char *cursor = NULL;
    const char *end = NULL;
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = fallback;
    if (dci_json_iterate_object(json, json_len, &cursor, &end) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    for (;;) {
        dci_json_pair_t pair = {0};
        esp_err_t err = dci_json_next_pair(&cursor, end, &pair);
        if (err == ESP_ERR_NOT_FOUND) {
            return ESP_OK;
        }
        if (err != ESP_OK) {
            return err;
        }
        if (dci_json_key_equals(&pair, key)) {
            return dci_json_parse_bool_value(pair.value, pair.value_len, out);
        }
    }
}

static esp_err_t dci_json_validate_consumed(const char *cursor, const char *end)
{
    const char *tail = dci_json_skip_ws(cursor, end);
    return (tail == end) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t dci_apply_heartbeat_text_locked(dci_slot_t *slot, const char *json, uint64_t rx_ms)
{
    const char *cursor = NULL;
    const char *end = NULL;
    if (!slot || !json) {
        return ESP_ERR_INVALID_ARG;
    }
    if (dci_json_iterate_object(json, strlen(json), &cursor, &end) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    slot->state.has_heartbeat = true;
    slot->state.heartbeat_rx_ms = rx_ms;
    for (;;) {
        dci_json_pair_t pair = {0};
        uint64_t value = 0;
        esp_err_t err = dci_json_next_pair(&cursor, end, &pair);
        if (err == ESP_ERR_NOT_FOUND) {
            break;
        }
        if (err != ESP_OK) {
            return err;
        }
        if (dci_json_key_equals(&pair, "ts_ms")) {
            if (dci_json_parse_u64_value(pair.value, pair.value_len, &value) == ESP_OK) {
                slot->state.heartbeat_ts_ms = value;
            }
        } else if (dci_json_key_equals(&pair, "uptime_ms")) {
            if (dci_json_parse_u64_value(pair.value, pair.value_len, &value) == ESP_OK) {
                slot->state.heartbeat_uptime_ms = value;
            }
        } else if (dci_json_key_equals(&pair, "status_seq")) {
            if (dci_json_parse_u64_value(pair.value, pair.value_len, &value) == ESP_OK &&
                value <= UINT32_MAX) {
                slot->state.heartbeat_status_seq = (uint32_t)value;
            }
        } else if (dci_json_key_equals(&pair, "boot_id")) {
            (void)dci_json_copy_string_field(&pair,
                                             slot->state.heartbeat_boot_id,
                                             sizeof(slot->state.heartbeat_boot_id));
        }
    }
    if (dci_json_validate_consumed(cursor, end) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    slot->state.heartbeat_count++;
    return ESP_OK;
}

esp_err_t dci_apply_status_text_locked(dci_slot_t *slot, const char *json, uint64_t rx_ms)
{
    const char *cursor = NULL;
    const char *end = NULL;
    if (!slot || !json) {
        return ESP_ERR_INVALID_ARG;
    }
    if (dci_json_iterate_object(json, strlen(json), &cursor, &end) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    slot->state.has_status = true;
    slot->state.status_rx_ms = rx_ms;
    for (;;) {
        dci_json_pair_t pair = {0};
        uint64_t value = 0;
        bool flag = false;
        esp_err_t err = dci_json_next_pair(&cursor, end, &pair);
        if (err == ESP_ERR_NOT_FOUND) {
            break;
        }
        if (err != ESP_OK) {
            return err;
        }
        if (dci_json_key_equals(&pair, "ts_ms")) {
            if (dci_json_parse_u64_value(pair.value, pair.value_len, &value) == ESP_OK) {
                slot->state.status_ts_ms = value;
            }
        } else if (dci_json_key_equals(&pair, "boot_id")) {
            (void)dci_json_copy_string_field(&pair,
                                             slot->state.status_boot_id,
                                             sizeof(slot->state.status_boot_id));
        } else if (dci_json_key_equals(&pair, "fw_version")) {
            (void)dci_json_copy_string_field(&pair,
                                             slot->state.status_fw_version,
                                             sizeof(slot->state.status_fw_version));
        } else if (dci_json_key_equals(&pair, "mode")) {
            (void)dci_json_copy_string_field(&pair,
                                             slot->state.status_mode,
                                             sizeof(slot->state.status_mode));
        } else if (dci_json_key_equals(&pair, "state")) {
            (void)dci_json_copy_string_field(&pair,
                                             slot->state.status_state,
                                             sizeof(slot->state.status_state));
        } else if (dci_json_key_equals(&pair, "health")) {
            (void)dci_json_copy_string_field(&pair,
                                             slot->state.status_health,
                                             sizeof(slot->state.status_health));
        } else if (dci_json_key_equals(&pair, "runtime")) {
            if (dci_json_find_bool_in_object(pair.value,
                                             pair.value_len,
                                             "active",
                                             slot->state.status_runtime_active,
                                             &flag) == ESP_OK) {
                slot->state.status_runtime_active = flag;
            }
        } else if (dci_json_key_equals(&pair, "runtime_active")) {
            if (dci_json_parse_bool_value(pair.value, pair.value_len, &flag) == ESP_OK) {
                slot->state.status_runtime_active = flag;
            }
        }
    }
    if (dci_json_validate_consumed(cursor, end) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    slot->state.status_count++;
    return ESP_OK;
}

esp_err_t dci_apply_diag_text_locked(dci_slot_t *slot, const char *json, uint64_t rx_ms)
{
    const char *cursor = NULL;
    const char *end = NULL;
    if (!slot || !json) {
        return ESP_ERR_INVALID_ARG;
    }
    if (dci_json_iterate_object(json, strlen(json), &cursor, &end) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    slot->state.has_diag = true;
    slot->state.diag_rx_ms = rx_ms;
    for (;;) {
        dci_json_pair_t pair = {0};
        uint64_t value = 0;
        esp_err_t err = dci_json_next_pair(&cursor, end, &pair);
        if (err == ESP_ERR_NOT_FOUND) {
            break;
        }
        if (err != ESP_OK) {
            return err;
        }
        if (dci_json_key_equals(&pair, "ts_ms")) {
            if (dci_json_parse_u64_value(pair.value, pair.value_len, &value) == ESP_OK) {
                slot->state.diag_ts_ms = value;
            }
        } else if (dci_json_key_equals(&pair, "level")) {
            (void)dci_json_copy_string_field(&pair,
                                             slot->state.diag_level,
                                             sizeof(slot->state.diag_level));
        } else if (dci_json_key_equals(&pair, "code")) {
            (void)dci_json_copy_string_field(&pair,
                                             slot->state.diag_code,
                                             sizeof(slot->state.diag_code));
        } else if (dci_json_key_equals(&pair, "message")) {
            (void)dci_json_copy_string_field(&pair,
                                             slot->state.diag_message,
                                             sizeof(slot->state.diag_message));
        }
    }
    if (dci_json_validate_consumed(cursor, end) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    slot->state.diag_count++;
    return ESP_OK;
}

esp_err_t dci_apply_result_text_locked(dci_slot_t *slot, const char *json, uint64_t rx_ms)
{
    const char *cursor = NULL;
    const char *end = NULL;
    const char *data_value = NULL;
    size_t data_value_len = 0;
    char status[DEVICE_CONTROL_INGEST_RESULT_STATUS_MAX_LEN] = {0};
    dci_json_pair_t error_pair = {0};
    if (!slot || !json) {
        return ESP_ERR_INVALID_ARG;
    }
    if (dci_json_iterate_object(json, strlen(json), &cursor, &end) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    slot->state.has_result = true;
    slot->state.result_rx_ms = rx_ms;
    slot->state.result_error_code[0] = '\0';
    slot->state.result_message[0] = '\0';
    slot->state.result_data_json[0] = '\0';
    for (;;) {
        dci_json_pair_t pair = {0};
        uint64_t value = 0;
        esp_err_t err = dci_json_next_pair(&cursor, end, &pair);
        if (err == ESP_ERR_NOT_FOUND) {
            break;
        }
        if (err != ESP_OK) {
            return err;
        }
        if (dci_json_key_equals(&pair, "ts_ms")) {
            if (dci_json_parse_u64_value(pair.value, pair.value_len, &value) == ESP_OK) {
                slot->state.result_ts_ms = value;
            }
        } else if (dci_json_key_equals(&pair, "request_id")) {
            (void)dci_json_copy_string_field(&pair,
                                             slot->state.result_request_id,
                                             sizeof(slot->state.result_request_id));
        } else if (dci_json_key_equals(&pair, "command")) {
            (void)dci_json_copy_string_field(&pair,
                                             slot->state.result_command,
                                             sizeof(slot->state.result_command));
        } else if (dci_json_key_equals(&pair, "status")) {
            (void)dci_json_copy_string_field(&pair, status, sizeof(status));
        } else if (dci_json_key_equals(&pair, "error_code")) {
            (void)dci_json_copy_string_field(&pair,
                                             slot->state.result_error_code,
                                             sizeof(slot->state.result_error_code));
        } else if (dci_json_key_equals(&pair, "message")) {
            (void)dci_json_copy_string_field(&pair,
                                             slot->state.result_message,
                                             sizeof(slot->state.result_message));
        } else if (dci_json_key_equals(&pair, "data")) {
            data_value = pair.value;
            data_value_len = pair.value_len;
        } else if (dci_json_key_equals(&pair, "error")) {
            error_pair = pair;
        }
    }
    if (dci_json_validate_consumed(cursor, end) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    quest_str_copy(slot->state.result_status,
                   sizeof(slot->state.result_status),
                   scenehub_command_result_normalize(status));
    if (data_value && data_value_len > 0) {
        if (strcmp(slot->state.result_command, "describe_interface") == 0) {
            esp_err_t cache_err =
                dci_store_describe_interface_data_locked(slot->state.device_id,
                                                         slot->state.result_request_id,
                                                         data_value,
                                                         data_value_len,
                                                         rx_ms);
            if (cache_err != ESP_OK) {
                return cache_err;
            }
        } else {
            (void)dci_json_copy_raw_value(data_value,
                                          data_value_len,
                                          slot->state.result_data_json,
                                          sizeof(slot->state.result_data_json));
        }
    }
    if (error_pair.value && (!slot->state.result_error_code[0] || !slot->state.result_message[0])) {
        if (!slot->state.result_error_code[0]) {
            (void)dci_json_copy_raw_field(error_pair.value,
                                          error_pair.value_len,
                                          "code",
                                          slot->state.result_error_code,
                                          sizeof(slot->state.result_error_code));
            if (slot->state.result_error_code[0] == '"') {
                char decoded[DEVICE_CONTROL_INGEST_ERROR_CODE_MAX_LEN] = {0};
                if (dci_json_copy_string_value(slot->state.result_error_code,
                                               strlen(slot->state.result_error_code),
                                               decoded,
                                               sizeof(decoded)) == ESP_OK) {
                    quest_str_copy(slot->state.result_error_code,
                                   sizeof(slot->state.result_error_code),
                                   decoded);
                }
            }
        }
        if (!slot->state.result_message[0]) {
            (void)dci_json_copy_raw_field(error_pair.value,
                                          error_pair.value_len,
                                          "message",
                                          slot->state.result_message,
                                          sizeof(slot->state.result_message));
            if (slot->state.result_message[0] == '"') {
                char decoded[DEVICE_CONTROL_INGEST_MESSAGE_MAX_LEN] = {0};
                if (dci_json_copy_string_value(slot->state.result_message,
                                               strlen(slot->state.result_message),
                                               decoded,
                                               sizeof(decoded)) == ESP_OK) {
                    quest_str_copy(slot->state.result_message,
                                   sizeof(slot->state.result_message),
                                   decoded);
                }
            }
        }
    }
    slot->state.result_count++;
    return ESP_OK;
}

esp_err_t dci_apply_event_text_locked(dci_slot_t *slot, const char *json, uint64_t rx_ms)
{
    const char *cursor = NULL;
    const char *end = NULL;
    if (!slot || !json) {
        return ESP_ERR_INVALID_ARG;
    }
    if (dci_json_iterate_object(json, strlen(json), &cursor, &end) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    slot->state.has_event = true;
    slot->state.event_rx_ms = rx_ms;
    slot->state.event_name[0] = '\0';
    slot->state.event_args_json[0] = '\0';
    for (;;) {
        dci_json_pair_t pair = {0};
        uint64_t value = 0;
        esp_err_t err = dci_json_next_pair(&cursor, end, &pair);
        if (err == ESP_ERR_NOT_FOUND) {
            break;
        }
        if (err != ESP_OK) {
            return err;
        }
        if (dci_json_key_equals(&pair, "ts_ms")) {
            if (dci_json_parse_u64_value(pair.value, pair.value_len, &value) == ESP_OK) {
                slot->state.event_ts_ms = value;
            }
        } else if (dci_json_key_equals(&pair, "event")) {
            (void)dci_json_copy_string_field(&pair,
                                             slot->state.event_name,
                                             sizeof(slot->state.event_name));
        } else if (dci_json_key_equals(&pair, "args")) {
            (void)dci_json_copy_raw_value(pair.value,
                                          pair.value_len,
                                          slot->state.event_args_json,
                                          sizeof(slot->state.event_args_json));
        }
    }
    if (dci_json_validate_consumed(cursor, end) != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!slot->state.event_name[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    slot->state.event_count++;
    return ESP_OK;
}
