#include "web_ui_handlers.h"

#include <stddef.h>

#include "cJSON.h"
#include "esp_heap_caps.h"

#include "orchestrator_registry.h"
#include "room_scenario.h"
#include "web_ui_utils.h"

#define GM_ROOM_SCENARIO_IMPORT_MAX_BYTES (256 * 1024)

static void *gm_scenario_store_alloc(size_t size)
{
    void *ptr = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
        ptr = heap_caps_calloc(1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return ptr;
}

static esp_err_t gm_scenario_store_read_body(httpd_req_t *req, char **out_body)
{
    char *body = NULL;
    size_t received = 0;
    if (!req || !out_body) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_body = NULL;
    if (req->content_len <= 0 || req->content_len > 512) {
        return ESP_ERR_INVALID_SIZE;
    }
    body = gm_scenario_store_alloc((size_t)req->content_len + 1);
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

static esp_err_t gm_scenario_store_read_body_limit(httpd_req_t *req,
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
    body = gm_scenario_store_alloc((size_t)req->content_len + 1);
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

static esp_err_t gm_scenario_store_send_error(httpd_req_t *req, esp_err_t err)
{
    if (err == ESP_ERR_INVALID_ARG || err == ESP_ERR_INVALID_SIZE ||
        err == ESP_ERR_INVALID_VERSION) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid room scenarios json");
    }
    if (err == ESP_ERR_NOT_FOUND) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "room scenarios file not found");
    }
    if (err == ESP_ERR_NO_MEM) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "room scenarios operation failed");
}

static esp_err_t gm_scenario_send_delete_error(httpd_req_t *req, esp_err_t err)
{
    if (err == ESP_ERR_INVALID_ARG || err == ESP_ERR_INVALID_SIZE) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "room_id/scenario_id required");
    }
    if (err == ESP_ERR_NOT_FOUND) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "room or scenario not found");
    }
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "scenario select failed");
}

static esp_err_t gm_scenario_store_send_ok(httpd_req_t *req, const char *operation)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "operation", operation ? operation : "");
    cJSON_AddStringToObject(root, "path", ROOM_SCENARIO_STORAGE_PATH);
    cJSON_AddNumberToObject(root, "generation", room_scenario_generation());
    return web_ui_send_json(req, root);
}

static const cJSON *gm_scenario_payload_object(const cJSON *root)
{
    const cJSON *scenario = cJSON_GetObjectItemCaseSensitive(root, "scenario");
    if (cJSON_IsObject(scenario)) {
        return scenario;
    }
    return root;
}

static const char *gm_scenario_validation_level_str(room_scenario_validation_level_t level)
{
    return level == ROOM_SCENARIO_VALIDATION_WARNING ? "warning" : "error";
}

static esp_err_t gm_scenario_add_validation_report(cJSON *root,
                                                   const room_scenario_validation_report_t *report)
{
    cJSON *issues = NULL;
    size_t error_count = 0;
    size_t warning_count = 0;
    if (!root || !report) {
        return ESP_ERR_INVALID_ARG;
    }
    issues = cJSON_CreateArray();
    if (!issues) {
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < report->issue_count; ++i) {
        const room_scenario_validation_issue_t *issue = &report->issues[i];
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            cJSON_Delete(issues);
            return ESP_ERR_NO_MEM;
        }
        if (issue->level == ROOM_SCENARIO_VALIDATION_WARNING) {
            ++warning_count;
        } else {
            ++error_count;
        }
        cJSON_AddStringToObject(item, "level", gm_scenario_validation_level_str(issue->level));
        cJSON_AddNumberToObject(item, "step_index", issue->step_index);
        cJSON_AddStringToObject(item, "code", issue->code);
        cJSON_AddStringToObject(item, "message", issue->message);
        cJSON_AddItemToArray(issues, item);
    }
    cJSON_AddBoolToObject(root, "valid", report->valid);
    cJSON_AddNumberToObject(root, "issue_count", report->issue_count);
    cJSON_AddNumberToObject(root, "error_count", error_count);
    cJSON_AddNumberToObject(root, "warning_count", warning_count);
    cJSON_AddItemToObject(root, "issues", issues);
    return ESP_OK;
}

esp_err_t gm_room_scenario_validate_handler(httpd_req_t *req)
{
    char *body = NULL;
    size_t body_len = 0;
    cJSON *root = NULL;
    cJSON *out = NULL;
    room_scenario_t *scenario = NULL;
    room_scenario_validation_report_t *report = NULL;
    esp_err_t err = gm_scenario_store_read_body_limit(req, 32768, &body, &body_len);
    if (err != ESP_OK) {
        return gm_scenario_store_send_error(req, err);
    }
    scenario = gm_scenario_store_alloc(sizeof(*scenario));
    report = gm_scenario_store_alloc(sizeof(*report));
    if (!scenario || !report) {
        heap_caps_free(scenario);
        heap_caps_free(report);
        heap_caps_free(body);
        return gm_scenario_store_send_error(req, ESP_ERR_NO_MEM);
    }
    root = cJSON_ParseWithLength(body, body_len);
    heap_caps_free(body);
    if (!root) {
        heap_caps_free(scenario);
        heap_caps_free(report);
        return gm_scenario_store_send_error(req, ESP_ERR_INVALID_ARG);
    }
    err = room_scenario_from_json(gm_scenario_payload_object(root), scenario);
    if (err == ESP_OK) {
        err = room_scenario_validate(scenario, report);
    }
    cJSON_Delete(root);
    if (err != ESP_OK) {
        heap_caps_free(scenario);
        heap_caps_free(report);
        return gm_scenario_store_send_error(req, err);
    }
    out = cJSON_CreateObject();
    if (!out) {
        heap_caps_free(scenario);
        heap_caps_free(report);
        return gm_scenario_store_send_error(req, ESP_ERR_NO_MEM);
    }
    cJSON_AddBoolToObject(out, "ok", true);
    cJSON_AddStringToObject(out, "scenario_id", scenario->id);
    err = gm_scenario_add_validation_report(out, report);
    if (err != ESP_OK) {
        cJSON_Delete(out);
        heap_caps_free(scenario);
        heap_caps_free(report);
        return gm_scenario_store_send_error(req, err);
    }
    heap_caps_free(scenario);
    heap_caps_free(report);
    return web_ui_send_json(req, out);
}

esp_err_t gm_room_scenario_save_handler(httpd_req_t *req)
{
    char *body = NULL;
    size_t body_len = 0;
    cJSON *root = NULL;
    cJSON *scenario_json = NULL;
    cJSON *out = NULL;
    room_scenario_t *scenario = NULL;
    esp_err_t err = gm_scenario_store_read_body_limit(req, 32768, &body, &body_len);
    if (err != ESP_OK) {
        return gm_scenario_store_send_error(req, err);
    }
    scenario = gm_scenario_store_alloc(sizeof(*scenario));
    if (!scenario) {
        heap_caps_free(body);
        return gm_scenario_store_send_error(req, ESP_ERR_NO_MEM);
    }
    root = cJSON_ParseWithLength(body, body_len);
    heap_caps_free(body);
    if (!root) {
        heap_caps_free(scenario);
        return gm_scenario_store_send_error(req, ESP_ERR_INVALID_ARG);
    }
    err = room_scenario_from_json(gm_scenario_payload_object(root), scenario);
    if (err == ESP_OK) {
        err = room_scenario_add_and_save(scenario);
    }
    if (err != ESP_OK) {
        cJSON_Delete(root);
        heap_caps_free(scenario);
        return gm_scenario_store_send_error(req, err);
    }
    orchestrator_registry_invalidate();
    out = cJSON_CreateObject();
    scenario_json = cJSON_CreateObject();
    if (!out || !scenario_json) {
        cJSON_Delete(root);
        cJSON_Delete(out);
        cJSON_Delete(scenario_json);
        heap_caps_free(scenario);
        return gm_scenario_store_send_error(req, ESP_ERR_NO_MEM);
    }
    cJSON_AddBoolToObject(out, "ok", true);
    cJSON_AddNumberToObject(out, "generation", room_scenario_generation());
    err = room_scenario_to_json(scenario, scenario_json);
    if (err != ESP_OK) {
        cJSON_Delete(root);
        cJSON_Delete(out);
        cJSON_Delete(scenario_json);
        heap_caps_free(scenario);
        return gm_scenario_store_send_error(req, err);
    }
    cJSON_AddItemToObject(out, "scenario", scenario_json);
    cJSON_Delete(root);
    heap_caps_free(scenario);
    return web_ui_send_json(req, out);
}

esp_err_t gm_room_scenario_delete_handler(httpd_req_t *req)
{
    char *body = NULL;
    cJSON *root = NULL;
    cJSON *out = NULL;
    const cJSON *scenario_id_item = NULL;
    const char *scenario_id = NULL;
    esp_err_t err = gm_scenario_store_read_body(req, &body);
    if (err != ESP_OK) {
        return gm_scenario_send_delete_error(req, err);
    }
    root = cJSON_Parse(body);
    heap_caps_free(body);
    if (!root) {
        return gm_scenario_send_delete_error(req, ESP_ERR_INVALID_ARG);
    }
    scenario_id_item = cJSON_GetObjectItemCaseSensitive(root, "scenario_id");
    scenario_id = cJSON_IsString(scenario_id_item) ? scenario_id_item->valuestring : NULL;
    if (!scenario_id || !scenario_id[0]) {
        cJSON_Delete(root);
        return gm_scenario_send_delete_error(req, ESP_ERR_INVALID_ARG);
    }
    err = room_scenario_delete_and_save(scenario_id);
    if (err != ESP_OK) {
        cJSON_Delete(root);
        return gm_scenario_send_delete_error(req, err);
    }
    orchestrator_registry_invalidate();
    out = cJSON_CreateObject();
    if (!out) {
        cJSON_Delete(root);
        return gm_scenario_send_delete_error(req, ESP_ERR_NO_MEM);
    }
    cJSON_AddBoolToObject(out, "ok", true);
    cJSON_AddStringToObject(out, "deleted_scenario_id", scenario_id);
    cJSON_AddNumberToObject(out, "generation", room_scenario_generation());
    cJSON_Delete(root);
    return web_ui_send_json(req, out);
}

esp_err_t gm_room_scenarios_export_handler(httpd_req_t *req)
{
    cJSON *root = NULL;
    esp_err_t err = room_scenario_store_export_json(&root);
    if (err != ESP_OK) {
        return gm_scenario_store_send_error(req, err);
    }
    httpd_resp_set_hdr(req,
                       "Content-Disposition",
                       "attachment; filename=\"room_scenarios.json\"");
    return web_ui_send_json(req, root);
}

esp_err_t gm_room_scenarios_import_handler(httpd_req_t *req)
{
    char *body = NULL;
    size_t body_len = 0;
    cJSON *root = NULL;
    const cJSON *items = NULL;
    int scenario_count = 0;
    esp_err_t err = gm_scenario_store_read_body_limit(req,
                                                      GM_ROOM_SCENARIO_IMPORT_MAX_BYTES,
                                                      &body,
                                                      &body_len);
    if (err != ESP_OK) {
        return gm_scenario_store_send_error(req, err);
    }
    root = cJSON_ParseWithLength(body, body_len);
    heap_caps_free(body);
    if (!root) {
        return gm_scenario_store_send_error(req, ESP_ERR_INVALID_ARG);
    }
    items = cJSON_GetObjectItemCaseSensitive(root, "room_scenarios");
    if (cJSON_IsArray(items)) {
        scenario_count = cJSON_GetArraySize(items);
    }
    err = room_scenario_store_import_json_and_save(root);
    cJSON_Delete(root);
    if (err != ESP_OK) {
        return gm_scenario_store_send_error(req, err);
    }
    orchestrator_registry_invalidate();

    root = cJSON_CreateObject();
    if (!root) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "operation", "import");
    cJSON_AddNumberToObject(root, "scenario_count", scenario_count);
    cJSON_AddNumberToObject(root, "generation", room_scenario_generation());
    return web_ui_send_json(req, root);
}

esp_err_t gm_room_scenarios_save_handler(httpd_req_t *req)
{
    esp_err_t err = room_scenario_store_save();
    if (err != ESP_OK) {
        return gm_scenario_store_send_error(req, err);
    }
    return gm_scenario_store_send_ok(req, "save");
}

esp_err_t gm_room_scenarios_load_handler(httpd_req_t *req)
{
    esp_err_t err = room_scenario_store_load();
    if (err != ESP_OK) {
        return gm_scenario_store_send_error(req, err);
    }
    orchestrator_registry_invalidate();
    return gm_scenario_store_send_ok(req, "load");
}
