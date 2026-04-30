#include "web_ui_handlers.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "gm_api.h"
#include "orchestrator_registry.h"
#include "orchestrator_timeline.h"
#include "quest_device.h"
#include "room_scenario.h"
#include "web_ui_utils.h"

#define GM_QUEST_DEVICE_BODY_MAX_BYTES (160 * 1024)

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

static const cJSON *gm_qd_payload_device(const cJSON *root)
{
    const cJSON *device = cJSON_GetObjectItemCaseSensitive(root, "device");
    if (cJSON_IsObject(device)) {
        return device;
    }
    return root;
}

static esp_err_t gm_qd_send_error(httpd_req_t *req, esp_err_t err)
{
    if (err == ESP_ERR_INVALID_ARG || err == ESP_ERR_INVALID_SIZE ||
        err == ESP_ERR_NOT_SUPPORTED) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid quest device request");
    }
    if (err == ESP_ERR_NOT_FOUND) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "quest device not found");
    }
    if (err == ESP_ERR_INVALID_STATE) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_send(req, "quest device command not available", HTTPD_RESP_USE_STRLEN);
    }
    if (err == ESP_ERR_NO_MEM) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "quest device operation failed");
}

static esp_err_t gm_qd_send_store_ok(httpd_req_t *req, const char *operation)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return gm_qd_send_error(req, ESP_ERR_NO_MEM);
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "operation", operation ? operation : "");
    cJSON_AddStringToObject(root, "path", QUEST_DEVICE_STORAGE_PATH);
    cJSON_AddNumberToObject(root, "generation", quest_device_generation());
    return web_ui_send_json(req, root);
}

esp_err_t gm_quest_devices_handler(httpd_req_t *req)
{
    bool include_system = gm_qd_query_bool(req, "include_system", true);
    quest_device_t *devices = NULL;
    size_t count = 0;
    size_t emitted = 0;
    cJSON *root = NULL;
    cJSON *items = NULL;
    esp_err_t err = ESP_OK;

    devices = gm_qd_alloc(sizeof(*devices) * QUEST_DEVICE_MAX_DEVICES);
    if (!devices) {
        return gm_qd_send_error(req, ESP_ERR_NO_MEM);
    }
    err = quest_device_list(devices, QUEST_DEVICE_MAX_DEVICES, &count, false);
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
        err = quest_device_to_json(&devices[i], item);
        if (err != ESP_OK) {
            cJSON_Delete(item);
            cJSON_Delete(root);
            cJSON_Delete(items);
            heap_caps_free(devices);
            return gm_qd_send_error(req, err);
        }
        cJSON_AddItemToArray(items, item);
        emitted++;
    }
    if (include_system) {
        quest_device_t *system_audio = gm_qd_alloc(sizeof(*system_audio));
        cJSON *item = NULL;
        if (!system_audio) {
            cJSON_Delete(root);
            cJSON_Delete(items);
            heap_caps_free(devices);
            return gm_qd_send_error(req, ESP_ERR_NO_MEM);
        }
        err = quest_device_get(QUEST_DEVICE_SYSTEM_AUDIO_ID, system_audio);
        if (err != ESP_OK) {
            cJSON_Delete(root);
            cJSON_Delete(items);
            heap_caps_free(system_audio);
            heap_caps_free(devices);
            return gm_qd_send_error(req, err);
        }
        item = cJSON_CreateObject();
        if (!item) {
            cJSON_Delete(root);
            cJSON_Delete(items);
            heap_caps_free(system_audio);
            heap_caps_free(devices);
            return gm_qd_send_error(req, ESP_ERR_NO_MEM);
        }
        err = quest_device_to_json(system_audio, item);
        heap_caps_free(system_audio);
        if (err != ESP_OK) {
            cJSON_Delete(item);
            cJSON_Delete(root);
            cJSON_Delete(items);
            heap_caps_free(devices);
            return gm_qd_send_error(req, err);
        }
        cJSON_AddItemToArray(items, item);
        emitted++;
    }
    cJSON_AddNumberToObject(root, "device_count", emitted);
    cJSON_AddItemToObject(root, "devices", items);
    heap_caps_free(devices);
    return web_ui_send_json(req, root);
}

esp_err_t gm_quest_device_save_handler(httpd_req_t *req)
{
    cJSON *root = NULL;
    cJSON *out = NULL;
    cJSON *device_json = NULL;
    quest_device_t device = {0};
    esp_err_t err = gm_qd_read_json(req, 8192, &root);
    if (err != ESP_OK) {
        return gm_qd_send_error(req, err);
    }
    err = quest_device_from_json(gm_qd_payload_device(root), &device);
    if (err == ESP_OK) {
        err = quest_device_upsert_and_save(&device);
    }
    if (err != ESP_OK) {
        cJSON_Delete(root);
        return gm_qd_send_error(req, err);
    }
    out = cJSON_CreateObject();
    device_json = cJSON_CreateObject();
    if (!out || !device_json) {
        cJSON_Delete(root);
        cJSON_Delete(out);
        cJSON_Delete(device_json);
        return gm_qd_send_error(req, ESP_ERR_NO_MEM);
    }
    cJSON_AddBoolToObject(out, "ok", true);
    cJSON_AddNumberToObject(out, "generation", quest_device_generation());
    err = quest_device_to_json(&device, device_json);
    if (err != ESP_OK) {
        cJSON_Delete(root);
        cJSON_Delete(out);
        cJSON_Delete(device_json);
        return gm_qd_send_error(req, err);
    }
    cJSON_AddItemToObject(out, "device", device_json);
    cJSON_Delete(root);
    return web_ui_send_json(req, out);
}

esp_err_t gm_quest_device_delete_handler(httpd_req_t *req)
{
    cJSON *root = NULL;
    cJSON *out = NULL;
    const cJSON *device_id_item = NULL;
    const char *device_id = NULL;
    esp_err_t err = gm_qd_read_json(req, 512, &root);
    if (err != ESP_OK) {
        return gm_qd_send_error(req, err);
    }
    device_id_item = cJSON_GetObjectItemCaseSensitive(root, "device_id");
    device_id = cJSON_IsString(device_id_item) ? device_id_item->valuestring : NULL;
    if (!device_id || !device_id[0]) {
        cJSON_Delete(root);
        return gm_qd_send_error(req, ESP_ERR_INVALID_ARG);
    }
    err = quest_device_delete_and_save(device_id);
    if (err != ESP_OK) {
        cJSON_Delete(root);
        return gm_qd_send_error(req, err);
    }
    out = cJSON_CreateObject();
    if (!out) {
        cJSON_Delete(root);
        return gm_qd_send_error(req, ESP_ERR_NO_MEM);
    }
    cJSON_AddBoolToObject(out, "ok", true);
    cJSON_AddStringToObject(out, "deleted_device_id", device_id);
    cJSON_AddNumberToObject(out, "generation", quest_device_generation());
    cJSON_Delete(root);
    return web_ui_send_json(req, out);
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
    cJSON *out = NULL;
    const cJSON *device_id_item = NULL;
    const cJSON *command_id_item = NULL;
    const char *device_id = NULL;
    const char *command_id = NULL;
    char params_json[ROOM_SCENARIO_COMMAND_PARAMS_JSON_MAX_LEN] = {0};
    quest_device_t *device = NULL;
    quest_device_command_t command = {0};
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
    err = gm_qd_params_to_json_string(root, params_json, sizeof(params_json));
    if (err != ESP_OK) {
        cJSON_Delete(root);
        return gm_qd_send_error(req, err);
    }

    device = gm_qd_alloc(sizeof(*device));
    if (!device) {
        cJSON_Delete(root);
        return gm_qd_send_error(req, ESP_ERR_NO_MEM);
    }
    err = quest_device_get(device_id, device);
    if (err == ESP_OK && !device->enabled) {
        err = ESP_ERR_INVALID_STATE;
    }
    if (err == ESP_OK) {
        err = quest_device_get_command(device_id, command_id, &command);
    }
    if (err == ESP_OK && !command.button_enabled) {
        err = ESP_ERR_INVALID_STATE;
    }
    if (err == ESP_OK) {
        err = gm_api_device_command_run(device_id, command_id, params_json);
    }
    if (err != ESP_OK) {
        heap_caps_free(device);
        cJSON_Delete(root);
        return gm_qd_send_error(req, err);
    }

    orchestrator_registry_invalidate();
    (void)orchestrator_timeline_log(ORCH_TIMELINE_TYPE_DEVICE_ACTION,
                                    command.dangerous ? ORCH_TIMELINE_SEVERITY_WARNING
                                                      : ORCH_TIMELINE_SEVERITY_INFO,
                                    "http",
                                    "",
                                    device_id,
                                    "Quest device command",
                                    command_id);

    out = cJSON_CreateObject();
    if (!out) {
        heap_caps_free(device);
        cJSON_Delete(root);
        return gm_qd_send_error(req, ESP_ERR_NO_MEM);
    }
    cJSON_AddBoolToObject(out, "ok", true);
    cJSON_AddStringToObject(out, "device_id", device_id);
    cJSON_AddStringToObject(out, "device_name", device->name);
    cJSON_AddStringToObject(out, "command_id", command_id);
    cJSON_AddStringToObject(out, "command_label", command.label);
    heap_caps_free(device);
    cJSON_Delete(root);
    return web_ui_send_json(req, out);
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
    err = quest_device_import_json_and_save(root);
    cJSON_Delete(root);
    if (err != ESP_OK) {
        return gm_qd_send_error(req, err);
    }
    root = cJSON_CreateObject();
    if (!root) {
        return gm_qd_send_error(req, ESP_ERR_NO_MEM);
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "operation", "import");
    cJSON_AddNumberToObject(root, "device_count", device_count);
    cJSON_AddNumberToObject(root, "generation", quest_device_generation());
    return web_ui_send_json(req, root);
}

esp_err_t gm_quest_devices_save_handler(httpd_req_t *req)
{
    esp_err_t err = quest_device_save();
    if (err != ESP_OK) {
        return gm_qd_send_error(req, err);
    }
    return gm_qd_send_store_ok(req, "save");
}

esp_err_t gm_quest_devices_load_handler(httpd_req_t *req)
{
    esp_err_t err = quest_device_load();
    if (err != ESP_OK) {
        return gm_qd_send_error(req, err);
    }
    return gm_qd_send_store_ok(req, "load");
}
