#include "gm/web_ui_gm_quest_device_json.h"

#include "quest_device.h"

static esp_err_t gm_qd_json_add_raw_object(cJSON *obj, const char *name, const char *json)
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

static esp_err_t gm_qd_param_to_json(const quest_device_command_param_t *param, cJSON *array)
{
    cJSON *obj = cJSON_CreateObject();
    if (!param || !array || !obj) {
        cJSON_Delete(obj);
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

static esp_err_t gm_qd_command_to_json(const quest_device_command_t *cmd, cJSON *array)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON *policy = NULL;
    cJSON *params = NULL;
    esp_err_t err = ESP_OK;
    if (!cmd || !array || !obj) {
        cJSON_Delete(obj);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(obj, "id", cmd->id);
    cJSON_AddStringToObject(obj, "label", cmd->label);
    cJSON_AddStringToObject(obj, "capability", cmd->capability);
    cJSON_AddStringToObject(obj, "command", cmd->command);
    err = gm_qd_json_add_raw_object(obj, "default_args", cmd->default_args_json);
    if (err != ESP_OK) {
        cJSON_Delete(obj);
        return err;
    }
    policy = cJSON_AddObjectToObject(obj, "policy");
    params = cJSON_AddArrayToObject(obj, "args_schema");
    if (!policy || !params) {
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
    for (uint8_t i = 0; i < cmd->param_count; ++i) {
        err = gm_qd_param_to_json(&cmd->params[i], params);
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

static esp_err_t gm_qd_event_to_json(const quest_device_event_t *event, cJSON *array)
{
    cJSON *obj = cJSON_CreateObject();
    esp_err_t err = ESP_OK;
    if (!event || !array || !obj) {
        cJSON_Delete(obj);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(obj, "id", event->id);
    cJSON_AddStringToObject(obj, "label", event->label);
    cJSON_AddStringToObject(obj, "capability", event->capability);
    cJSON_AddStringToObject(obj, "event", event->event);
    err = gm_qd_json_add_raw_object(obj, "match", event->match_json);
    if (err != ESP_OK) {
        cJSON_Delete(obj);
        return err;
    }
    if (!cJSON_AddItemToArray(array, obj)) {
        cJSON_Delete(obj);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static esp_err_t gm_qd_add_manifest_summary(const orch_quest_device_catalog_entry_t *device,
                                            cJSON *out)
{
    cJSON *summary = NULL;
    if (!device->compact_manifest) {
        return ESP_OK;
    }
    summary = cJSON_CreateObject();
    if (!summary) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddBoolToObject(summary, "compact", true);
    cJSON_AddStringToObject(summary, "node_kind", device->node_kind);
    cJSON_AddStringToObject(summary, "capability_contract", device->capability_contract);
    cJSON_AddNumberToObject(summary, "resource_count", device->resource_count);
    cJSON_AddNumberToObject(summary, "command_template_count", device->command_template_count);
    cJSON_AddNumberToObject(summary, "event_template_count", device->event_template_count);
    cJSON_AddItemToObject(out, "manifest_summary", summary);
    return ESP_OK;
}

esp_err_t gm_quest_device_catalog_entry_to_json(const orch_quest_device_catalog_entry_t *device,
                                                cJSON *out,
                                                bool include_manifest_json)
{
    cJSON *commands = NULL;
    cJSON *events = NULL;
    esp_err_t err = ESP_OK;
    if (!device || !cJSON_IsObject(out)) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON_AddStringToObject(out, "id", device->id);
    cJSON_AddStringToObject(out, "client_id", device->client_id);
    cJSON_AddStringToObject(out, "name", device->name);
    cJSON_AddBoolToObject(out, "enabled", device->enabled);
    cJSON_AddBoolToObject(out, "system_device", device->system_device);
    if (include_manifest_json) {
        err = gm_qd_json_add_raw_object(out, "device_description", device->device_description_json);
        if (err != ESP_OK) {
            return err;
        }
    }
    err = gm_qd_add_manifest_summary(device, out);
    if (err != ESP_OK) {
        return err;
    }
    commands = cJSON_AddArrayToObject(out, "commands");
    events = cJSON_AddArrayToObject(out, "events");
    if (!commands || !events) {
        return ESP_ERR_NO_MEM;
    }
    for (uint8_t i = 0; i < device->command_count; ++i) {
        err = gm_qd_command_to_json(&device->commands[i], commands);
        if (err != ESP_OK) {
            return err;
        }
    }
    for (uint8_t i = 0; i < device->event_count; ++i) {
        err = gm_qd_event_to_json(&device->events[i], events);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
