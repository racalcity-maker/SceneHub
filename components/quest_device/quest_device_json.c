#include "quest_device.h"
#include "quest_device_internal.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"

#define QUEST_DEVICE_JSON_VERSION 1

static void qd_json_copy(char *dst, size_t dst_len, const char *src)
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

static esp_err_t qd_json_copy_string(const cJSON *json,
                                     const char *name,
                                     char *dst,
                                     size_t dst_len,
                                     bool required)
{
    const cJSON *item = json ? cJSON_GetObjectItemCaseSensitive(json, name) : NULL;
    if (!item || cJSON_IsNull(item)) {
        if (required) {
            return ESP_ERR_INVALID_ARG;
        }
        dst[0] = '\0';
        return ESP_OK;
    }
    if (!cJSON_IsString(item) || !item->valuestring ||
        (required && !item->valuestring[0]) ||
        strlen(item->valuestring) >= dst_len) {
        return ESP_ERR_INVALID_ARG;
    }
    qd_json_copy(dst, dst_len, item->valuestring);
    return ESP_OK;
}

static bool qd_json_bool(const cJSON *json, const char *name, bool fallback)
{
    const cJSON *item = json ? cJSON_GetObjectItemCaseSensitive(json, name) : NULL;
    if (!item || cJSON_IsNull(item)) {
        return fallback;
    }
    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item);
    }
    if (cJSON_IsNumber(item)) {
        return item->valueint != 0;
    }
    return fallback;
}

static const cJSON *qd_json_policy(const cJSON *json)
{
    const cJSON *policy = json ? cJSON_GetObjectItemCaseSensitive(json, "policy") : NULL;
    return cJSON_IsObject(policy) ? policy : NULL;
}

static void qd_default_capability_from_name(char *dst, size_t dst_len, const char *name)
{
    const char *dot = NULL;
    size_t len = 0;
    if (!dst || dst_len == 0) {
        return;
    }
    dst[0] = '\0';
    if (!name || !name[0]) {
        return;
    }
    dot = strchr(name, '.');
    len = dot ? (size_t)(dot - name) : strlen(name);
    if (len == 0) {
        return;
    }
    if (len >= dst_len) {
        len = dst_len - 1;
    }
    memcpy(dst, name, len);
    dst[len] = '\0';
}

static uint32_t qd_json_u32(const cJSON *json, const char *name, uint32_t fallback)
{
    const cJSON *item = json ? cJSON_GetObjectItemCaseSensitive(json, name) : NULL;
    if (!item || cJSON_IsNull(item)) {
        return fallback;
    }
    if (!cJSON_IsNumber(item) || item->valuedouble < 0.0 || item->valuedouble > UINT32_MAX) {
        return fallback;
    }
    return (uint32_t)item->valuedouble;
}

static esp_err_t qd_json_object_to_string(const cJSON *json,
                                          const char *name,
                                          char *dst,
                                          size_t dst_len)
{
    const cJSON *item = json ? cJSON_GetObjectItemCaseSensitive(json, name) : NULL;
    char *printed = NULL;
    if (!dst || dst_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    dst[0] = '\0';
    if (!item || cJSON_IsNull(item)) {
        return ESP_OK;
    }
    if (!cJSON_IsObject(item)) {
        return ESP_ERR_INVALID_ARG;
    }
    printed = cJSON_PrintUnformatted(item);
    if (!printed) {
        return ESP_ERR_NO_MEM;
    }
    if (strlen(printed) >= dst_len) {
        cJSON_free(printed);
        return ESP_ERR_INVALID_SIZE;
    }
    qd_json_copy(dst, dst_len, printed);
    cJSON_free(printed);
    return ESP_OK;
}

static esp_err_t qd_json_add_raw_object(cJSON *obj, const char *name, const char *json)
{
    cJSON *parsed = NULL;
    if (!obj || !name) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!json || !json[0]) {
        return ESP_OK;
    }
    parsed = cJSON_Parse(json);
    if (!cJSON_IsObject(parsed)) {
        cJSON_Delete(parsed);
        return ESP_ERR_INVALID_ARG;
    }
    if (!cJSON_AddItemToObject(obj, name, parsed)) {
        cJSON_Delete(parsed);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static bool qd_commands_have_duplicate_ids(const quest_device_t *device)
{
    if (!device) {
        return true;
    }
    for (uint8_t i = 0; i < device->command_count; ++i) {
        for (uint8_t j = i + 1; j < device->command_count; ++j) {
            if (strcmp(device->commands[i].id, device->commands[j].id) == 0) {
                return true;
            }
        }
    }
    return false;
}

static bool qd_events_have_duplicate_ids(const quest_device_t *device)
{
    if (!device) {
        return true;
    }
    for (uint8_t i = 0; i < device->event_count; ++i) {
        for (uint8_t j = i + 1; j < device->event_count; ++j) {
            if (strcmp(device->events[i].id, device->events[j].id) == 0) {
                return true;
            }
        }
    }
    return false;
}

static bool qd_json_device_valid(const quest_device_t *device)
{
    if (!device || !device->id[0] || !device->name[0] || !device->client_id[0]) {
        return false;
    }
    if (device->system_device ||
        strcmp(device->id, QUEST_DEVICE_SYSTEM_AUDIO_ID) == 0 ||
        strcmp(device->id, QUEST_DEVICE_SYSTEM_RELAY_ID) == 0 ||
        strcmp(device->id, QUEST_DEVICE_SYSTEM_MOSFET_ID) == 0 ||
        strcmp(device->id, QUEST_DEVICE_SYSTEM_IO_ID) == 0) {
        return false;
    }
    if (device->command_count > QUEST_DEVICE_MAX_COMMANDS ||
        device->event_count > QUEST_DEVICE_MAX_EVENTS) {
        return false;
    }
    if (qd_commands_have_duplicate_ids(device) || qd_events_have_duplicate_ids(device)) {
        return false;
    }
    for (uint8_t i = 0; i < device->command_count; ++i) {
        const quest_device_command_t *cmd = &device->commands[i];
        if (!cmd->id[0] || !cmd->label[0] || !cmd->capability[0] || !cmd->command[0] ||
            cmd->param_count > QUEST_DEVICE_MAX_COMMAND_PARAMS) {
            return false;
        }
        for (uint8_t p = 0; p < cmd->param_count; ++p) {
            if (!cmd->params[p].key[0] || !cmd->params[p].label[0]) {
                return false;
            }
        }
    }
    for (uint8_t i = 0; i < device->event_count; ++i) {
        const quest_device_event_t *event = &device->events[i];
        if (!event->id[0] || !event->label[0] || !event->capability[0] || !event->event[0]) {
            return false;
        }
    }
    return true;
}

static quest_device_t *qd_json_alloc_items(size_t count)
{
    quest_device_t *items = NULL;
    if (count == 0) {
        return NULL;
    }
    items = heap_caps_calloc(count, sizeof(*items), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!items) {
        items = heap_caps_calloc(count, sizeof(*items), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return items;
}

const char *quest_device_command_param_type_to_str(quest_device_command_param_type_t type)
{
    switch (type) {
        case QUEST_DEVICE_COMMAND_PARAM_TEXT:
            return "text";
        case QUEST_DEVICE_COMMAND_PARAM_NUMBER:
            return "number";
        case QUEST_DEVICE_COMMAND_PARAM_CHECKBOX:
            return "checkbox";
        case QUEST_DEVICE_COMMAND_PARAM_AUDIO_FILE_SELECT:
            return "audio_file_select";
        default:
            return "text";
    }
}

esp_err_t quest_device_command_param_type_from_str(const char *s,
                                                   quest_device_command_param_type_t *out)
{
    if (!s || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(s, "text") == 0) {
        *out = QUEST_DEVICE_COMMAND_PARAM_TEXT;
    } else if (strcmp(s, "number") == 0) {
        *out = QUEST_DEVICE_COMMAND_PARAM_NUMBER;
    } else if (strcmp(s, "checkbox") == 0) {
        *out = QUEST_DEVICE_COMMAND_PARAM_CHECKBOX;
    } else if (strcmp(s, "audio_file_select") == 0) {
        *out = QUEST_DEVICE_COMMAND_PARAM_AUDIO_FILE_SELECT;
    } else {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return ESP_OK;
}

static esp_err_t qd_param_to_json(const quest_device_command_param_t *param, cJSON *array)
{
    cJSON *obj = cJSON_CreateObject();
    if (!obj) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(obj, "key", param->key);
    cJSON_AddStringToObject(obj, "label", param->label);
    cJSON_AddStringToObject(obj, "type", quest_device_command_param_type_to_str(param->type));
    cJSON_AddBoolToObject(obj, "optional", param->optional);
    if (!cJSON_AddItemToArray(array, obj)) {
        cJSON_Delete(obj);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t qd_command_to_json(const quest_device_command_t *cmd, cJSON *array)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON *policy = NULL;
    cJSON *params = NULL;
    if (!obj) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(obj, "id", cmd->id);
    cJSON_AddStringToObject(obj, "label", cmd->label);
    cJSON_AddStringToObject(obj, "capability", cmd->capability);
    cJSON_AddStringToObject(obj, "command", cmd->command);
    esp_err_t args_err = qd_json_add_raw_object(obj, "default_args", cmd->default_args_json);
    if (args_err != ESP_OK) {
        cJSON_Delete(obj);
        return args_err;
    }
    policy = cJSON_AddObjectToObject(obj, "policy");
    if (!policy) {
        cJSON_Delete(obj);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddBoolToObject(policy, "manual_allowed", cmd->manual_allowed);
    cJSON_AddBoolToObject(policy, "scenario_allowed", cmd->scenario_allowed);
    cJSON_AddBoolToObject(policy, "requires_confirmation", cmd->requires_confirmation);
    cJSON_AddBoolToObject(policy, "result_required", cmd->result_required);
    cJSON_AddNumberToObject(policy,
                            "timeout_ms",
                            cmd->timeout_ms ? cmd->timeout_ms : QUEST_DEVICE_COMMAND_TIMEOUT_DEFAULT_MS);
    cJSON_AddStringToObject(policy,
                            "danger_level",
                            cmd->danger_level[0] ? cmd->danger_level : "normal");
    params = cJSON_AddArrayToObject(obj, "args_schema");
    if (!params) {
        cJSON_Delete(obj);
        return ESP_ERR_NO_MEM;
    }
    for (uint8_t i = 0; i < cmd->param_count; ++i) {
        esp_err_t err = qd_param_to_json(&cmd->params[i], params);
        if (err != ESP_OK) {
            cJSON_Delete(obj);
            return err;
        }
    }
    if (!cJSON_AddItemToArray(array, obj)) {
        cJSON_Delete(obj);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t qd_event_to_json(const quest_device_event_t *event, cJSON *array)
{
    cJSON *obj = cJSON_CreateObject();
    if (!obj) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(obj, "id", event->id);
    cJSON_AddStringToObject(obj, "label", event->label);
    cJSON_AddStringToObject(obj, "capability", event->capability);
    cJSON_AddStringToObject(obj, "event", event->event);
    esp_err_t match_err = qd_json_add_raw_object(obj, "match", event->match_json);
    if (match_err != ESP_OK) {
        cJSON_Delete(obj);
        return match_err;
    }
    if (!cJSON_AddItemToArray(array, obj)) {
        cJSON_Delete(obj);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t quest_device_to_json(const quest_device_t *device, cJSON *out)
{
    cJSON *commands = NULL;
    cJSON *events = NULL;
    if (!device || !cJSON_IsObject(out)) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON_AddStringToObject(out, "id", device->id);
    cJSON_AddStringToObject(out, "client_id", device->client_id);
    cJSON_AddStringToObject(out, "name", device->name);
    cJSON_AddBoolToObject(out, "enabled", device->enabled);
    cJSON_AddBoolToObject(out, "system_device", device->system_device);
    commands = cJSON_AddArrayToObject(out, "commands");
    events = cJSON_AddArrayToObject(out, "events");
    if (!commands || !events) {
        return ESP_ERR_NO_MEM;
    }
    for (uint8_t i = 0; i < device->command_count; ++i) {
        esp_err_t err = qd_command_to_json(&device->commands[i], commands);
        if (err != ESP_OK) {
            return err;
        }
    }
    for (uint8_t i = 0; i < device->event_count; ++i) {
        esp_err_t err = qd_event_to_json(&device->events[i], events);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

static esp_err_t qd_param_from_json(const cJSON *json, quest_device_command_param_t *out)
{
    const cJSON *type = NULL;
    esp_err_t err = ESP_OK;
    if (!cJSON_IsObject(json) || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    err = qd_json_copy_string(json, "key", out->key, sizeof(out->key), true);
    if (err != ESP_OK) {
        return err;
    }
    err = qd_json_copy_string(json, "label", out->label, sizeof(out->label), false);
    if (err != ESP_OK) {
        return err;
    }
    if (!out->label[0]) {
        qd_json_copy(out->label, sizeof(out->label), out->key);
    }
    type = cJSON_GetObjectItemCaseSensitive(json, "type");
    if (!cJSON_IsString(type) || !type->valuestring) {
        return ESP_ERR_INVALID_ARG;
    }
    err = quest_device_command_param_type_from_str(type->valuestring, &out->type);
    if (err != ESP_OK) {
        return err;
    }
    out->optional = qd_json_bool(json, "optional", false);
    return ESP_OK;
}

static esp_err_t qd_command_from_json(const cJSON *json, quest_device_command_t *out)
{
    const cJSON *params = NULL;
    const cJSON *policy = NULL;
    esp_err_t err = ESP_OK;
    if (!cJSON_IsObject(json) || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    err = qd_json_copy_string(json, "id", out->id, sizeof(out->id), true);
    if (err != ESP_OK) {
        return err;
    }
    err = qd_json_copy_string(json, "label", out->label, sizeof(out->label), false);
    if (err != ESP_OK) {
        return err;
    }
    if (!out->label[0]) {
        qd_json_copy(out->label, sizeof(out->label), out->id);
    }
    if (cJSON_GetObjectItemCaseSensitive(json, "kind") ||
        cJSON_GetObjectItemCaseSensitive(json, "topic") ||
        cJSON_GetObjectItemCaseSensitive(json, "payload") ||
        cJSON_GetObjectItemCaseSensitive(json, "action") ||
        cJSON_GetObjectItemCaseSensitive(json, "button_enabled") ||
        cJSON_GetObjectItemCaseSensitive(json, "dangerous") ||
        cJSON_GetObjectItemCaseSensitive(json, "result_required") ||
        cJSON_GetObjectItemCaseSensitive(json, "timeout_ms") ||
        cJSON_GetObjectItemCaseSensitive(json, "params_schema")) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    err = qd_json_copy_string(json, "command", out->command, sizeof(out->command), true);
    if (err != ESP_OK) {
        return err;
    }
    err = qd_json_copy_string(json, "capability", out->capability, sizeof(out->capability), false);
    if (err != ESP_OK) {
        return err;
    }
    if (!out->capability[0]) {
        qd_default_capability_from_name(out->capability, sizeof(out->capability), out->command);
    }
    err = qd_json_object_to_string(json,
                                   "default_args",
                                   out->default_args_json,
                                   sizeof(out->default_args_json));
    if (err != ESP_OK) {
        return err;
    }
    policy = qd_json_policy(json);
    out->manual_allowed = qd_json_bool(policy, "manual_allowed", true);
    out->scenario_allowed = qd_json_bool(policy, "scenario_allowed", true);
    out->requires_confirmation = qd_json_bool(policy, "requires_confirmation", false);
    out->result_required = qd_json_bool(policy, "result_required", true);
    out->timeout_ms = qd_json_u32(policy, "timeout_ms", QUEST_DEVICE_COMMAND_TIMEOUT_DEFAULT_MS);
    err = qd_json_copy_string(policy, "danger_level", out->danger_level, sizeof(out->danger_level), false);
    if (err != ESP_OK) {
        return err;
    }
    if (!out->danger_level[0]) {
        qd_json_copy(out->danger_level, sizeof(out->danger_level), "normal");
    }
    params = cJSON_GetObjectItemCaseSensitive(json, "args_schema");
    if (params && !cJSON_IsNull(params)) {
        int count = cJSON_GetArraySize(params);
        if (!cJSON_IsArray(params) || count < 0 || count > QUEST_DEVICE_MAX_COMMAND_PARAMS) {
            return ESP_ERR_INVALID_ARG;
        }
        for (int i = 0; i < count; ++i) {
            err = qd_param_from_json(cJSON_GetArrayItem(params, i),
                                     &out->params[out->param_count]);
            if (err != ESP_OK) {
                return err;
            }
            out->param_count++;
        }
    }
    return ESP_OK;
}

static esp_err_t qd_event_from_json(const cJSON *json, quest_device_event_t *out)
{
    esp_err_t err = ESP_OK;
    if (!cJSON_IsObject(json) || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    err = qd_json_copy_string(json, "id", out->id, sizeof(out->id), true);
    if (err != ESP_OK) {
        return err;
    }
    err = qd_json_copy_string(json, "label", out->label, sizeof(out->label), false);
    if (err != ESP_OK) {
        return err;
    }
    if (!out->label[0]) {
        qd_json_copy(out->label, sizeof(out->label), out->id);
    }
    if (cJSON_GetObjectItemCaseSensitive(json, "topic") ||
        cJSON_GetObjectItemCaseSensitive(json, "payload") ||
        cJSON_GetObjectItemCaseSensitive(json, "event_type")) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    err = qd_json_copy_string(json, "event", out->event, sizeof(out->event), true);
    if (err != ESP_OK) {
        return err;
    }
    err = qd_json_copy_string(json, "capability", out->capability, sizeof(out->capability), false);
    if (err != ESP_OK) {
        return err;
    }
    if (!out->capability[0]) {
        qd_default_capability_from_name(out->capability, sizeof(out->capability), out->event);
    }
    err = qd_json_object_to_string(json, "match", out->match_json, sizeof(out->match_json));
    if (err != ESP_OK) {
        return err;
    }
    return ESP_OK;
}

esp_err_t quest_device_from_json(const cJSON *json, quest_device_t *out)
{
    const cJSON *commands = NULL;
    const cJSON *events = NULL;
    esp_err_t err = ESP_OK;
    if (!cJSON_IsObject(json) || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    err = qd_json_copy_string(json, "id", out->id, sizeof(out->id), true);
    if (err != ESP_OK) {
        return err;
    }
    err = qd_json_copy_string(json, "client_id", out->client_id, sizeof(out->client_id), true);
    if (err != ESP_OK) {
        return err;
    }
    err = qd_json_copy_string(json, "name", out->name, sizeof(out->name), false);
    if (err != ESP_OK) {
        return err;
    }
    if (!out->name[0]) {
        qd_json_copy(out->name, sizeof(out->name), out->id);
    }
    out->enabled = qd_json_bool(json, "enabled", true);
    out->system_device = false;

    commands = cJSON_GetObjectItemCaseSensitive(json, "commands");
    if (!cJSON_IsArray(commands)) {
        return ESP_ERR_INVALID_ARG;
    }
    int command_count = cJSON_GetArraySize(commands);
    if (command_count < 0 || command_count > QUEST_DEVICE_MAX_COMMANDS) {
        return ESP_ERR_INVALID_ARG;
    }
    for (int i = 0; i < command_count; ++i) {
        err = qd_command_from_json(cJSON_GetArrayItem(commands, i),
                                   &out->commands[out->command_count]);
        if (err != ESP_OK) {
            return err;
        }
        out->command_count++;
    }

    events = cJSON_GetObjectItemCaseSensitive(json, "events");
    if (!cJSON_IsArray(events)) {
        return ESP_ERR_INVALID_ARG;
    }
    int event_count = cJSON_GetArraySize(events);
    if (event_count < 0 || event_count > QUEST_DEVICE_MAX_EVENTS) {
        return ESP_ERR_INVALID_ARG;
    }
    for (int i = 0; i < event_count; ++i) {
        err = qd_event_from_json(cJSON_GetArrayItem(events, i),
                                 &out->events[out->event_count]);
        if (err != ESP_OK) {
            return err;
        }
        out->event_count++;
    }
    return qd_json_device_valid(out) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

esp_err_t quest_device_export_json(cJSON **out)
{
    cJSON *root = NULL;
    cJSON *array = NULL;
    quest_device_t *items = NULL;
    size_t count = 0;
    esp_err_t err = ESP_OK;
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddNumberToObject(root, "version", QUEST_DEVICE_JSON_VERSION);
    array = cJSON_AddArrayToObject(root, "quest_devices");
    if (!array) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    items = qd_json_alloc_items(QUEST_DEVICE_MAX_DEVICES);
    if (!items) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    err = quest_device_list(items, QUEST_DEVICE_MAX_DEVICES, &count, false);
    if (err != ESP_OK) {
        heap_caps_free(items);
        cJSON_Delete(root);
        return err;
    }
    for (size_t i = 0; i < count; ++i) {
        cJSON *obj = cJSON_CreateObject();
        if (!obj) {
            heap_caps_free(items);
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }
        err = quest_device_to_json(&items[i], obj);
        if (err != ESP_OK || !cJSON_AddItemToArray(array, obj)) {
            cJSON_Delete(obj);
            heap_caps_free(items);
            cJSON_Delete(root);
            return err != ESP_OK ? err : ESP_ERR_NO_MEM;
        }
    }
    heap_caps_free(items);
    *out = root;
    return ESP_OK;
}

esp_err_t quest_device_import_json(const cJSON *root)
{
    const cJSON *version = NULL;
    const cJSON *array = NULL;
    quest_device_t *items = NULL;
    int count = 0;
    esp_err_t err = ESP_OK;
    if (!cJSON_IsObject(root)) {
        return ESP_ERR_INVALID_ARG;
    }
    version = cJSON_GetObjectItemCaseSensitive(root, "version");
    if (!cJSON_IsNumber(version) || version->valueint != QUEST_DEVICE_JSON_VERSION) {
        return ESP_ERR_INVALID_ARG;
    }
    array = cJSON_GetObjectItemCaseSensitive(root, "quest_devices");
    if (!cJSON_IsArray(array)) {
        return ESP_ERR_INVALID_ARG;
    }
    count = cJSON_GetArraySize(array);
    if (count < 0 || count > QUEST_DEVICE_MAX_DEVICES) {
        return ESP_ERR_INVALID_ARG;
    }
    items = qd_json_alloc_items((size_t)count);
    if (count > 0 && !items) {
        return ESP_ERR_NO_MEM;
    }
    for (int i = 0; i < count; ++i) {
        err = quest_device_from_json(cJSON_GetArrayItem(array, i), &items[i]);
        if (err != ESP_OK) {
            heap_caps_free(items);
            return err;
        }
    }
    err = quest_device_replace_all(items, (size_t)count);
    heap_caps_free(items);
    return err;
}
