#include "scenehub_control_internal.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "command_executor.h"
#include "device_control_ingest.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mqtt_core.h"
#include "scenehub_device_command_resolver.h"

static const char *TAG = "scenehub_control";
static const uint32_t SCENEHUB_CONTROL_DISPATCH_IFACE_TIMEOUT_MS = 3000;
static const uint32_t SCENEHUB_CONTROL_DISPATCH_IFACE_POLL_MS = 100;
#define SCENEHUB_CONTROL_DISPATCH_QUEUE_LEN 2

typedef enum {
    SCENEHUB_CONTROL_DISPATCH_REQ_DESCRIBE_INTERFACE = 1,
    SCENEHUB_CONTROL_DISPATCH_REQ_DEVICE_COMMAND = 2,
    SCENEHUB_CONTROL_DISPATCH_REQ_DEVICE_ADMIN_COMMAND = 3,
} scenehub_control_dispatch_request_type_t;

typedef struct {
    scenehub_control_dispatch_request_type_t type;
    char source[COMMAND_EXECUTOR_SOURCE_MAX_LEN];
    char device_id[QUEST_ID_MAX_LEN];
    char command_id[QUEST_ID_MAX_LEN];
    char params_json[QUEST_PAYLOAD_MAX_LEN];
    char client_id[QUEST_ID_MAX_LEN];
    bool confirmed;
    bool require_manual_allowed;
    bool require_scenario_allowed;
    bool reject_result_required;
    bool return_raw_err;
    char result_required_error[COMMAND_EXECUTOR_COMMAND_MAX_LEN];
    char *out_error;
    size_t out_error_size;
    const char *admin_params_json;
    command_executor_dispatch_t *out_dispatch;
    bool *out_log_warning;
    scenehub_control_device_interface_info_t *out_interface_info;
    scenehub_control_device_command_info_t *out_command_info;
    scenehub_control_device_admin_info_t *out_admin_info;
    scenehub_control_result_t *out_result;
    TaskHandle_t reply_task;
    esp_err_t *out_err;
} scenehub_control_dispatch_request_t;

static StaticQueue_t s_dispatch_queue_storage;
static uint8_t
    s_dispatch_queue_buffer[sizeof(scenehub_control_dispatch_request_t) * SCENEHUB_CONTROL_DISPATCH_QUEUE_LEN];
static QueueHandle_t s_dispatch_queue = NULL;
static StaticTask_t s_dispatch_task_storage;
static StackType_t s_dispatch_task_stack[4096];
static TaskHandle_t s_dispatch_task = NULL;
static portMUX_TYPE s_dispatch_init_lock = portMUX_INITIALIZER_UNLOCKED;

static device_control_ingest_device_t *s_dispatch_ingest_device = NULL;
static char *s_dispatch_describe_json = NULL;
static quest_device_t *s_dispatch_admin_device = NULL;
static quest_device_command_t *s_dispatch_admin_command = NULL;
static char *s_dispatch_admin_payload = NULL;
static scenehub_resolved_device_command_t *s_dispatch_resolved_command = NULL;
static command_executor_request_t *s_dispatch_executor_request = NULL;

static esp_err_t scenehub_control_dispatch_publish_describe_interface(const char *client_id,
                                                                      const char *request_id)
{
    char topic[128] = {0};
    char payload[256] = {0};
    uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000);

    if (!client_id || !client_id[0] || !request_id || !request_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(topic, sizeof(topic), "cp/v1/dev/%s/control/command", client_id);
    snprintf(payload,
             sizeof(payload),
             "{\"request_id\":\"%s\",\"command\":\"describe_interface\",\"args\":{},\"ts_ms\":%llu}",
             request_id,
             (unsigned long long)now_ms);
    return mqtt_core_publish_qos(topic, payload, 1);
}

static cJSON *scenehub_control_dispatch_extract_device_description(const char *data_json)
{
    cJSON *data = NULL;
    cJSON *device_description = NULL;
    cJSON *detached = NULL;

    if (!data_json || !data_json[0]) {
        return NULL;
    }
    data = cJSON_Parse(data_json);
    if (!cJSON_IsObject(data)) {
        cJSON_Delete(data);
        return NULL;
    }
    device_description = cJSON_GetObjectItemCaseSensitive(data, "device_description");
    if (!cJSON_IsObject(device_description)) {
        cJSON_Delete(data);
        return NULL;
    }
    detached = cJSON_Duplicate(device_description, true);
    cJSON_Delete(data);
    return detached;
}

static const char *dispatch_json_string(const cJSON *object, const char *key)
{
    if (!cJSON_IsObject(object) || !key || !key[0]) {
        return "";
    }
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : "";
}

static bool dispatch_json_bool_default(const cJSON *object, const char *key, bool fallback)
{
    if (!cJSON_IsObject(object) || !key || !key[0]) {
        return fallback;
    }
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item);
    }
    return fallback;
}

static uint32_t dispatch_json_u32_default(const cJSON *object, const char *key, uint32_t fallback)
{
    if (!cJSON_IsObject(object) || !key || !key[0]) {
        return fallback;
    }
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsNumber(item) && item->valuedouble >= 0) {
        return (uint32_t)item->valuedouble;
    }
    return fallback;
}

static esp_err_t scenehub_control_dispatch_ensure_scratch(void)
{
    if (s_dispatch_ingest_device && s_dispatch_describe_json) {
        return ESP_OK;
    }

    if (!s_dispatch_ingest_device) {
        s_dispatch_ingest_device = heap_caps_calloc(1,
                                                    sizeof(*s_dispatch_ingest_device),
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_dispatch_ingest_device) {
            s_dispatch_ingest_device = heap_caps_calloc(1,
                                                        sizeof(*s_dispatch_ingest_device),
                                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
    }
    if (!s_dispatch_describe_json) {
        s_dispatch_describe_json = heap_caps_calloc(DEVICE_CONTROL_INGEST_DESCRIBE_INTERFACE_DATA_JSON_MAX_LEN,
                                                    sizeof(char),
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_dispatch_describe_json) {
            s_dispatch_describe_json = heap_caps_calloc(DEVICE_CONTROL_INGEST_DESCRIBE_INTERFACE_DATA_JSON_MAX_LEN,
                                                        sizeof(char),
                                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
    }
    return (s_dispatch_ingest_device && s_dispatch_describe_json) ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t scenehub_control_dispatch_ensure_admin_scratch(void)
{
    if (!s_dispatch_admin_device) {
        s_dispatch_admin_device = heap_caps_calloc(1,
                                                   sizeof(*s_dispatch_admin_device),
                                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_dispatch_admin_device) {
            s_dispatch_admin_device = heap_caps_calloc(1,
                                                       sizeof(*s_dispatch_admin_device),
                                                       MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
    }
    if (!s_dispatch_admin_command) {
        s_dispatch_admin_command = heap_caps_calloc(1,
                                                    sizeof(*s_dispatch_admin_command),
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_dispatch_admin_command) {
            s_dispatch_admin_command = heap_caps_calloc(1,
                                                        sizeof(*s_dispatch_admin_command),
                                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
    }
    if (!s_dispatch_admin_payload) {
        s_dispatch_admin_payload = heap_caps_calloc(DEVICE_CONTROL_INGEST_CACHED_RESULT_DATA_JSON_MAX_LEN,
                                                    sizeof(char),
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_dispatch_admin_payload) {
            s_dispatch_admin_payload = heap_caps_calloc(DEVICE_CONTROL_INGEST_CACHED_RESULT_DATA_JSON_MAX_LEN,
                                                        sizeof(char),
                                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
    }
    return (s_dispatch_admin_device && s_dispatch_admin_command && s_dispatch_admin_payload) ? ESP_OK
                                                                                              : ESP_ERR_NO_MEM;
}

static esp_err_t scenehub_control_dispatch_ensure_command_scratch(void)
{
    if (s_dispatch_resolved_command && s_dispatch_executor_request) {
        return ESP_OK;
    }

    if (!s_dispatch_resolved_command) {
        s_dispatch_resolved_command = heap_caps_calloc(1,
                                                       sizeof(*s_dispatch_resolved_command),
                                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_dispatch_resolved_command) {
            s_dispatch_resolved_command = heap_caps_calloc(1,
                                                           sizeof(*s_dispatch_resolved_command),
                                                           MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
    }
    if (!s_dispatch_executor_request) {
        s_dispatch_executor_request = heap_caps_calloc(1,
                                                       sizeof(*s_dispatch_executor_request),
                                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_dispatch_executor_request) {
            s_dispatch_executor_request = heap_caps_calloc(1,
                                                           sizeof(*s_dispatch_executor_request),
                                                           MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
    }

    return (s_dispatch_resolved_command && s_dispatch_executor_request) ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t scenehub_control_dispatch_execute_describe_interface(
    const scenehub_control_dispatch_request_t *request)
{
    uint64_t start_ms = (uint64_t)(esp_timer_get_time() / 1000);
    uint64_t deadline_ms = start_ms + SCENEHUB_CONTROL_DISPATCH_IFACE_TIMEOUT_MS;
    esp_err_t err = ESP_OK;

    if (!request || !request->client_id[0] || !request->out_interface_info || !request->out_result) {
        return ESP_ERR_INVALID_ARG;
    }
    err = scenehub_control_dispatch_ensure_scratch();
    if (err != ESP_OK) {
        scenehub_control_fill_common_error(request->out_result, err);
        return ESP_OK;
    }

    memset(request->out_interface_info, 0, sizeof(*request->out_interface_info));
    snprintf(request->out_interface_info->request_id,
             sizeof(request->out_interface_info->request_id),
             "iface_%llu",
             (unsigned long long)start_ms);
    ESP_LOGI(TAG,
             "describe_interface request client=%s request_id=%s",
             request->client_id,
             request->out_interface_info->request_id);

    err = scenehub_control_dispatch_publish_describe_interface(request->client_id,
                                                               request->out_interface_info->request_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "describe_interface publish failed client=%s request_id=%s err=%s",
                 request->client_id,
                 request->out_interface_info->request_id,
                 esp_err_to_name(err));
        scenehub_control_set_result(request->out_result,
                                    SCENEHUB_CONTROL_STATUS_FAILED,
                                    err,
                                    false,
                                    "publish_failed",
                                    "Describe interface publish failed");
        return ESP_OK;
    }

    while ((uint64_t)(esp_timer_get_time() / 1000) < deadline_ms) {
        memset(s_dispatch_ingest_device, 0, sizeof(*s_dispatch_ingest_device));
        if (device_control_ingest_get_device(request->client_id, s_dispatch_ingest_device) == ESP_OK &&
            s_dispatch_ingest_device->has_result &&
            strcmp(s_dispatch_ingest_device->result_request_id, request->out_interface_info->request_id) == 0 &&
            strcmp(s_dispatch_ingest_device->result_command, "describe_interface") == 0) {
            if (strcmp(s_dispatch_ingest_device->result_status, "accepted") == 0) {
                vTaskDelay(pdMS_TO_TICKS(SCENEHUB_CONTROL_DISPATCH_IFACE_POLL_MS));
                continue;
            }
            if (strcmp(s_dispatch_ingest_device->result_status, "ok") != 0 &&
                strcmp(s_dispatch_ingest_device->result_status, "done") != 0) {
                ESP_LOGW(TAG,
                         "describe_interface device error client=%s request_id=%s status=%s code=%s",
                         request->client_id,
                         request->out_interface_info->request_id,
                         s_dispatch_ingest_device->result_status,
                         s_dispatch_ingest_device->result_error_code);
                scenehub_control_set_result(
                    request->out_result,
                    SCENEHUB_CONTROL_STATUS_REJECTED,
                    ESP_ERR_INVALID_RESPONSE,
                    false,
                    s_dispatch_ingest_device->result_error_code[0] ? s_dispatch_ingest_device->result_error_code
                                                                   : "device_error",
                    "Device rejected interface description");
                return ESP_OK;
            }
            s_dispatch_describe_json[0] = '\0';
            err = device_control_ingest_take_describe_interface_data(
                request->client_id,
                request->out_interface_info->request_id,
                s_dispatch_describe_json,
                DEVICE_CONTROL_INGEST_DESCRIBE_INTERFACE_DATA_JSON_MAX_LEN);
            if (err == ESP_OK) {
                request->out_interface_info->device_description =
                    scenehub_control_dispatch_extract_device_description(s_dispatch_describe_json);
            }
            if (!request->out_interface_info->device_description) {
                scenehub_control_set_result(request->out_result,
                                            SCENEHUB_CONTROL_STATUS_FAILED,
                                            ESP_ERR_INVALID_RESPONSE,
                                            false,
                                            "missing_device_description",
                                            "Device returned no device_description");
                return ESP_OK;
            }
            ESP_LOGI(TAG,
                     "describe_interface success client=%s request_id=%s",
                     request->client_id,
                     request->out_interface_info->request_id);
            scenehub_control_finish_success_no_state_change(request->out_result);
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(SCENEHUB_CONTROL_DISPATCH_IFACE_POLL_MS));
    }

    ESP_LOGW(TAG,
             "describe_interface timeout client=%s request_id=%s",
             request->client_id,
             request->out_interface_info->request_id);
    scenehub_control_set_result(request->out_result,
                                SCENEHUB_CONTROL_STATUS_TIMEOUT,
                                ESP_ERR_TIMEOUT,
                                false,
                                "timeout",
                                "Describe interface timed out");
    return ESP_OK;
}

static const cJSON *scenehub_control_dispatch_find_admin_template(const cJSON *root,
                                                                  const char *command_id)
{
    const cJSON *templates = cJSON_GetObjectItemCaseSensitive(root, "admin_command_templates");
    const cJSON *item = NULL;

    if (!cJSON_IsArray(templates) || !command_id || !command_id[0]) {
        return NULL;
    }

    cJSON_ArrayForEach(item, templates) {
        if (cJSON_IsObject(item) && strcmp(dispatch_json_string(item, "id"), command_id) == 0) {
            return item;
        }
    }
    return NULL;
}

static esp_err_t scenehub_control_dispatch_fill_admin_command(const quest_device_t *device,
                                                              const char *command_id,
                                                              quest_device_command_t *out_command)
{
    cJSON *root = NULL;
    const cJSON *tmpl = NULL;
    const cJSON *policy = NULL;
    cJSON *default_args = NULL;
    char *printed = NULL;
    esp_err_t err = ESP_OK;

    if (!device || !device->device_description_json[0] || !command_id || !command_id[0] || !out_command) {
        return ESP_ERR_INVALID_ARG;
    }

    root = cJSON_Parse(device->device_description_json);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    tmpl = scenehub_control_dispatch_find_admin_template(root, command_id);
    if (!cJSON_IsObject(tmpl) || !dispatch_json_string(tmpl, "command")[0]) {
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }

    memset(out_command, 0, sizeof(*out_command));
    scenehub_control_copy(out_command->id, sizeof(out_command->id), dispatch_json_string(tmpl, "id"));
    scenehub_control_copy(out_command->label,
                          sizeof(out_command->label),
                          dispatch_json_string(tmpl, "label"));
    scenehub_control_copy(out_command->capability, sizeof(out_command->capability), "admin");
    scenehub_control_copy(out_command->command,
                          sizeof(out_command->command),
                          dispatch_json_string(tmpl, "command"));

    policy = cJSON_GetObjectItemCaseSensitive(tmpl, "policy");
    out_command->manual_allowed = dispatch_json_bool_default(policy, "manual_allowed", false);
    out_command->scenario_allowed = dispatch_json_bool_default(policy, "scenario_allowed", false);
    out_command->requires_confirmation =
        dispatch_json_bool_default(policy, "requires_confirmation", false);
    out_command->result_required = dispatch_json_bool_default(policy, "result_required", true);
    out_command->timeout_ms =
        dispatch_json_u32_default(policy, "timeout_ms", QUEST_DEVICE_COMMAND_TIMEOUT_DEFAULT_MS);
    scenehub_control_copy(out_command->danger_level,
                          sizeof(out_command->danger_level),
                          dispatch_json_string(policy, "danger_level"));
    if (!out_command->danger_level[0]) {
        scenehub_control_copy(out_command->danger_level,
                              sizeof(out_command->danger_level),
                              "normal");
    }

    default_args = cJSON_GetObjectItemCaseSensitive((cJSON *)tmpl, "default_args");
    if (cJSON_IsObject(default_args)) {
        printed = cJSON_PrintUnformatted(default_args);
        if (!printed) {
            err = ESP_ERR_NO_MEM;
        } else if (strlen(printed) >= sizeof(out_command->default_args_json)) {
            err = ESP_ERR_INVALID_SIZE;
        } else {
            scenehub_control_copy(out_command->default_args_json,
                                  sizeof(out_command->default_args_json),
                                  printed);
        }
    }

    if (printed) {
        cJSON_free(printed);
    }
    cJSON_Delete(root);
    return err;
}

static esp_err_t scenehub_control_dispatch_publish_admin_command(const char *client_id,
                                                                 const quest_device_command_t *command,
                                                                 const char *args_json,
                                                                 char *out_request_id,
                                                                 size_t out_request_id_size)
{
    char topic[96] = {0};
    uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000);
    const char *args = (args_json && args_json[0]) ? args_json
                                                   : (command && command->default_args_json[0]
                                                          ? command->default_args_json
                                                          : "{}");
    int written = 0;

    if (!client_id || !client_id[0] || !command || !command->command[0] || !out_request_id ||
        out_request_id_size == 0 || !s_dispatch_admin_payload) {
        return ESP_ERR_INVALID_ARG;
    }
    if (snprintf(topic, sizeof(topic), "cp/v1/dev/%s/control/command", client_id) >= (int)sizeof(topic)) {
        return ESP_ERR_INVALID_SIZE;
    }

    snprintf(out_request_id, out_request_id_size, "adm_%llu", (unsigned long long)now_ms);
    written = snprintf(s_dispatch_admin_payload,
                       QUEST_DEVICE_DESCRIPTION_JSON_MAX_LEN + 512U,
                       "{\"request_id\":\"%s\",\"command\":\"%s\",\"args\":%s,\"ts_ms\":%llu}",
                       out_request_id,
                       command->command,
                       args,
                       (unsigned long long)now_ms);
    if (written < 0 || written >= (int)(QUEST_DEVICE_DESCRIPTION_JSON_MAX_LEN + 512U)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return mqtt_core_publish_qos(topic, s_dispatch_admin_payload, 1);
}

static esp_err_t scenehub_control_dispatch_execute_device_admin_command(
    const scenehub_control_dispatch_request_t *request)
{
    char request_id[SCENEHUB_CONTROL_REQUEST_ID_MAX_LEN] = {0};
    bool device_online = false;
    uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000);
    uint64_t deadline_ms = 0;
    esp_err_t err = ESP_OK;
    bool log_warning = false;

    if (!request || !request->device_id[0] || !request->command_id[0] || !request->out_result) {
        return ESP_ERR_INVALID_ARG;
    }

    err = scenehub_control_dispatch_ensure_scratch();
    if (err != ESP_OK) {
        scenehub_control_fill_common_error(request->out_result, err);
        return ESP_OK;
    }
    err = scenehub_control_dispatch_ensure_admin_scratch();
    if (err != ESP_OK) {
        scenehub_control_fill_common_error(request->out_result, err);
        return ESP_OK;
    }

    memset(s_dispatch_admin_device, 0, sizeof(*s_dispatch_admin_device));
    memset(s_dispatch_admin_command, 0, sizeof(*s_dispatch_admin_command));
    if (request->out_admin_info) {
        memset(request->out_admin_info, 0, sizeof(*request->out_admin_info));
    }

    err = quest_device_get(request->device_id, s_dispatch_admin_device);
    if (err != ESP_OK) {
        scenehub_control_fill_common_error(request->out_result, err);
        return ESP_OK;
    }
    err = scenehub_control_dispatch_fill_admin_command(s_dispatch_admin_device,
                                                       request->command_id,
                                                       s_dispatch_admin_command);
    if (err != ESP_OK) {
        scenehub_control_fill_common_error(request->out_result, err);
        return ESP_OK;
    }

    log_warning = s_dispatch_admin_command->requires_confirmation ||
                  strcmp(s_dispatch_admin_command->danger_level, "normal") != 0;
    if (request->out_log_warning) {
        *request->out_log_warning = log_warning;
    }
    if (request->out_admin_info) {
        scenehub_control_copy(request->out_admin_info->device_name,
                              sizeof(request->out_admin_info->device_name),
                              s_dispatch_admin_device->name);
        scenehub_control_copy(request->out_admin_info->command_label,
                              sizeof(request->out_admin_info->command_label),
                              s_dispatch_admin_command->label);
    }

    if (s_dispatch_admin_command->requires_confirmation && !request->confirmed) {
        scenehub_control_set_result(request->out_result,
                                    SCENEHUB_CONTROL_STATUS_REJECTED,
                                    ESP_ERR_INVALID_STATE,
                                    false,
                                    "confirmation_required",
                                    "Action requires confirmation");
        return ESP_OK;
    }

    err = device_control_ingest_get_presence(s_dispatch_admin_device->client_id,
                                             now_ms,
                                             DEVICE_CONTROL_INGEST_DEFAULT_ONLINE_TIMEOUT_MS,
                                             NULL,
                                             &device_online);
    if (err != ESP_OK || !device_online) {
        scenehub_control_set_result(request->out_result,
                                    SCENEHUB_CONTROL_STATUS_FAILED,
                                    ESP_ERR_INVALID_STATE,
                                    false,
                                    "device_offline",
                                    "Device is offline");
        return ESP_OK;
    }

    err = scenehub_control_dispatch_publish_admin_command(s_dispatch_admin_device->client_id,
                                                          s_dispatch_admin_command,
                                                          request->admin_params_json,
                                                          request_id,
                                                          sizeof(request_id));
    if (err != ESP_OK) {
        scenehub_control_set_result(request->out_result,
                                    SCENEHUB_CONTROL_STATUS_FAILED,
                                    err,
                                    false,
                                    "device_command_publish_failed",
                                    "Admin command publish failed");
        return ESP_OK;
    }

    scenehub_control_set_request_id(request->out_result, request_id);
    if (!s_dispatch_admin_command->result_required) {
        scenehub_control_set_remote_status(request->out_result, "accepted");
        return ESP_OK;
    }

    deadline_ms = (uint64_t)(esp_timer_get_time() / 1000) +
                  (s_dispatch_admin_command->timeout_ms ? s_dispatch_admin_command->timeout_ms
                                                        : QUEST_DEVICE_COMMAND_TIMEOUT_DEFAULT_MS);
    while ((uint64_t)(esp_timer_get_time() / 1000) < deadline_ms) {
        memset(s_dispatch_ingest_device, 0, sizeof(*s_dispatch_ingest_device));
        if (device_control_ingest_get_device(s_dispatch_admin_device->client_id, s_dispatch_ingest_device) ==
                ESP_OK &&
            s_dispatch_ingest_device->has_result &&
            strcmp(s_dispatch_ingest_device->result_request_id, request_id) == 0 &&
            strcmp(s_dispatch_ingest_device->result_command, s_dispatch_admin_command->command) == 0) {
            scenehub_control_set_remote_status(request->out_result,
                                               s_dispatch_ingest_device->result_status);
            if (strcmp(s_dispatch_ingest_device->result_status, "accepted") == 0) {
                vTaskDelay(pdMS_TO_TICKS(SCENEHUB_CONTROL_DISPATCH_IFACE_POLL_MS));
                continue;
            }
            if (strcmp(s_dispatch_ingest_device->result_status, "ok") == 0 ||
                strcmp(s_dispatch_ingest_device->result_status, "done") == 0) {
                if (request->out_admin_info) {
                    const char *result_json = NULL;
                    s_dispatch_admin_payload[0] = '\0';
                    if (s_dispatch_ingest_device->result_data_json[0]) {
                        result_json = s_dispatch_ingest_device->result_data_json;
                    } else if (device_control_ingest_take_result_data(
                                   s_dispatch_admin_device->client_id,
                                   request_id,
                                   s_dispatch_admin_command->command,
                                   s_dispatch_admin_payload,
                                   DEVICE_CONTROL_INGEST_CACHED_RESULT_DATA_JSON_MAX_LEN) == ESP_OK) {
                        result_json = s_dispatch_admin_payload;
                    }
                    if (result_json && result_json[0]) {
                        request->out_admin_info->result_data = cJSON_Parse(result_json);
                    }
                }
                return ESP_OK;
            }
            scenehub_control_set_result(
                request->out_result,
                strcmp(s_dispatch_ingest_device->result_status, "rejected") == 0
                    ? SCENEHUB_CONTROL_STATUS_REJECTED
                    : SCENEHUB_CONTROL_STATUS_FAILED,
                ESP_ERR_INVALID_RESPONSE,
                false,
                s_dispatch_ingest_device->result_error_code[0]
                    ? s_dispatch_ingest_device->result_error_code
                    : "device_error",
                s_dispatch_ingest_device->result_message[0]
                    ? s_dispatch_ingest_device->result_message
                    : "Device rejected admin command");
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(SCENEHUB_CONTROL_DISPATCH_IFACE_POLL_MS));
    }

    scenehub_control_set_result(request->out_result,
                                SCENEHUB_CONTROL_STATUS_TIMEOUT,
                                ESP_ERR_TIMEOUT,
                                false,
                                "timeout",
                                "Admin command timed out");
    return ESP_OK;
}

static esp_err_t scenehub_control_dispatch_execute_device_command(
    const scenehub_control_dispatch_request_t *request)
{
    esp_err_t err = ESP_OK;
    bool log_warning = false;

    if (!request || !request->device_id[0] || !request->command_id[0] ||
        (!request->return_raw_err && !request->out_result)) {
        if (request && request->return_raw_err && request->out_error && request->out_error_size > 0) {
            scenehub_control_copy(request->out_error,
                                  request->out_error_size,
                                  "device_command_invalid");
        }
        return ESP_ERR_INVALID_ARG;
    }
    err = scenehub_control_dispatch_ensure_command_scratch();
    if (err != ESP_OK) {
        if (request->return_raw_err) {
            return err;
        }
        scenehub_control_fill_common_error(request->out_result, err);
        return ESP_OK;
    }

    memset(s_dispatch_resolved_command, 0, sizeof(*s_dispatch_resolved_command));
    memset(s_dispatch_executor_request, 0, sizeof(*s_dispatch_executor_request));
    err = scenehub_device_command_resolve(request->device_id,
                                          request->command_id,
                                          request->params_json,
                                          true,
                                          s_dispatch_resolved_command,
                                          request->return_raw_err ? request->out_error : NULL,
                                          request->return_raw_err ? request->out_error_size : 0);
    if (err == ESP_OK && request->out_command_info) {
        scenehub_control_copy(request->out_command_info->device_name,
                              sizeof(request->out_command_info->device_name),
                              s_dispatch_resolved_command->device_name);
        scenehub_control_copy(request->out_command_info->command_label,
                              sizeof(request->out_command_info->command_label),
                              s_dispatch_resolved_command->command.label);
    }
    if (err == ESP_OK) {
        log_warning = s_dispatch_resolved_command->command.requires_confirmation ||
                      strcmp(s_dispatch_resolved_command->command.danger_level, "normal") != 0;
    }
    if (request->out_log_warning) {
        *request->out_log_warning = log_warning;
    }
    if (err == ESP_OK &&
        request->reject_result_required &&
        s_dispatch_resolved_command->command.result_required) {
        if (request->out_error && request->out_error_size > 0) {
            scenehub_control_copy(request->out_error,
                                  request->out_error_size,
                                  request->result_required_error[0]
                                      ? request->result_required_error
                                      : "device_command_result_required_unsupported");
        }
        err = ESP_ERR_NOT_SUPPORTED;
    }
    if (err == ESP_OK &&
        request->require_manual_allowed &&
        !s_dispatch_resolved_command->command.manual_allowed) {
        err = ESP_ERR_INVALID_STATE;
    }
    if (err == ESP_OK &&
        request->require_manual_allowed &&
        s_dispatch_resolved_command->command.requires_confirmation &&
        !request->confirmed) {
        scenehub_control_set_result(request->out_result,
                                    SCENEHUB_CONTROL_STATUS_REJECTED,
                                    ESP_ERR_INVALID_STATE,
                                    false,
                                    "confirmation_required",
                                    "Action requires confirmation");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        if (request->return_raw_err) {
            return err;
        }
        scenehub_control_fill_common_error(request->out_result, err);
        return ESP_OK;
    }

    scenehub_control_copy(s_dispatch_executor_request->source,
                          sizeof(s_dispatch_executor_request->source),
                          request->source[0] ? request->source : "manual");
    scenehub_control_copy(s_dispatch_executor_request->device_id,
                          sizeof(s_dispatch_executor_request->device_id),
                          request->device_id);
    scenehub_control_copy(s_dispatch_executor_request->command_id,
                          sizeof(s_dispatch_executor_request->command_id),
                          request->command_id);
    s_dispatch_executor_request->require_manual_allowed = request->require_manual_allowed;
    s_dispatch_executor_request->require_scenario_allowed = request->require_scenario_allowed;
    if (request->params_json[0]) {
        scenehub_control_copy(s_dispatch_executor_request->params_json,
                              sizeof(s_dispatch_executor_request->params_json),
                              request->params_json);
    }

    err = command_executor_execute_resolved(s_dispatch_executor_request,
                                            s_dispatch_resolved_command->client_id,
                                            &s_dispatch_resolved_command->command,
                                            request->out_dispatch,
                                            request->return_raw_err ? request->out_error : NULL,
                                            request->return_raw_err ? request->out_error_size : 0);
    if (err != ESP_OK) {
        if (request->return_raw_err) {
            return err;
        }
        scenehub_control_fill_common_error(request->out_result, err);
    }
    return ESP_OK;
}

static void scenehub_control_dispatch_task(void *arg)
{
    (void)arg;
    while (true) {
        scenehub_control_dispatch_request_t request = {0};
        if (xQueueReceive(s_dispatch_queue, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (request.out_err) {
            switch (request.type) {
            case SCENEHUB_CONTROL_DISPATCH_REQ_DESCRIBE_INTERFACE:
                *request.out_err = scenehub_control_dispatch_execute_describe_interface(&request);
                break;
            case SCENEHUB_CONTROL_DISPATCH_REQ_DEVICE_COMMAND:
                *request.out_err = scenehub_control_dispatch_execute_device_command(&request);
                break;
            case SCENEHUB_CONTROL_DISPATCH_REQ_DEVICE_ADMIN_COMMAND:
                *request.out_err = scenehub_control_dispatch_execute_device_admin_command(&request);
                break;
            default:
                *request.out_err = ESP_ERR_NOT_SUPPORTED;
                break;
            }
        }
        if (request.reply_task) {
            xTaskNotifyGive(request.reply_task);
        }
    }
}

static esp_err_t scenehub_control_dispatch_ensure_owner(void)
{
    if (s_dispatch_queue && s_dispatch_task) {
        return ESP_OK;
    }

    portENTER_CRITICAL(&s_dispatch_init_lock);
    if (!s_dispatch_queue) {
        s_dispatch_queue = xQueueCreateStatic(SCENEHUB_CONTROL_DISPATCH_QUEUE_LEN,
                                              sizeof(scenehub_control_dispatch_request_t),
                                              s_dispatch_queue_buffer,
                                              &s_dispatch_queue_storage);
    }
    if (s_dispatch_queue && !s_dispatch_task) {
        s_dispatch_task = xTaskCreateStatic(scenehub_control_dispatch_task,
                                            "sh_ctrl_disp",
                                            sizeof(s_dispatch_task_stack) / sizeof(s_dispatch_task_stack[0]),
                                            NULL,
                                            tskIDLE_PRIORITY + 1,
                                            s_dispatch_task_stack,
                                            &s_dispatch_task_storage);
    }
    portEXIT_CRITICAL(&s_dispatch_init_lock);

    if (!s_dispatch_queue || !s_dispatch_task) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

/*
 * Dispatch owner invariant:
 * - callers must not hold gm_core/session locks while waiting here
 * - callers must not be event_bus dispatch hot-path handlers
 * - callers must not be the sh_ctrl_disp owner task itself
 * - queued operations must remain bounded and must not wait forever internally
 * - the owner serializes scratch buffers and command/device resolution
 * - queue length 2 is intentional backpressure, not a bulk work queue
 */

esp_err_t scenehub_control_dispatch_describe_interface(
    const char *client_id,
    scenehub_control_device_interface_info_t *out_info,
    scenehub_control_result_t *out_result)
{
    scenehub_control_dispatch_request_t request = {0};
    esp_err_t err = scenehub_control_dispatch_ensure_owner();
    if (err != ESP_OK) {
        return err;
    }
    scenehub_control_copy(request.client_id, sizeof(request.client_id), client_id);
    request.type = SCENEHUB_CONTROL_DISPATCH_REQ_DESCRIBE_INTERFACE;
    request.out_interface_info = out_info;
    request.out_result = out_result;
    request.reply_task = xTaskGetCurrentTaskHandle();
    request.out_err = &err;

    if (xQueueSend(s_dispatch_queue, &request, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY) == 0) {
        return ESP_ERR_TIMEOUT;
    }
    return err;
}

esp_err_t scenehub_control_dispatch_device_command(
    const char *source,
    const char *device_id,
    const char *command_id,
    const char *params_json,
    bool confirmed,
    scenehub_control_device_command_info_t *out_info,
    command_executor_dispatch_t *out_dispatch,
    bool *out_log_warning,
    scenehub_control_result_t *out_result)
{
    scenehub_control_dispatch_request_t request = {0};
    esp_err_t err = scenehub_control_dispatch_ensure_owner();
    if (err != ESP_OK) {
        return err;
    }

    scenehub_control_copy(request.source, sizeof(request.source), source);
    scenehub_control_copy(request.device_id, sizeof(request.device_id), device_id);
    scenehub_control_copy(request.command_id, sizeof(request.command_id), command_id);
    if (params_json && params_json[0]) {
        scenehub_control_copy(request.params_json, sizeof(request.params_json), params_json);
    }
    request.confirmed = confirmed;
    request.require_manual_allowed = true;
    request.type = SCENEHUB_CONTROL_DISPATCH_REQ_DEVICE_COMMAND;
    request.out_command_info = out_info;
    request.out_dispatch = out_dispatch;
    request.out_log_warning = out_log_warning;
    request.out_result = out_result;
    request.reply_task = xTaskGetCurrentTaskHandle();
    request.out_err = &err;

    if (xQueueSend(s_dispatch_queue, &request, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY) == 0) {
        return ESP_ERR_TIMEOUT;
    }
    return err;
}

esp_err_t scenehub_control_dispatch_device_admin_command(
    const char *source,
    const char *device_id,
    const char *command_id,
    const char *params_json,
    bool confirmed,
    scenehub_control_device_admin_info_t *out_info,
    bool *out_log_warning,
    scenehub_control_result_t *out_result)
{
    scenehub_control_dispatch_request_t request = {0};
    esp_err_t err = scenehub_control_dispatch_ensure_owner();
    if (err != ESP_OK) {
        return err;
    }

    scenehub_control_copy(request.source, sizeof(request.source), source);
    scenehub_control_copy(request.device_id, sizeof(request.device_id), device_id);
    scenehub_control_copy(request.command_id, sizeof(request.command_id), command_id);
    request.admin_params_json = params_json;
    request.confirmed = confirmed;
    request.type = SCENEHUB_CONTROL_DISPATCH_REQ_DEVICE_ADMIN_COMMAND;
    request.out_admin_info = out_info;
    request.out_log_warning = out_log_warning;
    request.out_result = out_result;
    request.reply_task = xTaskGetCurrentTaskHandle();
    request.out_err = &err;

    if (xQueueSend(s_dispatch_queue, &request, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY) == 0) {
        return ESP_ERR_TIMEOUT;
    }
    return err;
}

esp_err_t scenehub_control_dispatch_scenario_command(
    const char *device_id,
    const char *command_id,
    const char *params_json,
    const char *result_required_error,
    command_executor_dispatch_t *out_dispatch,
    char *error,
    size_t error_size)
{
    scenehub_control_dispatch_request_t request = {0};
    esp_err_t err = scenehub_control_dispatch_ensure_owner();
    if (err != ESP_OK) {
        return err;
    }

    scenehub_control_copy(request.source, sizeof(request.source), "scenario");
    scenehub_control_copy(request.device_id, sizeof(request.device_id), device_id);
    scenehub_control_copy(request.command_id, sizeof(request.command_id), command_id);
    if (params_json && params_json[0]) {
        scenehub_control_copy(request.params_json, sizeof(request.params_json), params_json);
    }
    request.type = SCENEHUB_CONTROL_DISPATCH_REQ_DEVICE_COMMAND;
    request.out_dispatch = out_dispatch;
    request.require_scenario_allowed = true;
    request.reject_result_required = result_required_error && result_required_error[0];
    scenehub_control_copy(request.result_required_error,
                          sizeof(request.result_required_error),
                          result_required_error);
    request.return_raw_err = true;
    request.out_error = error;
    request.out_error_size = error_size;
    request.reply_task = xTaskGetCurrentTaskHandle();
    request.out_err = &err;

    if (xQueueSend(s_dispatch_queue, &request, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY) == 0) {
        return ESP_ERR_TIMEOUT;
    }
    return err;
}
