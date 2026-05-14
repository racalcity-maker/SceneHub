#include "scenehub_control_internal.h"

#include <stdio.h>
#include <string.h>

#include "device_control_ingest.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gm_api.h"
#include "mqtt_core.h"
#include "quest_device.h"

static const char *TAG = "scenehub_control";
static const uint32_t SCENEHUB_CONTROL_INTERFACE_DISCOVERY_TIMEOUT_MS = 3000;
static const uint32_t SCENEHUB_CONTROL_INTERFACE_DISCOVERY_POLL_MS = 100;

static SemaphoreHandle_t s_device_command_scratch_mutex = NULL;
static StaticSemaphore_t s_device_command_scratch_mutex_storage;
static portMUX_TYPE s_device_command_scratch_mutex_init_lock = portMUX_INITIALIZER_UNLOCKED;
static quest_device_t s_device_command_scratch_device;
static quest_device_command_t s_device_command_scratch_command;

static esp_err_t scenehub_control_publish_describe_interface(const char *client_id,
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

static cJSON *scenehub_control_extract_device_description(const char *data_json)
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

static esp_err_t scenehub_control_device_command_scratch_lock(void)
{
    if (!s_device_command_scratch_mutex) {
        portENTER_CRITICAL(&s_device_command_scratch_mutex_init_lock);
        if (!s_device_command_scratch_mutex) {
            s_device_command_scratch_mutex =
                xSemaphoreCreateMutexStatic(&s_device_command_scratch_mutex_storage);
        }
        portEXIT_CRITICAL(&s_device_command_scratch_mutex_init_lock);
        if (!s_device_command_scratch_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }
    return xSemaphoreTake(s_device_command_scratch_mutex, portMAX_DELAY) == pdTRUE ? ESP_OK
                                                                                    : ESP_ERR_TIMEOUT;
}

static void scenehub_control_device_command_scratch_unlock(void)
{
    if (s_device_command_scratch_mutex) {
        xSemaphoreGive(s_device_command_scratch_mutex);
    }
}

esp_err_t scenehub_control_save_device(const char *source,
                                       const quest_device_t *device,
                                       scenehub_control_result_t *out_result)
{
    (void)source;
    esp_err_t err = scenehub_control_prepare_result("", "device_save", out_result);
    if (err != ESP_OK) {
        return err;
    }
    if (!device) {
        scenehub_control_fill_common_error(out_result, ESP_ERR_INVALID_ARG);
        return ESP_OK;
    }
    return scenehub_control_finalize_api_result_with_invalidation(out_result,
                                                                  quest_device_upsert_and_save(device),
                                                                  SCENEHUB_STATE_SLICE_DEVICES_CATALOG,
                                                                  device->id,
                                                                  "device_save");
}

esp_err_t scenehub_control_delete_device(const char *source,
                                         const char *device_id,
                                         scenehub_control_result_t *out_result)
{
    (void)source;
    esp_err_t err = scenehub_control_prepare_result("", "device_delete", out_result);
    if (err != ESP_OK) {
        return err;
    }
    return scenehub_control_finalize_api_result_with_invalidation(out_result,
                                                                  quest_device_delete_and_save(device_id),
                                                                  SCENEHUB_STATE_SLICE_DEVICES_CATALOG,
                                                                  device_id,
                                                                  "device_delete");
}

esp_err_t scenehub_control_device_command_run(const char *source,
                                              const char *device_id,
                                              const char *command_id,
                                              const char *params_json,
                                              scenehub_control_device_command_info_t *out_info,
                                              scenehub_control_result_t *out_result)
{
    bool log_warning = false;
    esp_err_t err = scenehub_control_prepare_result("", "device_command_run", out_result);
    if (err != ESP_OK) {
        return err;
    }
    if (out_info) {
        memset(out_info, 0, sizeof(*out_info));
    }

    err = scenehub_control_device_command_scratch_lock();
    if (err != ESP_OK) {
        return err;
    }

    memset(&s_device_command_scratch_device, 0, sizeof(s_device_command_scratch_device));
    memset(&s_device_command_scratch_command, 0, sizeof(s_device_command_scratch_command));

    err = quest_device_get(device_id, &s_device_command_scratch_device);
    if (err == ESP_OK && out_info) {
        scenehub_control_copy(out_info->device_name,
                              sizeof(out_info->device_name),
                              s_device_command_scratch_device.name);
    }
    if (err == ESP_OK && !s_device_command_scratch_device.enabled) {
        err = ESP_ERR_INVALID_STATE;
    }
    if (err == ESP_OK) {
        err = quest_device_get_command(device_id, command_id, &s_device_command_scratch_command);
    }
    if (err == ESP_OK && out_info) {
        scenehub_control_copy(out_info->command_label,
                              sizeof(out_info->command_label),
                              s_device_command_scratch_command.label);
    }
    if (err == ESP_OK) {
        log_warning = s_device_command_scratch_command.requires_confirmation ||
                      strcmp(s_device_command_scratch_command.danger_level, "normal") != 0;
    }
    if (err == ESP_OK && !s_device_command_scratch_command.manual_allowed) {
        err = ESP_ERR_INVALID_STATE;
    }
    scenehub_control_device_command_scratch_unlock();

    if (err == ESP_OK) {
        err = gm_api_device_command_run(device_id, command_id, params_json);
    }
    if (err != ESP_OK) {
        scenehub_control_fill_common_error(out_result, err);
        return ESP_OK;
    }

    scenehub_control_log_device_action(source, device_id, log_warning, command_id);
    scenehub_control_finish_success_with_invalidation(out_result,
                                                      SCENEHUB_STATE_SLICE_DEVICES_RUNTIME,
                                                      device_id,
                                                      "device_command_run");
    return ESP_OK;
}

esp_err_t scenehub_control_device_describe_interface(
    const char *source,
    const char *client_id,
    scenehub_control_device_interface_info_t *out_info,
    scenehub_control_result_t *out_result)
{
    device_control_ingest_device_t device = {0};
    uint64_t start_ms = (uint64_t)(esp_timer_get_time() / 1000);
    uint64_t deadline_ms = start_ms + SCENEHUB_CONTROL_INTERFACE_DISCOVERY_TIMEOUT_MS;
    esp_err_t err = scenehub_control_prepare_result("", "device_describe_interface", out_result);

    (void)source;
    if (err != ESP_OK) {
        return err;
    }
    if (out_info) {
        memset(out_info, 0, sizeof(*out_info));
    }
    if (!client_id || !client_id[0] || !out_info) {
        scenehub_control_fill_common_error(out_result, ESP_ERR_INVALID_ARG);
        return ESP_OK;
    }

    snprintf(out_info->request_id,
             sizeof(out_info->request_id),
             "iface_%llu",
             (unsigned long long)start_ms);
    ESP_LOGI(TAG, "describe_interface request client=%s request_id=%s", client_id, out_info->request_id);

    err = scenehub_control_publish_describe_interface(client_id, out_info->request_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "describe_interface publish failed client=%s request_id=%s err=%s",
                 client_id,
                 out_info->request_id,
                 esp_err_to_name(err));
        scenehub_control_set_result(out_result,
                                    SCENEHUB_CONTROL_STATUS_FAILED,
                                    err,
                                    false,
                                    "publish_failed",
                                    "Describe interface publish failed");
        return ESP_OK;
    }

    while ((uint64_t)(esp_timer_get_time() / 1000) < deadline_ms) {
        memset(&device, 0, sizeof(device));
        if (device_control_ingest_get_device(client_id, &device) == ESP_OK &&
            device.has_result &&
            strcmp(device.result_request_id, out_info->request_id) == 0 &&
            strcmp(device.result_command, "describe_interface") == 0) {
            if (strcmp(device.result_status, "accepted") == 0) {
                vTaskDelay(pdMS_TO_TICKS(SCENEHUB_CONTROL_INTERFACE_DISCOVERY_POLL_MS));
                continue;
            }
            if (strcmp(device.result_status, "ok") != 0 &&
                strcmp(device.result_status, "done") != 0) {
                ESP_LOGW(TAG, "describe_interface device error client=%s request_id=%s status=%s code=%s",
                         client_id,
                         out_info->request_id,
                         device.result_status,
                         device.result_error_code);
                scenehub_control_set_result(out_result,
                                            SCENEHUB_CONTROL_STATUS_REJECTED,
                                            ESP_ERR_INVALID_RESPONSE,
                                            false,
                                            device.result_error_code[0] ? device.result_error_code
                                                                        : "device_error",
                                            "Device rejected interface description");
                return ESP_OK;
            }
            out_info->device_description =
                scenehub_control_extract_device_description(device.result_data_json);
            if (!out_info->device_description) {
                scenehub_control_set_result(out_result,
                                            SCENEHUB_CONTROL_STATUS_FAILED,
                                            ESP_ERR_INVALID_RESPONSE,
                                            false,
                                            "missing_device_description",
                                            "Device returned no device_description");
                return ESP_OK;
            }
            ESP_LOGI(TAG, "describe_interface success client=%s request_id=%s", client_id, out_info->request_id);
            scenehub_control_finish_success_no_state_change(out_result);
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(SCENEHUB_CONTROL_INTERFACE_DISCOVERY_POLL_MS));
    }

    ESP_LOGW(TAG, "describe_interface timeout client=%s request_id=%s", client_id, out_info->request_id);
    scenehub_control_set_result(out_result,
                                SCENEHUB_CONTROL_STATUS_TIMEOUT,
                                ESP_ERR_TIMEOUT,
                                false,
                                "timeout",
                                "Describe interface timed out");
    return ESP_OK;
}

esp_err_t scenehub_control_import_devices(const char *source,
                                          cJSON *root,
                                          scenehub_control_result_t *out_result)
{
    (void)source;
    esp_err_t err = scenehub_control_prepare_result("", "device_import", out_result);
    if (err != ESP_OK) {
        return err;
    }
    return scenehub_control_finalize_api_result_with_invalidation(out_result,
                                                                  quest_device_import_json_and_save(root),
                                                                  SCENEHUB_STATE_SLICE_DEVICES_CATALOG,
                                                                  "",
                                                                  "device_import");
}

esp_err_t scenehub_control_load_devices(const char *source,
                                        scenehub_control_result_t *out_result)
{
    (void)source;
    esp_err_t err = scenehub_control_prepare_result("", "device_load", out_result);
    if (err != ESP_OK) {
        return err;
    }
    return scenehub_control_finalize_api_result_with_invalidation(out_result,
                                                                  quest_device_load(),
                                                                  SCENEHUB_STATE_SLICE_DEVICES_CATALOG,
                                                                  "",
                                                                  "device_load");
}

esp_err_t scenehub_control_save_devices_store(const char *source,
                                              scenehub_control_result_t *out_result)
{
    (void)source;
    esp_err_t err = scenehub_control_prepare_result("", "device_store_save", out_result);
    if (err != ESP_OK) {
        return err;
    }
    return scenehub_control_finalize_no_state_change_result(out_result, quest_device_save());
}
