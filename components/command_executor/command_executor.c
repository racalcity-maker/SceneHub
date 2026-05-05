#include "command_executor_internal.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "quest_common_utils.h"

void *command_executor_alloc(size_t size)
{
    void *ptr = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
        ptr = heap_caps_calloc(1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return ptr;
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
    cJSON *root = NULL;
    const cJSON *item = NULL;
    if (!key || !out || out_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    out[0] = '\0';
    if (!params_json || !params_json[0]) {
        return required ? ESP_ERR_INVALID_ARG : ESP_OK;
    }
    root = cJSON_Parse(params_json);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!item || cJSON_IsNull(item)) {
        cJSON_Delete(root);
        return required ? ESP_ERR_INVALID_ARG : ESP_OK;
    }
    if (!cJSON_IsString(item) || !item->valuestring || strlen(item->valuestring) >= out_size) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    quest_str_copy(out, out_size, item->valuestring);
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t command_executor_params_get_int(const char *params_json,
                                          const char *key,
                                          int *out,
                                          bool required)
{
    cJSON *root = NULL;
    const cJSON *item = NULL;
    if (!key || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!params_json || !params_json[0]) {
        return required ? ESP_ERR_INVALID_ARG : ESP_OK;
    }
    root = cJSON_Parse(params_json);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!item || cJSON_IsNull(item)) {
        cJSON_Delete(root);
        return required ? ESP_ERR_INVALID_ARG : ESP_OK;
    }
    if (cJSON_IsNumber(item)) {
        *out = item->valueint;
        cJSON_Delete(root);
        return ESP_OK;
    }
    if (cJSON_IsString(item) && item->valuestring && item->valuestring[0]) {
        *out = atoi(item->valuestring);
        cJSON_Delete(root);
        return ESP_OK;
    }
    cJSON_Delete(root);
    return ESP_ERR_INVALID_ARG;
}

esp_err_t command_executor_params_get_bool(const char *params_json,
                                           const char *key,
                                           bool *out,
                                           bool required)
{
    cJSON *root = NULL;
    const cJSON *item = NULL;
    if (!key || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!params_json || !params_json[0]) {
        return required ? ESP_ERR_INVALID_ARG : ESP_OK;
    }
    root = cJSON_Parse(params_json);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!item || cJSON_IsNull(item)) {
        cJSON_Delete(root);
        return required ? ESP_ERR_INVALID_ARG : ESP_OK;
    }
    if (cJSON_IsBool(item)) {
        *out = cJSON_IsTrue(item);
        cJSON_Delete(root);
        return ESP_OK;
    }
    if (cJSON_IsNumber(item)) {
        *out = item->valueint != 0;
        cJSON_Delete(root);
        return ESP_OK;
    }
    if (cJSON_IsString(item) && item->valuestring) {
        *out = strcasecmp(item->valuestring, "true") == 0 ||
               strcmp(item->valuestring, "1") == 0 ||
               strcasecmp(item->valuestring, "yes") == 0 ||
               strcasecmp(item->valuestring, "on") == 0;
        cJSON_Delete(root);
        return ESP_OK;
    }
    cJSON_Delete(root);
    return ESP_ERR_INVALID_ARG;
}

bool command_executor_command_name_is(const quest_device_command_t *command, const char *name)
{
    return command && name && strcmp(command->command, name) == 0;
}

esp_err_t command_executor_execute(const command_executor_request_t *request,
                                   command_executor_dispatch_t *out_dispatch,
                                   char *error,
                                   size_t error_size)
{
    quest_device_t *device = NULL;
    quest_device_command_t *command = NULL;
    esp_err_t err = ESP_OK;
    if (!request || !request->device_id[0] || !request->command_id[0]) {
        return command_executor_fail(error, error_size, ESP_ERR_INVALID_ARG, "device_command_invalid");
    }
    if (out_dispatch) {
        memset(out_dispatch, 0, sizeof(*out_dispatch));
    }
    device = (quest_device_t *)command_executor_alloc(sizeof(*device));
    command = (quest_device_command_t *)command_executor_alloc(sizeof(*command));
    if (!device || !command) {
        heap_caps_free(device);
        heap_caps_free(command);
        return command_executor_fail(error, error_size, ESP_ERR_NO_MEM, "device_command_no_mem");
    }
    err = quest_device_get(request->device_id, device);
    if (err != ESP_OK) {
        heap_caps_free(device);
        heap_caps_free(command);
        return command_executor_fail(error, error_size, err, "device_not_found");
    }
    if (!device->enabled) {
        heap_caps_free(device);
        heap_caps_free(command);
        return command_executor_fail(error, error_size, ESP_ERR_INVALID_STATE, "device_disabled");
    }
    err = quest_device_get_command(request->device_id, request->command_id, command);
    if (err != ESP_OK) {
        heap_caps_free(device);
        heap_caps_free(command);
        return command_executor_fail(error, error_size, err, "device_command_not_found");
    }
    if (request->require_manual_allowed && !command->manual_allowed) {
        heap_caps_free(device);
        heap_caps_free(command);
        return command_executor_fail(error, error_size, ESP_ERR_INVALID_STATE, "device_command_manual_disabled");
    }
    if (request->require_scenario_allowed && !command->scenario_allowed) {
        heap_caps_free(device);
        heap_caps_free(command);
        return command_executor_fail(error, error_size, ESP_ERR_INVALID_STATE, "device_command_scenario_disabled");
    }
    if (strcmp(request->device_id, QUEST_DEVICE_SYSTEM_AUDIO_ID) == 0 ||
        strncmp(command->command, "audio.", strlen("audio.")) == 0) {
        err = command_executor_execute_audio(request, command, error, error_size);
    } else {
        err = command_executor_execute_mqtt(device, command, request, out_dispatch, error, error_size);
    }
    heap_caps_free(device);
    heap_caps_free(command);
    return err;
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
    if (params_json && params_json[0]) {
        if (strlen(params_json) >= sizeof(request.params_json)) {
            return ESP_ERR_INVALID_SIZE;
        }
        quest_str_copy(request.params_json, sizeof(request.params_json), params_json);
    }
    return command_executor_execute(&request, NULL, NULL, 0);
}
