#include "command_executor_internal.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_timer.h"
#include "mqtt_core.h"
#include "quest_common_utils.h"

static esp_err_t json_merge_object(cJSON *dst, const char *json)
{
    cJSON *src = NULL;
    cJSON *item = NULL;
    if (!dst || !cJSON_IsObject(dst)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!json || !json[0]) {
        return ESP_OK;
    }
    src = cJSON_Parse(json);
    if (!cJSON_IsObject(src)) {
        cJSON_Delete(src);
        return ESP_ERR_INVALID_ARG;
    }
    cJSON_ArrayForEach(item, src) {
        cJSON *dup = cJSON_Duplicate(item, true);
        cJSON *existing = NULL;
        if (!dup) {
            cJSON_Delete(src);
            return ESP_ERR_NO_MEM;
        }
        existing = cJSON_GetObjectItemCaseSensitive(dst, item->string);
        if (existing) {
            if (!cJSON_ReplaceItemInObjectCaseSensitive(dst, item->string, dup)) {
                cJSON_Delete(dup);
                cJSON_Delete(src);
                return ESP_FAIL;
            }
        } else if (!cJSON_AddItemToObject(dst, item->string, dup)) {
            cJSON_Delete(dup);
            cJSON_Delete(src);
            return ESP_ERR_NO_MEM;
        }
    }
    cJSON_Delete(src);
    return ESP_OK;
}

esp_err_t command_executor_execute_mqtt(const quest_device_t *device,
                                        const quest_device_command_t *command,
                                        const command_executor_request_t *request,
                                        command_executor_dispatch_t *out_dispatch,
                                        char *error,
                                        size_t error_size)
{
    cJSON *root = NULL;
    cJSON *args = NULL;
    char *payload = NULL;
    char topic[96] = {0};
    char request_id[COMMAND_EXECUTOR_REQUEST_ID_MAX_LEN] = {0};
    esp_err_t err = ESP_OK;
    int64_t now_ms = esp_timer_get_time() / 1000;

    if (!device || !command || !request || !device->client_id[0] || !command->command[0]) {
        return command_executor_fail(error, error_size, ESP_ERR_INVALID_ARG, "device_command_invalid");
    }
    if (snprintf(topic, sizeof(topic), "cp/v1/dev/%s/control/command", device->client_id) >= (int)sizeof(topic)) {
        return command_executor_fail(error,
                                     error_size,
                                     ESP_ERR_INVALID_SIZE,
                                     "device_command_topic_too_long");
    }
    snprintf(request_id, sizeof(request_id), "req-%08llx", (unsigned long long)now_ms);

    root = cJSON_CreateObject();
    args = cJSON_CreateObject();
    if (!root || !args) {
        cJSON_Delete(root);
        cJSON_Delete(args);
        return command_executor_fail(error, error_size, ESP_ERR_NO_MEM, "device_command_no_mem");
    }
    err = json_merge_object(args, command->default_args_json);
    if (err == ESP_OK) {
        err = json_merge_object(args, request->params_json);
    }
    if (err != ESP_OK) {
        cJSON_Delete(root);
        cJSON_Delete(args);
        return command_executor_fail(error, error_size, err, "device_command_args_invalid");
    }
    cJSON_AddStringToObject(root, "request_id", request_id);
    cJSON_AddStringToObject(root, "command", command->command);
    cJSON_AddItemToObject(root, "args", args);
    args = NULL;
    cJSON_AddNumberToObject(root, "ts_ms", (double)now_ms);
    payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        return command_executor_fail(error, error_size, ESP_ERR_NO_MEM, "device_command_no_mem");
    }
    err = mqtt_core_publish(topic, payload);
    cJSON_free(payload);
    if (err != ESP_OK) {
        return command_executor_fail(error, error_size, err, "device_command_publish_failed");
    }
    if (command->result_required) {
        err = command_executor_track_pending(request_id,
                                             device->client_id,
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
        quest_str_copy(out_dispatch->source_id, sizeof(out_dispatch->source_id), device->client_id);
        quest_str_copy(out_dispatch->command, sizeof(out_dispatch->command), command->command);
    }
    return ESP_OK;
}
