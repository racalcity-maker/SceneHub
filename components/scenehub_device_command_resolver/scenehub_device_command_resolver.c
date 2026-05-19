#include "scenehub_device_command_resolver.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "quest_common_utils.h"

static SemaphoreHandle_t s_resolver_mutex = NULL;
static StaticSemaphore_t s_resolver_mutex_storage;
static portMUX_TYPE s_resolver_mutex_init_lock = portMUX_INITIALIZER_UNLOCKED;
EXT_RAM_BSS_ATTR static quest_device_t s_resolver_device;

static esp_err_t resolver_lock(void)
{
    if (!s_resolver_mutex) {
        portENTER_CRITICAL(&s_resolver_mutex_init_lock);
        if (!s_resolver_mutex) {
            s_resolver_mutex = xSemaphoreCreateMutexStatic(&s_resolver_mutex_storage);
        }
        portEXIT_CRITICAL(&s_resolver_mutex_init_lock);
        if (!s_resolver_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }
    return xSemaphoreTake(s_resolver_mutex, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void resolver_unlock(void)
{
    if (s_resolver_mutex) {
        xSemaphoreGive(s_resolver_mutex);
    }
}

static void resolver_set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0 && message) {
        quest_str_copy(error, error_size, message);
    }
}

static const char *json_string(const cJSON *object, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : "";
}

static bool compact_manifest_root_valid(const cJSON *root)
{
    const cJSON *manifest_version = cJSON_GetObjectItemCaseSensitive(root, "manifest_version");
    return cJSON_IsObject(root) &&
           cJSON_IsNumber(manifest_version) &&
           manifest_version->valueint == 2 &&
           strcmp(json_string(root, "format"), "compact_resources") == 0 &&
           json_string(root, "node_kind")[0] &&
           strcmp(json_string(root, "capability_contract"), "scenehub.node.compact.v1") == 0;
}

static const cJSON *find_template_by_id(const cJSON *root, const char *command_id)
{
    const cJSON *templates = cJSON_GetObjectItemCaseSensitive(root, "command_templates");
    const cJSON *item = NULL;
    if (!cJSON_IsArray(templates) || !command_id || !command_id[0]) {
        return NULL;
    }
    cJSON_ArrayForEach(item, templates) {
        if (cJSON_IsObject(item) && strcmp(json_string(item, "id"), command_id) == 0) {
            return item;
        }
    }
    return NULL;
}

static const cJSON *schema_for_template(const cJSON *root, const cJSON *template)
{
    const char *schema_ref = json_string(template, "args_schema_ref");
    const cJSON *schemas = cJSON_GetObjectItemCaseSensitive(root, "schemas");
    if (!schema_ref[0] || !cJSON_IsObject(schemas)) {
        return NULL;
    }
    return cJSON_GetObjectItemCaseSensitive(schemas, schema_ref);
}

static bool json_value_present(const cJSON *item)
{
    return item && !cJSON_IsNull(item);
}

static bool json_number_like(const cJSON *item, int *out)
{
    char *end = NULL;
    long parsed = 0;
    if (cJSON_IsNumber(item)) {
        if (out) {
            *out = item->valueint;
        }
        return true;
    }
    if (!cJSON_IsString(item) || !item->valuestring || !item->valuestring[0]) {
        return false;
    }
    parsed = strtol(item->valuestring, &end, 10);
    if (!end || *end != '\0') {
        return false;
    }
    if (out) {
        *out = (int)parsed;
    }
    return true;
}

static bool json_bool_like(const cJSON *item)
{
    return cJSON_IsBool(item) || cJSON_IsNumber(item) || cJSON_IsString(item);
}

static esp_err_t json_object_set_dup(cJSON *object,
                                     const char *key,
                                     const cJSON *value)
{
    cJSON *dup = NULL;

    if (!cJSON_IsObject(object) || !key || !key[0] || !value) {
        return ESP_ERR_INVALID_ARG;
    }

    dup = cJSON_Duplicate(value, true);
    if (!dup) {
        return ESP_ERR_NO_MEM;
    }
    if (cJSON_GetObjectItemCaseSensitive(object, key)) {
        if (!cJSON_ReplaceItemInObjectCaseSensitive(object, key, dup)) {
            cJSON_Delete(dup);
            return ESP_FAIL;
        }
        return ESP_OK;
    }
    if (!cJSON_AddItemToObject(object, key, dup)) {
        cJSON_Delete(dup);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t merge_effective_params(const cJSON *default_args,
                                        const char *params_json,
                                        cJSON **out)
{
    cJSON *effective = NULL;
    cJSON *overrides = NULL;
    const cJSON *item = NULL;

    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;

    effective = cJSON_CreateObject();
    if (!effective) {
        return ESP_ERR_NO_MEM;
    }
    if (cJSON_IsObject(default_args)) {
        cJSON_ArrayForEach(item, default_args) {
            esp_err_t set_err = json_object_set_dup(effective, item->string, item);
            if (set_err != ESP_OK) {
                cJSON_Delete(effective);
                return set_err;
            }
        }
    }
    if (params_json && params_json[0]) {
        overrides = cJSON_Parse(params_json);
        if (!cJSON_IsObject(overrides)) {
            cJSON_Delete(overrides);
            cJSON_Delete(effective);
            return ESP_ERR_INVALID_ARG;
        }
        cJSON_ArrayForEach(item, overrides) {
            esp_err_t set_err = json_object_set_dup(effective, item->string, item);
            if (set_err != ESP_OK) {
                cJSON_Delete(overrides);
                cJSON_Delete(effective);
                return set_err;
            }
        }
    }
    cJSON_Delete(overrides);
    *out = effective;
    return ESP_OK;
}

static bool select_option_exists(const cJSON *schema_item, const cJSON *value)
{
    const cJSON *options = cJSON_GetObjectItemCaseSensitive(schema_item, "options");
    const cJSON *option = NULL;
    const char *selected = NULL;
    if (!cJSON_IsString(value) || !value->valuestring || !cJSON_IsArray(options)) {
        return false;
    }
    selected = value->valuestring;
    cJSON_ArrayForEach(option, options) {
        if (cJSON_IsString(option) && option->valuestring &&
            strcmp(option->valuestring, selected) == 0) {
            return true;
        }
    }
    return false;
}

static bool resource_channel_exists(const cJSON *root,
                                    const char *target,
                                    const cJSON *value)
{
    const cJSON *resources = cJSON_GetObjectItemCaseSensitive(root, "resources");
    const cJSON *array = cJSON_GetObjectItemCaseSensitive(resources, target);
    const cJSON *item = NULL;
    const char *field = (target && strcmp(target, "led_strips") == 0) ? "strip" : "channel";
    int selected = 0;
    if (!cJSON_IsArray(array) || !json_number_like(value, &selected)) {
        return false;
    }
    cJSON_ArrayForEach(item, array) {
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, field);
        if (cJSON_IsNumber(id) && id->valueint == selected) {
            return true;
        }
    }
    return false;
}

static quest_device_command_param_type_t compact_param_type(const char *type)
{
    if (type && strcmp(type, "number") == 0) {
        return QUEST_DEVICE_COMMAND_PARAM_NUMBER;
    }
    if (type && strcmp(type, "resource_channel") == 0) {
        return QUEST_DEVICE_COMMAND_PARAM_NUMBER;
    }
    if (type && strcmp(type, "checkbox") == 0) {
        return QUEST_DEVICE_COMMAND_PARAM_CHECKBOX;
    }
    if (type && strcmp(type, "audio_file_select") == 0) {
        return QUEST_DEVICE_COMMAND_PARAM_AUDIO_FILE_SELECT;
    }
    return QUEST_DEVICE_COMMAND_PARAM_TEXT;
}

static esp_err_t validate_compact_params(const cJSON *root,
                                         const cJSON *template,
                                         const cJSON *schema,
                                         const char *params_json,
                                         char *error,
                                         size_t error_size)
{
    cJSON *params = NULL;
    const cJSON *schema_item = NULL;
    const char *target = json_string(template, "target");
    const cJSON *default_args = cJSON_GetObjectItemCaseSensitive(template, "default_args");

    if (!cJSON_IsArray(schema)) {
        resolver_set_error(error, error_size, "device_command_schema_not_found");
        return ESP_ERR_INVALID_ARG;
    }
    if (merge_effective_params(default_args, params_json, &params) != ESP_OK) {
        cJSON_Delete(params);
        resolver_set_error(error, error_size, "device_command_params_invalid");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON_ArrayForEach(schema_item, schema) {
        const char *key = json_string(schema_item, "key");
        const char *type = json_string(schema_item, "type");
        const cJSON *optional = cJSON_GetObjectItemCaseSensitive(schema_item, "optional");
        bool is_optional = cJSON_IsTrue(optional);
        const cJSON *value = params ? cJSON_GetObjectItemCaseSensitive(params, key) : NULL;

        if (!key[0]) {
            cJSON_Delete(params);
            resolver_set_error(error, error_size, "device_command_schema_invalid");
            return ESP_ERR_INVALID_ARG;
        }
        if (!json_value_present(value)) {
            if (is_optional) {
                continue;
            }
            cJSON_Delete(params);
            resolver_set_error(error, error_size, "device_command_param_required");
            return ESP_ERR_INVALID_ARG;
        }
        if (strcmp(type, "resource_channel") == 0) {
            if (!resource_channel_exists(root, target, value)) {
                cJSON_Delete(params);
                resolver_set_error(error, error_size, "device_command_resource_invalid");
                return ESP_ERR_INVALID_ARG;
            }
        } else if (strcmp(type, "select") == 0) {
            if (!select_option_exists(schema_item, value)) {
                cJSON_Delete(params);
                resolver_set_error(error, error_size, "device_command_option_invalid");
                return ESP_ERR_INVALID_ARG;
            }
        } else if (strcmp(type, "number") == 0) {
            if (!json_number_like(value, NULL)) {
                cJSON_Delete(params);
                resolver_set_error(error, error_size, "device_command_number_invalid");
                return ESP_ERR_INVALID_ARG;
            }
        } else if (strcmp(type, "checkbox") == 0) {
            if (!json_bool_like(value)) {
                cJSON_Delete(params);
                resolver_set_error(error, error_size, "device_command_bool_invalid");
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    cJSON_Delete(params);
    return ESP_OK;
}

esp_err_t scenehub_device_command_validate_params(const quest_device_command_t *command,
                                                  const char *params_json,
                                                  char *error,
                                                  size_t error_size)
{
    cJSON *effective = NULL;
    cJSON *default_args = NULL;
    esp_err_t err = ESP_OK;

    if (!command) {
        resolver_set_error(error, error_size, "device_command_invalid");
        return ESP_ERR_INVALID_ARG;
    }
    if (command->default_args_json[0]) {
        default_args = cJSON_Parse(command->default_args_json);
        if (!cJSON_IsObject(default_args)) {
            cJSON_Delete(default_args);
            resolver_set_error(error, error_size, "device_command_default_args_invalid");
            return ESP_ERR_INVALID_ARG;
        }
    }
    err = merge_effective_params(default_args, params_json, &effective);
    cJSON_Delete(default_args);
    if (err != ESP_OK) {
        resolver_set_error(error, error_size, "device_command_params_invalid");
        return err;
    }

    for (uint8_t i = 0; i < command->param_count && i < QUEST_DEVICE_MAX_COMMAND_PARAMS; ++i) {
        const quest_device_command_param_t *param = &command->params[i];
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(effective, param->key);
        if (!param->key[0]) {
            cJSON_Delete(effective);
            resolver_set_error(error, error_size, "device_command_schema_invalid");
            return ESP_ERR_INVALID_ARG;
        }
        if (!json_value_present(value)) {
            if (param->optional) {
                continue;
            }
            cJSON_Delete(effective);
            resolver_set_error(error, error_size, "device_command_param_required");
            return ESP_ERR_INVALID_ARG;
        }
        switch (param->type) {
        case QUEST_DEVICE_COMMAND_PARAM_NUMBER:
            if (!json_number_like(value, NULL)) {
                cJSON_Delete(effective);
                resolver_set_error(error, error_size, "device_command_number_invalid");
                return ESP_ERR_INVALID_ARG;
            }
            break;
        case QUEST_DEVICE_COMMAND_PARAM_CHECKBOX:
            if (!json_bool_like(value)) {
                cJSON_Delete(effective);
                resolver_set_error(error, error_size, "device_command_bool_invalid");
                return ESP_ERR_INVALID_ARG;
            }
            break;
        case QUEST_DEVICE_COMMAND_PARAM_AUDIO_FILE_SELECT:
            if (!cJSON_IsString(value) || !value->valuestring || !value->valuestring[0]) {
                cJSON_Delete(effective);
                resolver_set_error(error, error_size, "device_command_audio_file_missing");
                return ESP_ERR_INVALID_ARG;
            }
            break;
        case QUEST_DEVICE_COMMAND_PARAM_TEXT:
        default:
            if (!cJSON_IsString(value) || !value->valuestring || !value->valuestring[0]) {
                cJSON_Delete(effective);
                resolver_set_error(error, error_size, "device_command_text_invalid");
                return ESP_ERR_INVALID_ARG;
            }
            break;
        }
    }

    cJSON_Delete(effective);
    return ESP_OK;
}

static void command_capability_from_template(quest_device_command_t *command, const cJSON *template)
{
    const char *capability = json_string(template, "capability");
    const char *target = json_string(template, "target");
    const char *name = command->command;
    char tmp[QUEST_DEVICE_CAPABILITY_MAX_LEN] = {0};
    char *dot = NULL;

    if (capability[0]) {
        quest_str_copy(command->capability, sizeof(command->capability), capability);
        return;
    }
    if (target[0]) {
        quest_str_copy(command->capability, sizeof(command->capability), target);
        return;
    }
    quest_str_copy(tmp, sizeof(tmp), name);
    dot = strchr(tmp, '.');
    if (dot) {
        *dot = '\0';
    }
    quest_str_copy(command->capability, sizeof(command->capability), tmp[0] ? tmp : "node");
}

static esp_err_t fill_compact_command(quest_device_command_t *command,
                                      const cJSON *template,
                                      const cJSON *schema)
{
    const cJSON *policy = cJSON_GetObjectItemCaseSensitive(template, "policy");
    const cJSON *default_args = cJSON_GetObjectItemCaseSensitive(template, "default_args");
    const cJSON *schema_item = NULL;
    char *printed = NULL;

    memset(command, 0, sizeof(*command));
    quest_str_copy(command->id, sizeof(command->id), json_string(template, "id"));
    quest_str_copy(command->label, sizeof(command->label), json_string(template, "label"));
    quest_str_copy(command->command, sizeof(command->command), json_string(template, "command"));
    if (!command->label[0]) {
        quest_str_copy(command->label, sizeof(command->label), command->id);
    }
    command_capability_from_template(command, template);

    command->manual_allowed = true;
    command->scenario_allowed = true;
    command->timeout_ms = QUEST_DEVICE_COMMAND_TIMEOUT_DEFAULT_MS;
    quest_str_copy(command->danger_level, sizeof(command->danger_level), "normal");
    if (cJSON_IsObject(policy)) {
        const cJSON *manual = cJSON_GetObjectItemCaseSensitive(policy, "manual_allowed");
        const cJSON *scenario = cJSON_GetObjectItemCaseSensitive(policy, "scenario_allowed");
        const cJSON *confirm = cJSON_GetObjectItemCaseSensitive(policy, "requires_confirmation");
        const cJSON *result = cJSON_GetObjectItemCaseSensitive(policy, "result_required");
        const cJSON *timeout = cJSON_GetObjectItemCaseSensitive(policy, "timeout_ms");
        const char *danger = json_string(policy, "danger_level");
        if (cJSON_IsBool(manual)) {
            command->manual_allowed = cJSON_IsTrue(manual);
        }
        if (cJSON_IsBool(scenario)) {
            command->scenario_allowed = cJSON_IsTrue(scenario);
        }
        if (cJSON_IsBool(confirm)) {
            command->requires_confirmation = cJSON_IsTrue(confirm);
        }
        if (cJSON_IsBool(result)) {
            command->result_required = cJSON_IsTrue(result);
        }
        if (cJSON_IsNumber(timeout) && timeout->valueint > 0) {
            command->timeout_ms = (uint32_t)timeout->valueint;
        }
        if (danger[0]) {
            quest_str_copy(command->danger_level, sizeof(command->danger_level), danger);
        }
    }
    if (cJSON_IsObject(default_args)) {
        printed = cJSON_PrintUnformatted(default_args);
        if (printed) {
            if (strlen(printed) >= sizeof(command->default_args_json)) {
                cJSON_free(printed);
                return ESP_ERR_INVALID_SIZE;
            }
            quest_str_copy(command->default_args_json, sizeof(command->default_args_json), printed);
            cJSON_free(printed);
        }
    }
    cJSON_ArrayForEach(schema_item, schema) {
        quest_device_command_param_t *param = NULL;
        if (command->param_count >= QUEST_DEVICE_MAX_COMMAND_PARAMS) {
            break;
        }
        param = &command->params[command->param_count++];
        quest_str_copy(param->key, sizeof(param->key), json_string(schema_item, "key"));
        quest_str_copy(param->label, sizeof(param->label), json_string(schema_item, "label"));
        if (!param->label[0]) {
            quest_str_copy(param->label, sizeof(param->label), param->key);
        }
        param->type = compact_param_type(json_string(schema_item, "type"));
        param->optional = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(schema_item, "optional"));
    }
    return ESP_OK;
}

static esp_err_t resolve_compact_command(const quest_device_t *device,
                                         const char *command_id,
                                         const char *params_json,
                                         scenehub_resolved_device_command_t *out,
                                         char *error,
                                         size_t error_size)
{
    cJSON *root = cJSON_Parse(device->device_description_json);
    const cJSON *template = NULL;
    const cJSON *schema = NULL;
    esp_err_t err = ESP_OK;

    if (!root || !compact_manifest_root_valid(root)) {
        cJSON_Delete(root);
        resolver_set_error(error, error_size, "device_description_invalid");
        return ESP_ERR_INVALID_ARG;
    }
    template = find_template_by_id(root, command_id);
    if (!template) {
        cJSON_Delete(root);
        resolver_set_error(error, error_size, "device_command_not_found");
        return ESP_ERR_NOT_FOUND;
    }
    schema = schema_for_template(root, template);
    err = validate_compact_params(root, template, schema, params_json, error, error_size);
    if (err == ESP_OK) {
        err = fill_compact_command(&out->command, template, schema);
    }
    cJSON_Delete(root);
    if (err != ESP_OK && error && error_size > 0 && !error[0]) {
        resolver_set_error(error, error_size, "device_command_invalid");
    }
    return err;
}

esp_err_t scenehub_device_command_resolve(const char *device_id,
                                          const char *command_id,
                                          const char *params_json,
                                          bool require_enabled,
                                          scenehub_resolved_device_command_t *out,
                                          char *error,
                                          size_t error_size)
{
    esp_err_t err = ESP_OK;
    const quest_device_command_t *flat_command = NULL;

    if (!device_id || !device_id[0] || !command_id || !command_id[0] || !out) {
        resolver_set_error(error, error_size, "device_command_invalid");
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    err = resolver_lock();
    if (err != ESP_OK) {
        resolver_set_error(error, error_size, "device_command_lock_timeout");
        return err;
    }
    memset(&s_resolver_device, 0, sizeof(s_resolver_device));
    err = quest_device_get(device_id, &s_resolver_device);
    if (err != ESP_OK) {
        resolver_unlock();
        resolver_set_error(error, error_size, "device_not_found");
        return err;
    }
    if (require_enabled && !s_resolver_device.enabled) {
        resolver_unlock();
        resolver_set_error(error, error_size, "device_disabled");
        return ESP_ERR_INVALID_STATE;
    }
    quest_str_copy(out->device_id, sizeof(out->device_id), s_resolver_device.id);
    quest_str_copy(out->client_id, sizeof(out->client_id), s_resolver_device.client_id);
    quest_str_copy(out->device_name, sizeof(out->device_name), s_resolver_device.name);

    out->compact_manifest = s_resolver_device.device_description_json[0] != '\0';
    if (out->compact_manifest) {
        err = resolve_compact_command(&s_resolver_device, command_id, params_json, out, error, error_size);
        resolver_unlock();
        return err;
    }
    for (uint8_t i = 0; i < s_resolver_device.command_count; ++i) {
        if (strcmp(s_resolver_device.commands[i].id, command_id) == 0) {
            flat_command = &s_resolver_device.commands[i];
            break;
        }
    }
    if (!flat_command) {
        resolver_unlock();
        resolver_set_error(error, error_size, "device_command_not_found");
        return ESP_ERR_NOT_FOUND;
    }
    memcpy(&out->command, flat_command, sizeof(out->command));
    err = scenehub_device_command_validate_params(&out->command, params_json, error, error_size);
    resolver_unlock();
    return err;
}
