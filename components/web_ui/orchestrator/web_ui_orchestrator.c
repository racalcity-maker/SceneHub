#include "web_ui_handlers.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/portmacro.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "orch_device_view.h"
#include "orch_registry_snapshot.h"
#include "orch_scenario_view.h"
#include "orchestrator/orchestrator_api_view.h"
#include "orchestrator/orchestrator_scenario_layout_writer.h"
#include "orchestrator_audit.h"
#include "orchestrator_timeline.h"
#include "quest_device.h"
#include "room_catalog.h"
#include "room_scenario.h"
#include "scenehub_control.h"
#include "web_ui_utils.h"

#define ORCH_AUDIT_RECENT_DEFAULT 32
#define ORCH_TIMELINE_RECENT_DEFAULT 64
#define ORCH_ROOM_SCENARIOS_DEFAULT ORCH_REGISTRY_MAX_ROOM_SCENARIOS
static const char *TAG = "web_ui_orch";
EXT_RAM_BSS_ATTR static orch_registry_snapshot_t s_gm_state_snapshot;
static EXT_RAM_BSS_ATTR orch_room_scenario_detail_t s_room_scenario_detail_scratch;
static EXT_RAM_BSS_ATTR room_scenario_t s_room_scenario_layout_scratch;
static SemaphoreHandle_t s_room_scenario_detail_scratch_mutex = NULL;
static StaticSemaphore_t s_room_scenario_detail_scratch_mutex_storage;
static portMUX_TYPE s_room_scenario_detail_scratch_mutex_init_lock = portMUX_INITIALIZER_UNLOCKED;

static esp_err_t orch_room_scenario_detail_scratch_lock(void)
{
    if (!s_room_scenario_detail_scratch_mutex) {
        portENTER_CRITICAL(&s_room_scenario_detail_scratch_mutex_init_lock);
        if (!s_room_scenario_detail_scratch_mutex) {
            s_room_scenario_detail_scratch_mutex =
                xSemaphoreCreateMutexStatic(&s_room_scenario_detail_scratch_mutex_storage);
        }
        portEXIT_CRITICAL(&s_room_scenario_detail_scratch_mutex_init_lock);
    }
    if (!s_room_scenario_detail_scratch_mutex) {
        return ESP_ERR_NO_MEM;
    }
    return xSemaphoreTake(s_room_scenario_detail_scratch_mutex, portMAX_DELAY) == pdTRUE
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
}

static void orch_room_scenario_detail_scratch_unlock(void)
{
    if (s_room_scenario_detail_scratch_mutex) {
        xSemaphoreGive(s_room_scenario_detail_scratch_mutex);
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

static esp_err_t orch_send_text_error(httpd_req_t *req, const char *status, const char *message)
{
    esp_err_t err = ESP_OK;
    if (!req || !status || !message) {
        return ESP_ERR_INVALID_ARG;
    }
    err = web_ui_http_resp_set_status(req, status);
    if (err != ESP_OK) {
        return err;
    }
    err = web_ui_http_resp_set_type(req, "text/plain; charset=utf-8");
    if (err != ESP_OK) {
        return err;
    }
    return web_ui_http_resp_send(req, message, HTTPD_RESP_USE_STRLEN);
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
    char raw_query[256] = {0};
    char room_id[ROOM_SCENARIO_ROOM_ID_MAX_LEN] = {0};
    char scenario_id[ROOM_SCENARIO_ID_MAX_LEN] = {0};
    char detail[16] = {0};
    bool summary_only = false;
    bool single_scenario = false;
    bool layout_only = false;
    bool full_detail_only = false;
    orch_room_scenario_detail_t *scenarios = NULL;
    orch_room_scenario_entry_t *scenario_summaries = NULL;
    size_t scenario_count = 0;
    size_t emitted_count = 0;
    cJSON *root = NULL;
    esp_err_t err = ESP_OK;

    esp_err_t query_err = web_ui_http_req_get_url_query_str(req, raw_query, sizeof(raw_query));
    if (!orch_read_query_value(req, "room_id", room_id, sizeof(room_id)) || !room_id[0]) {
        ESP_LOGW(TAG,
                 "room_scenarios missing room_id query_err=%s raw_query=%s",
                 esp_err_to_name(query_err),
                 query_err == ESP_OK ? raw_query : "");
        return orch_send_text_error(req, "400 Bad Request", "room_id required");
    }
    if (orch_read_query_value(req, "detail", detail, sizeof(detail)) && detail[0]) {
        summary_only = strcmp(detail, "summary") == 0;
        layout_only = strcmp(detail, "layout") == 0;
        full_detail_only = strcmp(detail, "detail") == 0 || strcmp(detail, "full") == 0;
    }
    if (orch_read_query_value(req, "scenario_id", scenario_id, sizeof(scenario_id)) && scenario_id[0]) {
        single_scenario = true;
        layout_only = !full_detail_only;
    }
    ESP_LOGD(TAG,
             "room_scenarios room_id=%s detail=%s summary=%d single=%d scenario_id=%s",
             room_id,
             detail,
             summary_only,
             single_scenario,
             scenario_id);

    if (single_scenario) {
        err = orch_room_scenario_detail_scratch_lock();
        if (err != ESP_OK) {
            return orch_send_text_error(req, "500 Internal Server Error", "scenario detail busy");
        }
        if (layout_only) {
            room_scenario_t *layout_copy = NULL;
            memset(&s_room_scenario_layout_scratch, 0, sizeof(s_room_scenario_layout_scratch));
            err = orchestrator_registry_get_room_scenario_layout(room_id,
                                                                 scenario_id,
                                                                 &s_room_scenario_layout_scratch);
            if (err == ESP_ERR_NOT_FOUND) {
                orch_room_scenario_detail_scratch_unlock();
                return orch_send_text_error(req, "404 Not Found", "scenario not found");
            }
            if (err != ESP_OK) {
                orch_room_scenario_detail_scratch_unlock();
                return orch_send_text_error(req, "500 Internal Server Error", "room scenarios failed");
            }
            layout_copy = orch_snapshot_alloc(sizeof(*layout_copy));
            if (!layout_copy) {
                orch_room_scenario_detail_scratch_unlock();
                return orch_send_text_error(req, "500 Internal Server Error", "no memory");
            }
            *layout_copy = s_room_scenario_layout_scratch;
            orch_room_scenario_detail_scratch_unlock();
            err = orchestrator_scenario_layout_writer_send(req, room_id, layout_copy);
            heap_caps_free(layout_copy);
            return err;
        }
        orch_room_scenario_detail_t *detail_copy = NULL;
        memset(&s_room_scenario_detail_scratch, 0, sizeof(s_room_scenario_detail_scratch));
        err = orchestrator_registry_get_room_scenario_detail(room_id, scenario_id, &s_room_scenario_detail_scratch);
        if (err == ESP_ERR_NOT_FOUND) {
            orch_room_scenario_detail_scratch_unlock();
            return orch_send_text_error(req, "404 Not Found", "scenario not found");
        }
        if (err != ESP_OK) {
            orch_room_scenario_detail_scratch_unlock();
            return orch_send_text_error(req, "500 Internal Server Error", "room scenarios failed");
        }
        detail_copy = orch_snapshot_alloc(sizeof(*detail_copy));
        if (!detail_copy) {
            orch_room_scenario_detail_scratch_unlock();
            return orch_send_text_error(req, "500 Internal Server Error", "no memory");
        }
        *detail_copy = s_room_scenario_detail_scratch;
        orch_room_scenario_detail_scratch_unlock();
        root = orchestrator_api_view_room_scenarios(room_id, detail_copy, 1);
        heap_caps_free(detail_copy);
        if (!root) {
            return orch_send_text_error(req, "500 Internal Server Error", "no memory");
        }
        cJSON_ReplaceItemInObject(root, "count", cJSON_CreateNumber(1));
        return web_ui_send_json(req, root);
    }

    if (summary_only) {
        scenario_summaries = orch_snapshot_alloc(sizeof(*scenario_summaries) * ORCH_ROOM_SCENARIOS_DEFAULT);
        if (!scenario_summaries) {
            return orch_send_text_error(req, "500 Internal Server Error", "no memory");
        }
        err = orchestrator_registry_list_room_scenarios(room_id,
                                                        scenario_summaries,
                                                        ORCH_ROOM_SCENARIOS_DEFAULT,
                                                        &scenario_count);
        if (err == ESP_ERR_NOT_FOUND) {
            err = ESP_OK;
        } else if (err == ESP_ERR_INVALID_ARG) {
            heap_caps_free(scenario_summaries);
            return orch_send_text_error(req, "400 Bad Request", "invalid room_id");
        } else if (err != ESP_OK && err != ESP_ERR_INVALID_SIZE) {
            heap_caps_free(scenario_summaries);
            return orch_send_text_error(req, "500 Internal Server Error", "room scenarios failed");
        }
        emitted_count = scenario_count < ORCH_ROOM_SCENARIOS_DEFAULT ? scenario_count : ORCH_ROOM_SCENARIOS_DEFAULT;
        root = orchestrator_api_view_room_scenario_summaries(room_id, scenario_summaries, emitted_count);
        if (!root) {
            heap_caps_free(scenario_summaries);
            ESP_LOGE(TAG, "room_scenarios summary no memory room_id=%s", room_id);
            return orch_send_text_error(req, "500 Internal Server Error", "no memory");
        }
        cJSON_ReplaceItemInObject(root, "count", cJSON_CreateNumber((double)scenario_count));
        ESP_LOGD(TAG, "room_scenarios summary ready room_id=%s count=%u emitted=%u",
                 room_id,
                 (unsigned)scenario_count,
                 (unsigned)emitted_count);
        err = web_ui_send_json(req, root);
        heap_caps_free(scenario_summaries);
        return err;
    }

    scenarios = orch_snapshot_alloc(sizeof(*scenarios) * ORCH_ROOM_SCENARIOS_DEFAULT);
    if (!scenarios) {
        return orch_send_text_error(req, "500 Internal Server Error", "no memory");
    }

    err = orchestrator_registry_list_room_scenario_details(room_id,
                                                           scenarios,
                                                           ORCH_ROOM_SCENARIOS_DEFAULT,
                                                           &scenario_count);
    if (err == ESP_ERR_NOT_FOUND) {
        err = ESP_OK;
    } else if (err == ESP_ERR_INVALID_ARG) {
        heap_caps_free(scenarios);
        return orch_send_text_error(req, "400 Bad Request", "invalid room_id");
    } else if (err != ESP_OK && err != ESP_ERR_INVALID_SIZE) {
        heap_caps_free(scenarios);
        return orch_send_text_error(req, "500 Internal Server Error", "room scenarios failed");
    }
    emitted_count = scenario_count < ORCH_ROOM_SCENARIOS_DEFAULT ? scenario_count : ORCH_ROOM_SCENARIOS_DEFAULT;
    root = orchestrator_api_view_room_scenarios(room_id, scenarios, emitted_count);
    if (!root) {
        heap_caps_free(scenarios);
        return orch_send_text_error(req, "500 Internal Server Error", "no memory");
    }
    cJSON_ReplaceItemInObject(root, "count", cJSON_CreateNumber((double)scenario_count));

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

esp_err_t orchestrator_describe_interface_handler(httpd_req_t *req)
{
    char *body = NULL;
    cJSON *json = NULL;
    const cJSON *client_id_item = NULL;
    char client_id[QUEST_ID_MAX_LEN] = {0};
    scenehub_control_device_interface_info_t info = {0};
    scenehub_control_result_t result = {0};
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

    err = scenehub_control_device_describe_interface("http", client_id, &info, &result);
    if (err != ESP_OK) {
        return orch_send_interface_discovery_error(req,
                                                  "500 Internal Server Error",
                                                  "execution_failed",
                                                  client_id,
                                                  "");
    }
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        const char *status = "422 Unprocessable Entity";
        if (result.status == SCENEHUB_CONTROL_STATUS_TIMEOUT) {
            status = "504 Gateway Timeout";
        } else if (result.status == SCENEHUB_CONTROL_STATUS_FAILED) {
            status = "500 Internal Server Error";
        }
        return orch_send_interface_discovery_error(req,
                                                   status,
                                                   result.error_code[0] ? result.error_code : "execution_failed",
                                                   client_id,
                                                   info.request_id);
    }
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        cJSON_Delete(info.device_description);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "client_id", client_id);
    cJSON_AddStringToObject(root, "request_id", info.request_id);
    cJSON_AddItemToObject(root, "device_description", info.device_description);
    return web_ui_send_json(req, root);
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
    orch_control_device_entry_t *devices = NULL;
    size_t count = 0;
    uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000);
    cJSON *root = NULL;
    esp_err_t err = ESP_OK;

    devices = orch_snapshot_alloc(sizeof(*devices) * DEVICE_CONTROL_INGEST_MAX_DEVICES);
    if (!devices) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }

    err = orchestrator_registry_list_control_devices(devices,
                                                     DEVICE_CONTROL_INGEST_MAX_DEVICES,
                                                     &count);
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

esp_err_t gm_system_summary_handler(httpd_req_t *req)
{
    orch_gm_system_summary_t summary = {0};
    cJSON *root = NULL;
    esp_err_t err = orchestrator_registry_get_system_summary(&summary);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "gm summary failed");
    }
    root = orchestrator_api_view_gm_system_summary(&summary);
    if (!root) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    return web_ui_send_json(req, root);
}

esp_err_t gm_versions_handler(httpd_req_t *req)
{
    scenehub_state_versions_t versions = {0};
    cJSON *root = cJSON_CreateObject();
    esp_err_t err = scenehub_state_get_versions(&versions);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "versions unavailable");
    }
    if (!root) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    cJSON_AddNumberToObject(root, "rooms", versions.rooms);
    cJSON_AddNumberToObject(root, "devices", versions.devices);
    cJSON_AddNumberToObject(root, "scenarios", versions.scenarios);
    cJSON_AddNumberToObject(root, "profiles", versions.profiles);
    cJSON_AddNumberToObject(root, "ingest", versions.ingest);
    cJSON_AddNumberToObject(root, "session", versions.session);
    cJSON_AddNumberToObject(root, "static", versions.static_generation);
    cJSON_AddNumberToObject(root, "runtime", versions.runtime_generation);
    return web_ui_send_json(req, root);
}
