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
    command_executor_dispatch_t *out_dispatch;
    bool *out_log_warning;
    scenehub_control_device_interface_info_t *out_interface_info;
    scenehub_control_device_command_info_t *out_command_info;
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
    return mqtt_core_publish(topic, payload);
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
