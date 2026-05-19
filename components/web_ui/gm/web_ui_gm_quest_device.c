#include "web_ui_handlers.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "orch_device_view.h"
#include "orchestrator_registry.h"
#include "quest_device.h"
#include "room_scenario.h"
#include "scenehub_control.h"
#include "gm/web_ui_gm_quest_device_json.h"
#include "web_ui_utils.h"

#define GM_QUEST_DEVICE_BODY_MAX_BYTES (160 * 1024)
#define GM_QUEST_DEVICE_LIST_MAX (QUEST_DEVICE_MAX_DEVICES + 4)
#define GM_QUEST_DEVICE_ISSUES_MAX 8

static void *gm_qd_alloc(size_t size)
{
    void *ptr = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
        ptr = heap_caps_calloc(1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return ptr;
}

static bool gm_qd_read_query_value(httpd_req_t *req,
                                   const char *key,
                                   char *out,
                                   size_t out_size)
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

static bool gm_qd_query_bool(httpd_req_t *req, const char *key, bool fallback)
{
    char value[16] = {0};
    if (!gm_qd_read_query_value(req, key, value, sizeof(value)) || !value[0]) {
        return fallback;
    }
    return strcmp(value, "1") == 0 ||
           strcmp(value, "true") == 0 ||
           strcmp(value, "yes") == 0;
}

static esp_err_t gm_qd_read_body(httpd_req_t *req,
                                 size_t max_len,
                                 char **out_body,
                                 size_t *out_len)
{
    char *body = NULL;
    size_t received = 0;
    if (!req || !out_body || !out_len) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_body = NULL;
    *out_len = 0;
    if (req->content_len <= 0 || req->content_len > (int)max_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    body = gm_qd_alloc((size_t)req->content_len + 1);
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
    *out_len = received;
    return ESP_OK;
}

static esp_err_t gm_qd_read_json(httpd_req_t *req,
                                 size_t max_len,
                                 cJSON **out_root)
{
    char *body = NULL;
    size_t body_len = 0;
    cJSON *root = NULL;
    esp_err_t err = ESP_OK;
    if (!out_root) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_root = NULL;
    err = gm_qd_read_body(req, max_len, &body, &body_len);
    if (err != ESP_OK) {
        return err;
    }
    root = cJSON_ParseWithLength(body, body_len);
    heap_caps_free(body);
    if (!root) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_root = root;
    return ESP_OK;
}

static esp_err_t gm_qd_send_error(httpd_req_t *req, esp_err_t err)
{
    return web_ui_send_scenehub_control_error(req, err, NULL, "quest device operation failed");
}

static esp_err_t gm_qd_send_control_error(httpd_req_t *req,
                                          esp_err_t call_err,
                                          const scenehub_control_result_t *result)
{
    return web_ui_send_scenehub_control_error(req, call_err, result, "quest device operation failed");
}

static esp_err_t gm_qd_send_store_ok(httpd_req_t *req, const char *operation)
{
    return web_ui_send_store_operation_json(req,
                                            operation,
                                            QUEST_DEVICE_STORAGE_PATH,
                                            quest_device_generation());
}

static esp_err_t gm_qd_add_backend_issues(cJSON *item, const char *device_id)
{
    orch_issue_entry_t issues[GM_QUEST_DEVICE_ISSUES_MAX] = {0};
    size_t issue_count = 0;
    cJSON *array = NULL;
    esp_err_t err = ESP_OK;

    if (!item || !device_id || !device_id[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    array = cJSON_CreateArray();
    if (!array) {
        return ESP_ERR_NO_MEM;
    }
    err = orchestrator_registry_list_device_issues(device_id,
                                                   issues,
                                                   GM_QUEST_DEVICE_ISSUES_MAX,
                                                   &issue_count);
    if (err != ESP_OK) {
        cJSON_Delete(array);
        return err;
    }
    for (size_t i = 0; i < issue_count; ++i) {
        cJSON *issue = cJSON_CreateObject();
        if (!issue) {
            cJSON_Delete(array);
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(issue, "issue_id", issues[i].issue_id);
        cJSON_AddStringToObject(issue, "scope", issues[i].scope_text);
        cJSON_AddStringToObject(issue, "room_id", issues[i].room_id);
        cJSON_AddStringToObject(issue, "device_id", issues[i].device_id);
        cJSON_AddStringToObject(issue, "severity", issues[i].severity_text);
        cJSON_AddStringToObject(issue, "code", issues[i].code);
        cJSON_AddStringToObject(issue, "title", issues[i].title);
        cJSON_AddStringToObject(issue, "details", issues[i].details);
        cJSON_AddBoolToObject(issue, "active", issues[i].active);
        cJSON_AddItemToArray(array, issue);
    }
    cJSON_AddNumberToObject(item, "issue_count", issue_count);
    cJSON_AddItemToObject(item, "issues", array);
    return ESP_OK;
}

static esp_err_t gm_qd_add_backend_presentation(cJSON *item,
                                                const orch_quest_device_catalog_entry_t *device)
{
    orch_device_entry_t live = {0};
    cJSON *issues = NULL;
    esp_err_t err = ESP_OK;

    if (!item || !device) {
        return ESP_ERR_INVALID_ARG;
    }
    if (device->system_device) {
        issues = cJSON_CreateArray();
        if (!issues) {
            return ESP_ERR_NO_MEM;
        }
        cJSON_AddStringToObject(item, "health", "ok");
        cJSON_AddStringToObject(item, "status_text", "system device");
        cJSON_AddStringToObject(item, "connectivity", "online");
        cJSON_AddStringToObject(item, "runtime_state", "idle");
        cJSON_AddNumberToObject(item, "issue_count", 0);
        cJSON_AddItemToObject(item, "issues", issues);
        return ESP_OK;
    }
    err = orchestrator_registry_get_device(device->id, &live);
    if (err != ESP_OK) {
        return err;
    }
    cJSON_AddStringToObject(item, "health", live.health_text);
    cJSON_AddStringToObject(item,
                            "status_text",
                            live.state[0] ? live.state : live.connectivity_text);
    cJSON_AddStringToObject(item, "connectivity", live.connectivity_text);
    cJSON_AddStringToObject(item, "runtime_state", live.runtime_state_text);
    cJSON_AddStringToObject(item, "state_text", live.state);
    return gm_qd_add_backend_issues(item, device->id);
}

esp_err_t gm_quest_devices_handler(httpd_req_t *req)
{
    bool include_system = gm_qd_query_bool(req, "include_system", true);
    orch_quest_device_catalog_entry_t *devices = NULL;
    size_t count = 0;
    cJSON *root = NULL;
    cJSON *items = NULL;
    esp_err_t err = ESP_OK;

    devices = gm_qd_alloc(sizeof(*devices) * GM_QUEST_DEVICE_LIST_MAX);
    if (!devices) {
        return gm_qd_send_error(req, ESP_ERR_NO_MEM);
    }
    err = orchestrator_registry_list_quest_device_catalog(devices,
                                                          GM_QUEST_DEVICE_LIST_MAX,
                                                          &count,
                                                          include_system);
    if (err != ESP_OK) {
        heap_caps_free(devices);
        return gm_qd_send_error(req, err);
    }

    root = cJSON_CreateObject();
    items = cJSON_CreateArray();
    if (!root || !items) {
        cJSON_Delete(root);
        cJSON_Delete(items);
        heap_caps_free(devices);
        return gm_qd_send_error(req, ESP_ERR_NO_MEM);
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddNumberToObject(root, "generation", quest_device_generation());
    cJSON_AddBoolToObject(root, "include_system", include_system);
    for (size_t i = 0; i < count; ++i) {
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            cJSON_Delete(root);
            cJSON_Delete(items);
            heap_caps_free(devices);
            return gm_qd_send_error(req, ESP_ERR_NO_MEM);
        }
        err = gm_quest_device_catalog_entry_to_json(&devices[i], item);
        if (err != ESP_OK) {
            cJSON_Delete(item);
            cJSON_Delete(root);
            cJSON_Delete(items);
            heap_caps_free(devices);
            return gm_qd_send_error(req, err);
        }
        err = gm_qd_add_backend_presentation(item, &devices[i]);
        if (err != ESP_OK) {
            cJSON_Delete(item);
            cJSON_Delete(root);
            cJSON_Delete(items);
            heap_caps_free(devices);
            return gm_qd_send_error(req, err);
        }
        cJSON_AddItemToArray(items, item);
    }
    cJSON_AddNumberToObject(root, "device_count", count);
    cJSON_AddItemToObject(root, "devices", items);
    heap_caps_free(devices);
    return web_ui_send_json(req, root);
}

esp_err_t gm_quest_device_save_handler(httpd_req_t *req)
{
    cJSON *root = NULL;
    cJSON *device_json = NULL;
    esp_err_t err = gm_qd_read_json(req, 8192, &root);
    if (err != ESP_OK) {
        return gm_qd_send_error(req, err);
    }
    scenehub_control_result_t result = {0};
    err = scenehub_control_save_device_payload("http", root, &device_json, &result);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        cJSON_Delete(root);
        cJSON_Delete(device_json);
        return gm_qd_send_control_error(req, err, &result);
    }
    if (!device_json) {
        cJSON_Delete(root);
        return gm_qd_send_error(req, ESP_ERR_NO_MEM);
    }
    cJSON_Delete(root);
    return web_ui_send_generation_item_json(req,
                                            quest_device_generation(),
                                            "device",
                                            device_json);
}

esp_err_t gm_quest_device_delete_handler(httpd_req_t *req)
{
    cJSON *root = NULL;
    char device_id[QUEST_DEVICE_ID_MAX_LEN] = {0};
    const cJSON *device_id_item = NULL;
    esp_err_t err = gm_qd_read_json(req, 512, &root);
    if (err != ESP_OK) {
        return gm_qd_send_error(req, err);
    }
    device_id_item = cJSON_GetObjectItemCaseSensitive(root, "device_id");
    if (!cJSON_IsString(device_id_item) || !device_id_item->valuestring ||
        !device_id_item->valuestring[0]) {
        cJSON_Delete(root);
        return gm_qd_send_error(req, ESP_ERR_INVALID_ARG);
    }
    snprintf(device_id, sizeof(device_id), "%s", device_id_item->valuestring);
    scenehub_control_result_t result = {0};
    err = scenehub_control_delete_device("http", device_id, &result);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        cJSON_Delete(root);
        return gm_qd_send_control_error(req, err, &result);
    }
    cJSON_Delete(root);
    return web_ui_send_deleted_result_json(req,
                                           "deleted_device_id",
                                           device_id,
                                           quest_device_generation());
}

static esp_err_t gm_qd_params_to_json_string(const cJSON *root, char *out, size_t out_size)
{
    const cJSON *params = NULL;
    char *printed = NULL;
    if (!out || out_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    out[0] = '\0';
    if (!cJSON_IsObject(root)) {
        return ESP_ERR_INVALID_ARG;
    }
    params = cJSON_GetObjectItemCaseSensitive(root, "params");
    if (!params || cJSON_IsNull(params)) {
        return ESP_OK;
    }
    if (!cJSON_IsObject(params)) {
        return ESP_ERR_INVALID_ARG;
    }
    printed = cJSON_PrintUnformatted(params);
    if (!printed) {
        return ESP_ERR_NO_MEM;
    }
    if (strlen(printed) >= out_size) {
        cJSON_free(printed);
        return ESP_ERR_INVALID_SIZE;
    }
    snprintf(out, out_size, "%s", printed);
    cJSON_free(printed);
    return ESP_OK;
}

esp_err_t gm_quest_device_command_run_handler(httpd_req_t *req)
{
    cJSON *root = NULL;
    const cJSON *device_id_item = NULL;
    const cJSON *command_id_item = NULL;
    const char *device_id = NULL;
    const char *command_id = NULL;
    char device_id_buf[QUEST_DEVICE_ID_MAX_LEN] = {0};
    char command_id_buf[QUEST_DEVICE_COMMAND_ID_MAX_LEN] = {0};
    char params_json[ROOM_SCENARIO_COMMAND_PARAMS_JSON_MAX_LEN] = {0};
    scenehub_control_device_command_info_t info = {0};
    scenehub_control_result_t result = {0};
    esp_err_t err = gm_qd_read_json(req, 2048, &root);
    if (err != ESP_OK) {
        return gm_qd_send_error(req, err);
    }
    device_id_item = cJSON_GetObjectItemCaseSensitive(root, "device_id");
    command_id_item = cJSON_GetObjectItemCaseSensitive(root, "command_id");
    device_id = cJSON_IsString(device_id_item) ? device_id_item->valuestring : NULL;
    command_id = cJSON_IsString(command_id_item) ? command_id_item->valuestring : NULL;
    if (!device_id || !device_id[0] || !command_id || !command_id[0]) {
        cJSON_Delete(root);
        return gm_qd_send_error(req, ESP_ERR_INVALID_ARG);
    }
    snprintf(device_id_buf, sizeof(device_id_buf), "%s", device_id);
    snprintf(command_id_buf, sizeof(command_id_buf), "%s", command_id);
    err = gm_qd_params_to_json_string(root, params_json, sizeof(params_json));
    if (err != ESP_OK) {
        cJSON_Delete(root);
        return gm_qd_send_error(req, err);
    }
    err = scenehub_control_device_command_run("http",
                                              device_id_buf,
                                              command_id_buf,
                                              params_json,
                                              &info,
                                              &result);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        cJSON_Delete(root);
        return gm_qd_send_control_error(req, err, &result);
    }

    cJSON_Delete(root);
    return web_ui_send_device_command_result_json(req,
                                                  device_id_buf,
                                                  info.device_name,
                                                  command_id_buf,
                                                  info.command_label);
}

esp_err_t gm_quest_devices_export_handler(httpd_req_t *req)
{
    cJSON *root = NULL;
    esp_err_t err = quest_device_export_json(&root);
    if (err != ESP_OK) {
        return gm_qd_send_error(req, err);
    }
    httpd_resp_set_hdr(req,
                       "Content-Disposition",
                       "attachment; filename=\"quest_devices.json\"");
    return web_ui_send_json(req, root);
}

esp_err_t gm_quest_devices_import_handler(httpd_req_t *req)
{
    cJSON *root = NULL;
    const cJSON *items = NULL;
    int device_count = 0;
    esp_err_t err = gm_qd_read_json(req, GM_QUEST_DEVICE_BODY_MAX_BYTES, &root);
    if (err != ESP_OK) {
        return gm_qd_send_error(req, err);
    }
    items = cJSON_GetObjectItemCaseSensitive(root, "quest_devices");
    if (cJSON_IsArray(items)) {
        device_count = cJSON_GetArraySize(items);
    }
    scenehub_control_result_t result = {0};
    err = scenehub_control_import_devices("http", root, &result);
    cJSON_Delete(root);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        return gm_qd_send_control_error(req, err, &result);
    }
    return web_ui_send_import_result_json(req,
                                          "import",
                                          "device_count",
                                          device_count,
                                          quest_device_generation());
}

esp_err_t gm_quest_devices_save_handler(httpd_req_t *req)
{
    scenehub_control_result_t result = {0};
    esp_err_t err = scenehub_control_save_devices_store("http", &result);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        return gm_qd_send_control_error(req, err, &result);
    }
    return gm_qd_send_store_ok(req, "save");
}

esp_err_t gm_quest_devices_load_handler(httpd_req_t *req)
{
    scenehub_control_result_t result = {0};
    esp_err_t err = scenehub_control_load_devices("http", &result);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        return gm_qd_send_control_error(req, err, &result);
    }
    return gm_qd_send_store_ok(req, "load");
}
