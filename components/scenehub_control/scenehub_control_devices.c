#include "scenehub_control_internal.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "device_control_ingest.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gm_api.h"
#include "mqtt_core.h"
#include "quest_device.h"
#include "scenehub_device_command_resolver.h"

static const char *TAG = "scenehub_control";
static const uint32_t SCENEHUB_CONTROL_INTERFACE_DISCOVERY_TIMEOUT_MS = 3000;
static const uint32_t SCENEHUB_CONTROL_INTERFACE_DISCOVERY_POLL_MS = 100;

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

static const char *scenehub_control_json_string(const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : "";
}

static bool scenehub_control_json_has_key(const cJSON *obj, const char *key)
{
    return cJSON_IsObject(obj) && key && key[0] &&
           cJSON_GetObjectItemCaseSensitive(obj, key) != NULL;
}

static bool scenehub_control_resource_array_has_gpio(const cJSON *array)
{
    if (!cJSON_IsArray(array)) {
        return false;
    }
    int count = cJSON_GetArraySize(array);
    for (int i = 0; i < count; ++i) {
        if (scenehub_control_json_has_key(cJSON_GetArrayItem(array, i), "gpio")) {
            return true;
        }
    }
    return false;
}

static bool scenehub_control_manifest_has_gpio(const cJSON *manifest)
{
    static const char *resource_keys[] = {
        "relays",
        "mosfets",
        "inputs",
        "outputs",
        "led_strips",
    };
    const cJSON *resources = cJSON_GetObjectItemCaseSensitive(manifest, "resources");
    if (!cJSON_IsObject(resources)) {
        return false;
    }
    for (size_t i = 0; i < sizeof(resource_keys) / sizeof(resource_keys[0]); ++i) {
        if (scenehub_control_resource_array_has_gpio(
                cJSON_GetObjectItemCaseSensitive(resources, resource_keys[i]))) {
            return true;
        }
    }
    return false;
}

static bool scenehub_control_compact_manifest_root_present(const cJSON *manifest)
{
    return cJSON_IsObject(manifest) &&
           (scenehub_control_json_has_key(manifest, "manifest_version") ||
            scenehub_control_json_has_key(manifest, "format") ||
            scenehub_control_json_has_key(manifest, "node_kind") ||
            scenehub_control_json_has_key(manifest, "capability_contract") ||
            scenehub_control_json_has_key(manifest, "resources") ||
            scenehub_control_json_has_key(manifest, "command_templates") ||
            scenehub_control_json_has_key(manifest, "event_templates"));
}

static void scenehub_control_fill_device_payload_error(scenehub_control_result_t *result,
                                                       const cJSON *device_payload,
                                                       esp_err_t err)
{
    const cJSON *manifest = cJSON_GetObjectItemCaseSensitive(device_payload, "device_description");
    const cJSON *manifest_version = cJSON_GetObjectItemCaseSensitive(manifest, "manifest_version");
    const cJSON *resources = cJSON_GetObjectItemCaseSensitive(manifest, "resources");
    const cJSON *commands = cJSON_GetObjectItemCaseSensitive(manifest, "command_templates");
    const cJSON *events = cJSON_GetObjectItemCaseSensitive(manifest, "event_templates");
    const cJSON *schemas = cJSON_GetObjectItemCaseSensitive(manifest, "schemas");

    if (!cJSON_IsObject(manifest) || !scenehub_control_compact_manifest_root_present(manifest)) {
        scenehub_control_fill_common_error(result, err);
        return;
    }
    if (scenehub_control_json_has_key(manifest, "version") ||
        scenehub_control_json_has_key(manifest, "commands") ||
        scenehub_control_json_has_key(manifest, "events")) {
        scenehub_control_set_result(result,
                                    SCENEHUB_CONTROL_STATUS_REJECTED,
                                    err,
                                    false,
                                    "invalid_device_manifest",
                                    "SceneHub Node manifest must use compact v2 fields only; root version/commands/events are not supported.");
        return;
    }
    if (!cJSON_IsNumber(manifest_version) || manifest_version->valueint != 2 ||
        strcmp(scenehub_control_json_string(manifest, "format"), "compact_resources") != 0 ||
        !scenehub_control_json_string(manifest, "node_kind")[0] ||
        strcmp(scenehub_control_json_string(manifest, "capability_contract"),
               "scenehub.node.compact.v1") != 0) {
        scenehub_control_set_result(result,
                                    SCENEHUB_CONTROL_STATUS_REJECTED,
                                    err,
                                    false,
                                    "invalid_device_manifest_identity",
                                    "SceneHub Node manifest must include manifest_version=2, format=compact_resources, node_kind and capability_contract=scenehub.node.compact.v1.");
        return;
    }
    if (!cJSON_IsObject(resources) ||
        !cJSON_IsArray(commands) ||
        !cJSON_IsArray(events) ||
        !cJSON_IsObject(schemas)) {
        scenehub_control_set_result(result,
                                    SCENEHUB_CONTROL_STATUS_REJECTED,
                                    err,
                                    false,
                                    "invalid_device_manifest_shape",
                                    "SceneHub Node manifest must include resources, command_templates, event_templates and schemas.");
        return;
    }
    if (scenehub_control_manifest_has_gpio(manifest)) {
        scenehub_control_set_result(result,
                                    SCENEHUB_CONTROL_STATUS_REJECTED,
                                    err,
                                    false,
                                    "invalid_device_manifest_gpio",
                                    "GPIO pins are node-local configuration and must not be exposed in the SceneHub device manifest.");
        return;
    }
    scenehub_control_set_result(result,
                                SCENEHUB_CONTROL_STATUS_REJECTED,
                                err,
                                false,
                                "invalid_device_manifest",
                                "SceneHub Node manifest is invalid; check duplicate ids and schema references.");
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
    return scenehub_control_finalize_api_result_with_invalidation(
        out_result,
        scenehub_control_persistence_enabled() ? quest_device_upsert_and_save(device)
                                               : quest_device_upsert(device),
        SCENEHUB_STATE_SLICE_DEVICES_CATALOG,
        device->id,
        "device_save");
}

esp_err_t scenehub_control_save_device_payload(const char *source,
                                               const cJSON *payload,
                                               cJSON **out_device_json,
                                               scenehub_control_result_t *out_result)
{
    (void)source;
    const cJSON *device_payload = NULL;
    quest_device_t *device = NULL;
    cJSON *device_json = NULL;
    esp_err_t err = scenehub_control_prepare_result("", "device_save", out_result);
    if (err != ESP_OK) {
        return err;
    }
    if (out_device_json) {
        *out_device_json = NULL;
    }
    if (!payload) {
        scenehub_control_fill_common_error(out_result, ESP_ERR_INVALID_ARG);
        return ESP_OK;
    }

    device_payload = cJSON_GetObjectItemCaseSensitive(payload, "device");
    if (!cJSON_IsObject(device_payload)) {
        device_payload = payload;
    }

    device = heap_caps_calloc(1, sizeof(*device), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!device) {
        device = heap_caps_calloc(1, sizeof(*device), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!device) {
        scenehub_control_fill_common_error(out_result, ESP_ERR_NO_MEM);
        return ESP_OK;
    }

    err = quest_device_from_json(device_payload, device);
    if (err == ESP_OK && out_device_json) {
        device_json = cJSON_CreateObject();
        if (!device_json) {
            err = ESP_ERR_NO_MEM;
        } else {
            err = quest_device_to_json(device, device_json);
        }
    }
    if (err != ESP_OK) {
        cJSON_Delete(device_json);
        heap_caps_free(device);
        scenehub_control_fill_device_payload_error(out_result, device_payload, err);
        return ESP_OK;
    }

    err = scenehub_control_finalize_api_result_with_invalidation(
        out_result,
        scenehub_control_persistence_enabled() ? quest_device_upsert_and_save(device)
                                               : quest_device_upsert(device),
        SCENEHUB_STATE_SLICE_DEVICES_CATALOG,
        device->id,
        "device_save");
    heap_caps_free(device);
    if (out_result->status != SCENEHUB_CONTROL_STATUS_DONE) {
        cJSON_Delete(device_json);
        device_json = NULL;
    }
    if (out_device_json) {
        *out_device_json = device_json;
    } else {
        cJSON_Delete(device_json);
    }
    return err;
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
    return scenehub_control_finalize_api_result_with_invalidation(
        out_result,
        scenehub_control_persistence_enabled() ? quest_device_delete_and_save(device_id)
                                               : quest_device_delete(device_id),
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
    scenehub_resolved_device_command_t resolved = {0};
    esp_err_t err = scenehub_control_prepare_result("", "device_command_run", out_result);
    if (err != ESP_OK) {
        return err;
    }
    if (out_info) {
        memset(out_info, 0, sizeof(*out_info));
    }

    err = scenehub_device_command_resolve(device_id,
                                          command_id,
                                          params_json,
                                          true,
                                          &resolved,
                                          NULL,
                                          0);
    if (err == ESP_OK && out_info) {
        scenehub_control_copy(out_info->device_name,
                              sizeof(out_info->device_name),
                              resolved.device_name);
    }
    if (err == ESP_OK && out_info) {
        scenehub_control_copy(out_info->command_label,
                              sizeof(out_info->command_label),
                              resolved.command.label);
    }
    if (err == ESP_OK) {
        log_warning = resolved.command.requires_confirmation ||
                      strcmp(resolved.command.danger_level, "normal") != 0;
    }
    if (err == ESP_OK && !resolved.command.manual_allowed) {
        err = ESP_ERR_INVALID_STATE;
    }

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
