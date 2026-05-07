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
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gm_game_profile.h"
#include "gm_room_session.h"
#include "mqtt_core.h"
#include "orchestrator_api_view.h"
#include "orchestrator_audit.h"
#include "orchestrator_registry.h"
#include "orchestrator_timeline.h"
#include "quest_device.h"
#include "room_catalog.h"
#include "room_scenario.h"
#include "web_ui_utils.h"

#define ORCH_AUDIT_RECENT_DEFAULT 32
#define ORCH_TIMELINE_RECENT_DEFAULT 64
#define ORCH_ROOM_SCENARIOS_DEFAULT ORCH_REGISTRY_MAX_ROOM_SCENARIOS
#define ORCH_INTERFACE_DISCOVERY_TIMEOUT_MS 3000
#define ORCH_INTERFACE_DISCOVERY_POLL_MS 100

static const char *TAG = "web_ui_orchestrator";
EXT_RAM_BSS_ATTR static orch_registry_snapshot_t s_gm_state_snapshot;
EXT_RAM_BSS_ATTR static room_scenario_t s_room_scenario_response_scratch;
static SemaphoreHandle_t s_room_scenario_response_mutex = NULL;
static StaticSemaphore_t s_room_scenario_response_mutex_storage;

static esp_err_t orch_room_scenario_response_lock(void)
{
    if (!s_room_scenario_response_mutex) {
        s_room_scenario_response_mutex = xSemaphoreCreateMutexStatic(&s_room_scenario_response_mutex_storage);
    }
    if (!s_room_scenario_response_mutex) {
        return ESP_ERR_NO_MEM;
    }
    return xSemaphoreTake(s_room_scenario_response_mutex, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void orch_room_scenario_response_unlock(void)
{
    if (s_room_scenario_response_mutex) {
        xSemaphoreGive(s_room_scenario_response_mutex);
    }
}

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

esp_err_t gm_room_scenarios_handler(httpd_req_t *req)
{
    char room_id[ROOM_SCENARIO_ROOM_ID_MAX_LEN] = {0};
    room_scenario_t *scenario = &s_room_scenario_response_scratch;
    size_t scenario_count = 0;
    cJSON *root = NULL;
    cJSON *items = NULL;
    esp_err_t err = ESP_OK;

    if (!orch_read_query_value(req, "room_id", room_id, sizeof(room_id)) || !room_id[0]) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "room_id required");
    }

    err = orch_room_scenario_response_lock();
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }

    err = room_scenario_list_by_room(room_id, NULL, 0, &scenario_count);
    if (err == ESP_ERR_INVALID_ARG) {
        orch_room_scenario_response_unlock();
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid room_id");
    }
    if (err != ESP_OK && err != ESP_ERR_INVALID_SIZE) {
        orch_room_scenario_response_unlock();
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
        orch_room_scenario_response_unlock();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    cJSON_AddStringToObject(root, "room_id", room_id);
    cJSON_AddNumberToObject(root, "count", scenario_count);
    for (size_t i = 0; i < scenario_count; ++i) {
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            cJSON_Delete(root);
            orch_room_scenario_response_unlock();
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
        }
        memset(scenario, 0, sizeof(*scenario));
        err = room_scenario_get_by_room_index(room_id, i, scenario, &scenario_count);
        if (scenario_count > ROOM_SCENARIO_MAX_SCENARIOS) {
            scenario_count = ROOM_SCENARIO_MAX_SCENARIOS;
        }
        if (err == ESP_OK) {
            err = room_scenario_to_json(scenario, item);
        }
        if (err == ESP_OK) {
            cJSON *validation_issues = cJSON_CreateArray();
            if (!validation_issues) {
                err = ESP_ERR_NO_MEM;
            }
            cJSON_AddNumberToObject(item, "step_count", scenario->step_count);
            cJSON_AddBoolToObject(item, "valid", true);
            cJSON_AddNumberToObject(item, "validation_issue_count", 0);
            cJSON_AddNumberToObject(item, "error_count", 0);
            cJSON_AddNumberToObject(item, "warning_count", 0);
            if (validation_issues) {
                cJSON_AddItemToObject(item, "validation_issues", validation_issues);
            }
        }
        if (err != ESP_OK) {
            cJSON_Delete(item);
            cJSON_Delete(root);
            orch_room_scenario_response_unlock();
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "room scenarios failed");
        }
        cJSON_AddItemToArray(items, item);
    }
    cJSON_AddItemToObject(root, "scenarios", items);

    err = web_ui_send_json(req, root);
    orch_room_scenario_response_unlock();
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

esp_err_t gm_versions_handler(httpd_req_t *req)
{
    uint32_t room_generation = room_catalog_generation();
    uint32_t device_generation = quest_device_generation();
    uint32_t scenario_generation = room_scenario_generation();
    uint32_t profile_generation = gm_game_profile_generation();
    uint32_t ingest_generation = device_control_ingest_generation();
    uint32_t session_generation = gm_room_session_generation();
    uint32_t static_generation = device_generation ^
                                 (room_generation << 1) ^
                                 (scenario_generation << 2) ^
                                 (profile_generation << 3);
    uint32_t runtime_generation = ingest_generation ^ (session_generation << 1);
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    cJSON_AddNumberToObject(root, "rooms", room_generation);
    cJSON_AddNumberToObject(root, "devices", device_generation);
    cJSON_AddNumberToObject(root, "scenarios", scenario_generation);
    cJSON_AddNumberToObject(root, "profiles", profile_generation);
    cJSON_AddNumberToObject(root, "ingest", ingest_generation);
    cJSON_AddNumberToObject(root, "session", session_generation);
    cJSON_AddNumberToObject(root, "static", static_generation);
    cJSON_AddNumberToObject(root, "runtime", runtime_generation);
    return web_ui_send_json(req, root);
}
