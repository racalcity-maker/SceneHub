#include "command_executor_internal.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "quest_common_utils.h"

static SemaphoreHandle_t s_execute_mutex = NULL;
static StaticSemaphore_t s_execute_mutex_storage;
static portMUX_TYPE s_execute_mutex_init_lock = portMUX_INITIALIZER_UNLOCKED;
static EXT_RAM_BSS_ATTR quest_device_t s_execute_device;
static EXT_RAM_BSS_ATTR quest_device_command_t s_execute_command;

typedef struct {
    char client_id[QUEST_DEVICE_CLIENT_ID_MAX_LEN];
} command_executor_device_snapshot_t;

typedef enum {
    COMMAND_EXECUTOR_PARAM_MISSING = 0,
    COMMAND_EXECUTOR_PARAM_STRING,
    COMMAND_EXECUTOR_PARAM_NUMBER,
    COMMAND_EXECUTOR_PARAM_BOOL,
    COMMAND_EXECUTOR_PARAM_NULL,
    COMMAND_EXECUTOR_PARAM_INVALID,
} command_executor_param_type_t;

typedef struct {
    command_executor_param_type_t type;
    const char *value;
    size_t value_len;
    bool bool_value;
} command_executor_param_value_t;

static esp_err_t command_executor_ensure_execute_mutex(void)
{
    if (s_execute_mutex) {
        return ESP_OK;
    }
    portENTER_CRITICAL(&s_execute_mutex_init_lock);
    if (!s_execute_mutex) {
        s_execute_mutex = xSemaphoreCreateMutexStatic(&s_execute_mutex_storage);
    }
    portEXIT_CRITICAL(&s_execute_mutex_init_lock);
    return s_execute_mutex ? ESP_OK : ESP_ERR_NO_MEM;
}

static const char *params_skip_ws(const char *p)
{
    while (p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
        ++p;
    }
    return p;
}

static esp_err_t params_skip_string(const char **cursor)
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

static esp_err_t params_skip_value(const char **cursor)
{
    const char *p = params_skip_ws(cursor ? *cursor : NULL);
    int depth = 0;
    if (!p || !*p) {
        return ESP_ERR_INVALID_ARG;
    }
    if (*p == '"') {
        esp_err_t err = params_skip_string(&p);
        if (err == ESP_OK) {
            *cursor = p;
        }
        return err;
    }
    if (*p == '{' || *p == '[') {
        char open = *p;
        char close = open == '{' ? '}' : ']';
        depth = 1;
        for (++p; *p; ++p) {
            if (*p == '"') {
                esp_err_t err = params_skip_string(&p);
                if (err != ESP_OK) {
                    return err;
                }
                --p;
                continue;
            }
            if (*p == open) {
                depth++;
            } else if (*p == close) {
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

static bool params_key_equals(const char *json_key, size_t json_key_len, const char *key)
{
    return key && strlen(key) == json_key_len && strncmp(json_key, key, json_key_len) == 0;
}

static esp_err_t params_read_key(const char **cursor, const char **out_key, size_t *out_key_len)
{
    const char *p = params_skip_ws(cursor ? *cursor : NULL);
    const char *start = NULL;
    if (!p || *p != '"') {
        return ESP_ERR_INVALID_ARG;
    }
    start = p + 1;
    for (++p; *p; ++p) {
        if (*p == '\\') {
            return ESP_ERR_NOT_SUPPORTED;
        }
        if (*p == '"') {
            *out_key = start;
            *out_key_len = (size_t)(p - start);
            *cursor = p + 1;
            return ESP_OK;
        }
    }
    return ESP_ERR_INVALID_ARG;
}

static esp_err_t params_read_scalar(const char **cursor, command_executor_param_value_t *out)
{
    const char *p = params_skip_ws(cursor ? *cursor : NULL);
    const char *start = NULL;
    if (!p || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (*p == '"') {
        start = p + 1;
        esp_err_t err = params_skip_string(&p);
        if (err != ESP_OK) {
            out->type = COMMAND_EXECUTOR_PARAM_INVALID;
            return err;
        }
        out->type = COMMAND_EXECUTOR_PARAM_STRING;
        out->value = start;
        out->value_len = (size_t)((p - 1) - start);
        *cursor = p;
        return ESP_OK;
    }
    if (strncmp(p, "true", 4) == 0) {
        out->type = COMMAND_EXECUTOR_PARAM_BOOL;
        out->bool_value = true;
        *cursor = p + 4;
        return ESP_OK;
    }
    if (strncmp(p, "false", 5) == 0) {
        out->type = COMMAND_EXECUTOR_PARAM_BOOL;
        out->bool_value = false;
        *cursor = p + 5;
        return ESP_OK;
    }
    if (strncmp(p, "null", 4) == 0) {
        out->type = COMMAND_EXECUTOR_PARAM_NULL;
        *cursor = p + 4;
        return ESP_OK;
    }
    start = p;
    if (*p == '-') {
        ++p;
    }
    if (*p < '0' || *p > '9') {
        out->type = COMMAND_EXECUTOR_PARAM_INVALID;
        return ESP_ERR_INVALID_ARG;
    }
    while (*p >= '0' && *p <= '9') {
        ++p;
    }
    out->type = COMMAND_EXECUTOR_PARAM_NUMBER;
    out->value = start;
    out->value_len = (size_t)(p - start);
    *cursor = p;
    return ESP_OK;
}

static esp_err_t command_executor_params_find(const char *params_json,
                                              const char *key,
                                              command_executor_param_value_t *out)
{
    const char *p = params_skip_ws(params_json);
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->type = COMMAND_EXECUTOR_PARAM_MISSING;
    if (!p || !*p) {
        return ESP_ERR_NOT_FOUND;
    }
    if (*p != '{') {
        out->type = COMMAND_EXECUTOR_PARAM_INVALID;
        return ESP_ERR_INVALID_ARG;
    }
    p++;
    for (;;) {
        const char *json_key = NULL;
        size_t json_key_len = 0;
        esp_err_t err;
        p = params_skip_ws(p);
        if (*p == '}') {
            return ESP_ERR_NOT_FOUND;
        }
        err = params_read_key(&p, &json_key, &json_key_len);
        if (err != ESP_OK) {
            out->type = COMMAND_EXECUTOR_PARAM_INVALID;
            return err;
        }
        p = params_skip_ws(p);
        if (*p != ':') {
            out->type = COMMAND_EXECUTOR_PARAM_INVALID;
            return ESP_ERR_INVALID_ARG;
        }
        p++;
        if (params_key_equals(json_key, json_key_len, key)) {
            err = params_read_scalar(&p, out);
            if (err != ESP_OK) {
                return err;
            }
            p = params_skip_ws(p);
            if (*p == ',' || *p == '}' || *p == '\0') {
                return ESP_OK;
            }
            out->type = COMMAND_EXECUTOR_PARAM_INVALID;
            return ESP_ERR_INVALID_ARG;
        }
        err = params_skip_value(&p);
        if (err != ESP_OK) {
            out->type = COMMAND_EXECUTOR_PARAM_INVALID;
            return err;
        }
        p = params_skip_ws(p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == '}') {
            return ESP_ERR_NOT_FOUND;
        }
        out->type = COMMAND_EXECUTOR_PARAM_INVALID;
        return ESP_ERR_INVALID_ARG;
    }
}

static esp_err_t params_copy_json_string(const command_executor_param_value_t *value,
                                         char *out,
                                         size_t out_size)
{
    const char *p = value ? value->value : NULL;
    const char *end = p ? p + value->value_len : NULL;
    size_t written = 0;
    if (!value || value->type != COMMAND_EXECUTOR_PARAM_STRING || !out || out_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
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
            return ESP_ERR_INVALID_ARG;
        }
        out[written++] = ch;
    }
    out[written] = '\0';
    return ESP_OK;
}

esp_err_t command_executor_fail(char *error,
                                size_t error_size,
                                esp_err_t err,
                                const char *message)
{
    if (error && error_size > 0) {
        quest_str_copy(error, error_size, message ? message : "command_failed");
    }
    return err;
}

uint64_t command_executor_now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

esp_err_t command_executor_params_get_string(const char *params_json,
                                             const char *key,
                                             char *out,
                                             size_t out_size,
                                             bool required)
{
    command_executor_param_value_t value = {0};
    esp_err_t err;
    if (!key || !out || out_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    out[0] = '\0';
    if (!params_json || !params_json[0]) {
        return required ? ESP_ERR_INVALID_ARG : ESP_OK;
    }
    err = command_executor_params_find(params_json, key, &value);
    if (err == ESP_ERR_NOT_FOUND || value.type == COMMAND_EXECUTOR_PARAM_NULL) {
        return required ? ESP_ERR_INVALID_ARG : ESP_OK;
    }
    if (err != ESP_OK || value.type != COMMAND_EXECUTOR_PARAM_STRING) {
        return ESP_ERR_INVALID_ARG;
    }
    return params_copy_json_string(&value, out, out_size);
}

esp_err_t command_executor_params_get_int(const char *params_json,
                                          const char *key,
                                          int *out,
                                          bool required)
{
    command_executor_param_value_t value = {0};
    char tmp[24] = {0};
    char *end = NULL;
    long parsed = 0;
    esp_err_t err;
    if (!key || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!params_json || !params_json[0]) {
        return required ? ESP_ERR_INVALID_ARG : ESP_OK;
    }
    err = command_executor_params_find(params_json, key, &value);
    if (err == ESP_ERR_NOT_FOUND || value.type == COMMAND_EXECUTOR_PARAM_NULL) {
        return required ? ESP_ERR_INVALID_ARG : ESP_OK;
    }
    if (err != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    if (value.type == COMMAND_EXECUTOR_PARAM_NUMBER) {
        if (value.value_len >= sizeof(tmp)) {
            return ESP_ERR_INVALID_ARG;
        }
        memcpy(tmp, value.value, value.value_len);
    } else if (value.type == COMMAND_EXECUTOR_PARAM_STRING) {
        err = params_copy_json_string(&value, tmp, sizeof(tmp));
        if (err != ESP_OK || !tmp[0]) {
            return ESP_ERR_INVALID_ARG;
        }
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    parsed = strtol(tmp, &end, 10);
    if (!end || *end != '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    *out = (int)parsed;
    return ESP_OK;
}

esp_err_t command_executor_params_get_bool(const char *params_json,
                                           const char *key,
                                           bool *out,
                                           bool required)
{
    command_executor_param_value_t value = {0};
    char tmp[16] = {0};
    char *end = NULL;
    long parsed = 0;
    esp_err_t err;
    if (!key || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!params_json || !params_json[0]) {
        return required ? ESP_ERR_INVALID_ARG : ESP_OK;
    }
    err = command_executor_params_find(params_json, key, &value);
    if (err == ESP_ERR_NOT_FOUND || value.type == COMMAND_EXECUTOR_PARAM_NULL) {
        return required ? ESP_ERR_INVALID_ARG : ESP_OK;
    }
    if (err != ESP_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    if (value.type == COMMAND_EXECUTOR_PARAM_BOOL) {
        *out = value.bool_value;
        return ESP_OK;
    }
    if (value.type == COMMAND_EXECUTOR_PARAM_NUMBER) {
        if (value.value_len >= sizeof(tmp)) {
            return ESP_ERR_INVALID_ARG;
        }
        memcpy(tmp, value.value, value.value_len);
        parsed = strtol(tmp, &end, 10);
        if (!end || *end != '\0') {
            return ESP_ERR_INVALID_ARG;
        }
        *out = parsed != 0;
        return ESP_OK;
    }
    if (value.type == COMMAND_EXECUTOR_PARAM_STRING) {
        err = params_copy_json_string(&value, tmp, sizeof(tmp));
        if (err != ESP_OK) {
            return err;
        }
        *out = strcasecmp(tmp, "true") == 0 ||
               strcmp(tmp, "1") == 0 ||
               strcasecmp(tmp, "yes") == 0 ||
               strcasecmp(tmp, "on") == 0;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_ARG;
}

bool command_executor_command_name_is(const quest_device_command_t *command, const char *name)
{
    return command && name && strcmp(command->command, name) == 0;
}

esp_err_t command_executor_execute_resolved(const command_executor_request_t *request,
                                            const char *client_id,
                                            const quest_device_command_t *command,
                                            command_executor_dispatch_t *out_dispatch,
                                            char *error,
                                            size_t error_size)
{
    esp_err_t err = ESP_OK;
    if (!request || !request->device_id[0] || !request->command_id[0] ||
        !command || !command->id[0] || !command->command[0]) {
        return command_executor_fail(error, error_size, ESP_ERR_INVALID_ARG, "device_command_invalid");
    }
    if (out_dispatch) {
        memset(out_dispatch, 0, sizeof(*out_dispatch));
    }
    if (request->require_manual_allowed && !command->manual_allowed) {
        return command_executor_fail(error, error_size, ESP_ERR_INVALID_STATE, "device_command_manual_disabled");
    }
    if (request->require_scenario_allowed && !command->scenario_allowed) {
        return command_executor_fail(error, error_size, ESP_ERR_INVALID_STATE, "device_command_scenario_disabled");
    }
    if (strcmp(request->device_id, QUEST_DEVICE_SYSTEM_AUDIO_ID) == 0 ||
        strncmp(command->command, "audio.", strlen("audio.")) == 0) {
        return command_executor_execute_audio(request, command, error, error_size);
    } else if (strcmp(request->device_id, QUEST_DEVICE_SYSTEM_RELAY_ID) == 0 ||
               strcmp(request->device_id, QUEST_DEVICE_SYSTEM_MOSFET_ID) == 0 ||
               strcmp(request->device_id, QUEST_DEVICE_SYSTEM_IO_ID) == 0) {
        return command_executor_execute_hardware(request, command, error, error_size);
    }
    if (!client_id || !client_id[0]) {
        return command_executor_fail(error, error_size, ESP_ERR_INVALID_ARG, "device_client_id_missing");
    }
    err = command_executor_execute_mqtt(client_id,
                                        command,
                                        request,
                                        out_dispatch,
                                        error,
                                        error_size);
    return err;
}

esp_err_t command_executor_execute(const command_executor_request_t *request,
                                   command_executor_dispatch_t *out_dispatch,
                                   char *error,
                                   size_t error_size)
{
    command_executor_device_snapshot_t device = {0};
    quest_device_command_t command = {0};
    esp_err_t err = ESP_OK;
    if (!request || !request->device_id[0] || !request->command_id[0]) {
        return command_executor_fail(error, error_size, ESP_ERR_INVALID_ARG, "device_command_invalid");
    }
    if (out_dispatch) {
        memset(out_dispatch, 0, sizeof(*out_dispatch));
    }
    err = command_executor_ensure_execute_mutex();
    if (err != ESP_OK) {
        return command_executor_fail(error, error_size, ESP_ERR_NO_MEM, "device_command_no_mem");
    }
    if (xSemaphoreTake(s_execute_mutex, portMAX_DELAY) != pdTRUE) {
        return command_executor_fail(error, error_size, ESP_ERR_TIMEOUT, "device_command_lock_timeout");
    }
    memset(&s_execute_device, 0, sizeof(s_execute_device));
    memset(&s_execute_command, 0, sizeof(s_execute_command));
    err = quest_device_get(request->device_id, &s_execute_device);
    if (err != ESP_OK) {
        err = command_executor_fail(error, error_size, err, "device_not_found");
        goto done;
    }
    if (!s_execute_device.enabled) {
        err = command_executor_fail(error, error_size, ESP_ERR_INVALID_STATE, "device_disabled");
        goto done;
    }
    err = quest_device_get_command(request->device_id, request->command_id, &s_execute_command);
    if (err != ESP_OK) {
        err = command_executor_fail(error, error_size, err, "device_command_not_found");
        goto done;
    }
    quest_str_copy(device.client_id, sizeof(device.client_id), s_execute_device.client_id);
    memcpy(&command, &s_execute_command, sizeof(command));
done:
    xSemaphoreGive(s_execute_mutex);
    if (err != ESP_OK) {
        return err;
    }
    return command_executor_execute_resolved(request,
                                             device.client_id,
                                             &command,
                                             out_dispatch,
                                             error,
                                             error_size);
}

esp_err_t command_executor_execute_device_command(const char *device_id,
                                                  const char *command_id,
                                                  const char *params_json)
{
    command_executor_request_t request = {0};
    if (!device_id || !device_id[0] || !command_id || !command_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(device_id) >= sizeof(request.device_id) ||
        strlen(command_id) >= sizeof(request.command_id)) {
        return ESP_ERR_INVALID_SIZE;
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
    return command_executor_execute(&request, NULL, NULL, 0);
}
