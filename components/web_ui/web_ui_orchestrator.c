#include "web_ui_handlers.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "device_control_ingest.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_core.h"
#include "orchestrator_api_view.h"
#include "orchestrator_audit.h"
#include "orchestrator_registry.h"
#include "orchestrator_timeline.h"
#include "room_scenario.h"
#include "web_ui_utils.h"

#define ORCH_AUDIT_RECENT_DEFAULT 32
#define ORCH_TIMELINE_RECENT_DEFAULT 64
#define ORCH_ROOM_SCENARIOS_DEFAULT ORCH_REGISTRY_MAX_ROOM_SCENARIOS
#define ORCH_INTERFACE_DISCOVERY_TIMEOUT_MS 3000
#define ORCH_INTERFACE_DISCOVERY_POLL_MS 100

static const char *TAG = "web_ui_orchestrator";
EXT_RAM_BSS_ATTR static orch_registry_snapshot_t s_gm_state_snapshot;

static void *orch_snapshot_alloc(size_t size)
{
    void *ptr = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
        ptr = heap_caps_calloc(1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return ptr;
}

static bool orch_read_query_value(httpd_req_t *req, const char *key, char *out, size_t out_size)
{
    char query[256] = {0};
    if (!req || !key || !out || out_size == 0) {
        return false;
    }
    out[0] = '\0';
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;
    }
    return httpd_query_key_value(query, key, out, out_size) == ESP_OK;
}

static esp_err_t orch_read_json_body(httpd_req_t *req, size_t max_len, char **out_body)
{
    char *body = NULL;
    size_t received = 0;
    if (!req || !out_body) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_body = NULL;
    if (req->content_len <= 0 || req->content_len > (int)max_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    body = orch_snapshot_alloc((size_t)req->content_len + 1);
    if (!body) {
        return ESP_ERR_NO_MEM;
    }
    while (received < (size_t)req->content_len) {
        int r = httpd_req_recv(req, body + received, req->content_len - received);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            heap_caps_free(body);
            return ESP_FAIL;
        }
        received += (size_t)r;
    }
    body[received] = '\0';
    *out_body = body;
    return ESP_OK;
}

static const char *orch_room_scenario_validation_level_str(room_scenario_validation_level_t level)
{
    return level == ROOM_SCENARIO_VALIDATION_WARNING ? "warning" : "error";
}

static esp_err_t orch_add_room_scenario_validation(cJSON *obj,
                                                   const room_scenario_t *scenario)
{
    room_scenario_validation_report_t *report = NULL;
    cJSON *issues = NULL;
    size_t error_count = 0;
    size_t warning_count = 0;
    esp_err_t err = ESP_OK;

    if (!obj || !scenario) {
        return ESP_ERR_INVALID_ARG;
    }
    report = orch_snapshot_alloc(sizeof(*report));
    issues = cJSON_CreateArray();
    if (!report || !issues) {
        heap_caps_free(report);
        cJSON_Delete(issues);
        return ESP_ERR_NO_MEM;
    }
    err = room_scenario_validate(scenario, report);
    if (err != ESP_OK) {
        heap_caps_free(report);
        cJSON_Delete(issues);
        return err;
    }
    for (size_t i = 0; i < report->issue_count; ++i) {
        const room_scenario_validation_issue_t *issue = &report->issues[i];
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            heap_caps_free(report);
            cJSON_Delete(issues);
            return ESP_ERR_NO_MEM;
        }
        if (issue->level == ROOM_SCENARIO_VALIDATION_WARNING) {
            ++warning_count;
        } else {
            ++error_count;
        }
        cJSON_AddStringToObject(item, "level", orch_room_scenario_validation_level_str(issue->level));
        cJSON_AddNumberToObject(item, "step_index", issue->step_index);
        cJSON_AddStringToObject(item, "code", issue->code);
        cJSON_AddStringToObject(item, "message", issue->message);
        cJSON_AddItemToArray(issues, item);
    }
    cJSON_AddBoolToObject(obj, "valid", report->valid);
    cJSON_AddNumberToObject(obj, "validation_issue_count", report->issue_count);
    cJSON_AddNumberToObject(obj, "error_count", error_count);
    cJSON_AddNumberToObject(obj, "warning_count", warning_count);
    cJSON_AddItemToObject(obj, "validation_issues", issues);
    heap_caps_free(report);
    return ESP_OK;
}

esp_err_t gm_room_scenarios_handler(httpd_req_t *req)
{
    char room_id[ROOM_SCENARIO_ROOM_ID_MAX_LEN] = {0};
    room_scenario_t *scenarios = NULL;
    size_t scenario_count = 0;
    cJSON *root = NULL;
    cJSON *items = NULL;
    esp_err_t err = ESP_OK;

    if (!orch_read_query_value(req, "room_id", room_id, sizeof(room_id)) || !room_id[0]) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "room_id required");
    }

    scenarios = orch_snapshot_alloc(sizeof(*scenarios) * ROOM_SCENARIO_MAX_SCENARIOS);
    if (!scenarios) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }

    err = room_scenario_list_by_room(room_id,
                                     scenarios,
                                     ROOM_SCENARIO_MAX_SCENARIOS,
                                     &scenario_count);
    if (err == ESP_ERR_INVALID_ARG) {
        heap_caps_free(scenarios);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid room_id");
    }
    if (err != ESP_OK && err != ESP_ERR_INVALID_SIZE) {
        heap_caps_free(scenarios);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "room scenarios failed");
    }
    if (scenario_count > ROOM_SCENARIO_MAX_SCENARIOS) {
        scenario_count = ROOM_SCENARIO_MAX_SCENARIOS;
    }

    root = cJSON_CreateObject();
    items = cJSON_CreateArray();
    if (!root || !items) {
        cJSON_Delete(root);
        cJSON_Delete(items);
        heap_caps_free(scenarios);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    cJSON_AddStringToObject(root, "room_id", room_id);
    cJSON_AddNumberToObject(root, "count", scenario_count);
    for (size_t i = 0; i < scenario_count; ++i) {
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            cJSON_Delete(root);
            heap_caps_free(scenarios);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
        }
        err = room_scenario_to_json(&scenarios[i], item);
        if (err == ESP_OK) {
            cJSON_AddNumberToObject(item, "step_count", scenarios[i].step_count);
            err = orch_add_room_scenario_validation(item, &scenarios[i]);
        }
        if (err != ESP_OK) {
            cJSON_Delete(item);
            cJSON_Delete(root);
            heap_caps_free(scenarios);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "room scenarios failed");
        }
        cJSON_AddItemToArray(items, item);
    }
    cJSON_AddItemToObject(root, "scenarios", items);

    err = web_ui_send_json(req, root);
    heap_caps_free(scenarios);
    return err;
}

static esp_err_t orch_send_interface_discovery_error(httpd_req_t *req,
                                                    const char *status,
                                                    const char *error,
                                                    const char *client_id,
                                                    const char *request_id)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    cJSON_AddBoolToObject(root, "ok", false);
    cJSON_AddStringToObject(root, "error", error ? error : "interface_discovery_failed");
    cJSON_AddStringToObject(root, "client_id", client_id ? client_id : "");
    cJSON_AddStringToObject(root, "request_id", request_id ? request_id : "");
    if (status && status[0]) {
        httpd_resp_set_status(req, status);
    }
    return web_ui_send_json(req, root);
}

static esp_err_t orch_publish_describe_interface(const char *client_id, const char *request_id)
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

static cJSON *orch_extract_device_description(const char *data_json)
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

esp_err_t orchestrator_describe_interface_handler(httpd_req_t *req)
{
    char *body = NULL;
    cJSON *json = NULL;
    const cJSON *client_id_item = NULL;
    char client_id[QUEST_ID_MAX_LEN] = {0};
    char request_id[DEVICE_CONTROL_INGEST_REQUEST_ID_MAX_LEN] = {0};
    uint64_t start_ms = (uint64_t)(esp_timer_get_time() / 1000);
    uint64_t deadline_ms = start_ms + ORCH_INTERFACE_DISCOVERY_TIMEOUT_MS;
    esp_err_t err = orch_read_json_body(req, 512, &body);

    if (err == ESP_ERR_INVALID_SIZE || err == ESP_ERR_INVALID_ARG) {
        return orch_send_interface_discovery_error(req, "400 Bad Request", "invalid_request", "", "");
    }
    if (err == ESP_ERR_NO_MEM) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    if (err != ESP_OK) {
        return orch_send_interface_discovery_error(req, "500 Internal Server Error", "execution_failed", "", "");
    }

    json = cJSON_Parse(body);
    heap_caps_free(body);
    if (!json) {
        return orch_send_interface_discovery_error(req, "400 Bad Request", "invalid_json", "", "");
    }
    client_id_item = cJSON_GetObjectItemCaseSensitive(json, "client_id");
    if (!cJSON_IsString(client_id_item) || !client_id_item->valuestring || !client_id_item->valuestring[0]) {
        cJSON_Delete(json);
        return orch_send_interface_discovery_error(req, "400 Bad Request", "client_id_required", "", "");
    }
    snprintf(client_id, sizeof(client_id), "%s", client_id_item->valuestring);
    cJSON_Delete(json);

    snprintf(request_id,
             sizeof(request_id),
             "iface_%llu",
             (unsigned long long)start_ms);
    ESP_LOGI(TAG, "describe_interface request client=%s request_id=%s", client_id, request_id);
    err = orch_publish_describe_interface(client_id, request_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "describe_interface publish failed client=%s request_id=%s err=%s",
                 client_id,
                 request_id,
                 esp_err_to_name(err));
        return orch_send_interface_discovery_error(req,
                                                  "500 Internal Server Error",
                                                  "publish_failed",
                                                  client_id,
                                                  request_id);
    }

    device_control_ingest_device_t *device = orch_snapshot_alloc(sizeof(*device));
    if (!device) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    while ((uint64_t)(esp_timer_get_time() / 1000) < deadline_ms) {
        memset(device, 0, sizeof(*device));
        if (device_control_ingest_get_device(client_id, device) == ESP_OK &&
            device->has_result &&
            strcmp(device->result_request_id, request_id) == 0 &&
            strcmp(device->result_command, "describe_interface") == 0) {
            if (strcmp(device->result_status, "accepted") == 0) {
                vTaskDelay(pdMS_TO_TICKS(ORCH_INTERFACE_DISCOVERY_POLL_MS));
                continue;
            }
            if (strcmp(device->result_status, "ok") != 0 &&
                strcmp(device->result_status, "done") != 0) {
                ESP_LOGW(TAG, "describe_interface device error client=%s request_id=%s status=%s code=%s",
                         client_id,
                         request_id,
                         device->result_status,
                         device->result_error_code);
                esp_err_t send_err = orch_send_interface_discovery_error(req,
                                                                         "422 Unprocessable Entity",
                                                                         device->result_error_code[0] ? device->result_error_code : "device_error",
                                                                         client_id,
                                                                         request_id);
                heap_caps_free(device);
                return send_err;
            }
            cJSON *device_description = orch_extract_device_description(device->result_data_json);
            if (!device_description) {
                esp_err_t send_err = orch_send_interface_discovery_error(req,
                                                                         "422 Unprocessable Entity",
                                                                         "missing_device_description",
                                                                         client_id,
                                                                         request_id);
                heap_caps_free(device);
                return send_err;
            }
            cJSON *root = cJSON_CreateObject();
            if (!root) {
                cJSON_Delete(device_description);
                heap_caps_free(device);
                return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
            }
            cJSON_AddBoolToObject(root, "ok", true);
            cJSON_AddStringToObject(root, "client_id", client_id);
            cJSON_AddStringToObject(root, "request_id", request_id);
            cJSON_AddItemToObject(root, "device_description", device_description);
            ESP_LOGI(TAG, "describe_interface success client=%s request_id=%s", client_id, request_id);
            esp_err_t send_err = web_ui_send_json(req, root);
            heap_caps_free(device);
            return send_err;
        }
        vTaskDelay(pdMS_TO_TICKS(ORCH_INTERFACE_DISCOVERY_POLL_MS));
    }

    ESP_LOGW(TAG, "describe_interface timeout client=%s request_id=%s", client_id, request_id);
    esp_err_t send_err = orch_send_interface_discovery_error(req,
                                                            "504 Gateway Timeout",
                                                            "timeout",
                                                            client_id,
                                                            request_id);
    heap_caps_free(device);
    return send_err;
}

esp_err_t orchestrator_audit_recent_handler(httpd_req_t *req)
{
    orchestrator_audit_entry_t *entries = NULL;
    size_t count = 0;
    cJSON *root = NULL;
    esp_err_t err = ESP_OK;

    entries = orch_snapshot_alloc(sizeof(*entries) * ORCH_AUDIT_RECENT_DEFAULT);
    if (!entries) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }

    err = orchestrator_audit_list_recent(ORCH_AUDIT_RECENT_DEFAULT, entries, &count);
    if (err != ESP_OK) {
        heap_caps_free(entries);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "audit unavailable");
    }

    root = orchestrator_api_view_audit_recent(entries, count);
    if (!root) {
        heap_caps_free(entries);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }

    err = web_ui_send_json(req, root);
    heap_caps_free(entries);
    return err;
}

esp_err_t orchestrator_timeline_recent_handler(httpd_req_t *req)
{
    orchestrator_timeline_entry_t *entries = NULL;
    size_t count = 0;
    cJSON *root = NULL;
    esp_err_t err = ESP_OK;

    entries = orch_snapshot_alloc(sizeof(*entries) * ORCH_TIMELINE_RECENT_DEFAULT);
    if (!entries) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }

    err = orchestrator_timeline_list_recent(ORCH_TIMELINE_RECENT_DEFAULT, entries, &count);
    if (err != ESP_OK) {
        heap_caps_free(entries);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "timeline unavailable");
    }

    root = orchestrator_api_view_timeline_recent(entries, count);
    if (!root) {
        heap_caps_free(entries);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }

    err = web_ui_send_json(req, root);
    heap_caps_free(entries);
    return err;
}

esp_err_t orchestrator_control_devices_handler(httpd_req_t *req)
{
    device_control_ingest_device_t *devices = NULL;
    size_t count = 0;
    uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000);
    cJSON *root = NULL;
    esp_err_t err = ESP_OK;

    devices = orch_snapshot_alloc(sizeof(*devices) * DEVICE_CONTROL_INGEST_MAX_DEVICES);
    if (!devices) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }

    err = device_control_ingest_list_devices(devices, DEVICE_CONTROL_INGEST_MAX_DEVICES, &count);
    if (err != ESP_OK) {
        heap_caps_free(devices);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "control devices unavailable");
    }

    root = orchestrator_api_view_control_devices(devices, count, now_ms);
    if (!root) {
        heap_caps_free(devices);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }

    err = web_ui_send_json(req, root);
    heap_caps_free(devices);
    return err;
}

esp_err_t gm_state_handler(httpd_req_t *req)
{
    cJSON *root = NULL;

    memset(&s_gm_state_snapshot, 0, sizeof(s_gm_state_snapshot));
    esp_err_t err = orchestrator_registry_build_snapshot(&s_gm_state_snapshot);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "gm snapshot failed");
    }

    root = orchestrator_api_view_gm_state(&s_gm_state_snapshot);
    if (!root) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }

    err = web_ui_send_json(req, root);
    return err;
}
