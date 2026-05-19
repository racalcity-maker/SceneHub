#include "web_ui_handlers.h"

#include <stddef.h>
#include <string.h>

#include "cJSON.h"
#include "esp_heap_caps.h"

#include "room_scenario.h"
#include "scenehub_control.h"
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
    return web_ui_send_scenehub_control_error(req, err, NULL, "room scenarios operation failed");
}

static esp_err_t gm_scenario_send_delete_error(httpd_req_t *req, esp_err_t err)
{
    return web_ui_send_scenehub_control_error(req, err, NULL, "scenario select failed");
}

static esp_err_t gm_scenario_store_send_control_error(httpd_req_t *req,
                                                      esp_err_t call_err,
                                                      const scenehub_control_result_t *result,
                                                      const char *fallback_message)
{
    return web_ui_send_scenehub_control_error(req, call_err, result, fallback_message);
}

static esp_err_t gm_scenario_store_send_ok(httpd_req_t *req, const char *operation)
{
    return web_ui_send_store_operation_json(req,
                                            operation,
                                            ROOM_SCENARIO_STORAGE_PATH,
                                            room_scenario_generation());
}

esp_err_t gm_room_scenario_validate_handler(httpd_req_t *req)
{
    char *body = NULL;
    size_t body_len = 0;
    cJSON *root = NULL;
    char scenario_id[ROOM_SCENARIO_ID_MAX_LEN] = {0};
    room_scenario_t *scenario = NULL;
    room_scenario_validation_report_t *report = NULL;
    esp_err_t err = gm_scenario_store_read_body_limit(req, 32768, &body, &body_len);
    if (err != ESP_OK) {
        return gm_scenario_store_send_error(req, err);
    }
    err = room_scenario_acquire_scratch(&scenario, &report);
    if (err != ESP_OK) {
        heap_caps_free(body);
        return gm_scenario_store_send_error(req, err);
    }
    memset(report, 0, sizeof(*report));
    root = cJSON_ParseWithLength(body, body_len);
    heap_caps_free(body);
    if (!root) {
        room_scenario_release_scratch();
        return gm_scenario_store_send_error(req, ESP_ERR_INVALID_ARG);
    }
    scenehub_control_result_t result = {0};
    err = scenehub_control_validate_scenario_payload_into("http",
                                                          root,
                                                          scenario,
                                                          scenario_id,
                                                          sizeof(scenario_id),
                                                          report,
                                                          &result);
    cJSON_Delete(root);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        room_scenario_release_scratch();
        return gm_scenario_store_send_control_error(req, err, &result, "room scenarios operation failed");
    }
    err = web_ui_send_scenario_validation_result_json(req,
                                                      scenario_id,
                                                      report);
    room_scenario_release_scratch();
    return err;
}

esp_err_t gm_room_scenario_save_handler(httpd_req_t *req)
{
    char *body = NULL;
    size_t body_len = 0;
    cJSON *root = NULL;
    cJSON *scenario_json = NULL;
    esp_err_t err = gm_scenario_store_read_body_limit(req, 32768, &body, &body_len);
    if (err != ESP_OK) {
        return gm_scenario_store_send_error(req, err);
    }
    root = cJSON_ParseWithLength(body, body_len);
    heap_caps_free(body);
    if (!root) {
        return gm_scenario_store_send_error(req, ESP_ERR_INVALID_ARG);
    }
    scenehub_control_result_t result = {0};
    err = scenehub_control_save_scenario_payload("http", root, &scenario_json, &result);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        cJSON_Delete(root);
        cJSON_Delete(scenario_json);
        return gm_scenario_store_send_control_error(req, err, &result, "room scenarios operation failed");
    }
    if (!scenario_json) {
        cJSON_Delete(root);
        return gm_scenario_store_send_error(req, ESP_ERR_NO_MEM);
    }
    cJSON_Delete(root);
    err = web_ui_send_generation_item_json(req,
                                           room_scenario_generation(),
                                           "scenario",
                                           scenario_json);
    return err;
}

esp_err_t gm_room_scenario_delete_handler(httpd_req_t *req)
{
    char *body = NULL;
    char scenario_id[ROOM_SCENARIO_ID_MAX_LEN] = {0};
    cJSON *root = NULL;
    const cJSON *scenario_id_item = NULL;
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
    if (!cJSON_IsString(scenario_id_item) || !scenario_id_item->valuestring ||
        !scenario_id_item->valuestring[0]) {
        cJSON_Delete(root);
        return gm_scenario_send_delete_error(req, ESP_ERR_INVALID_ARG);
    }
    snprintf(scenario_id, sizeof(scenario_id), "%s", scenario_id_item->valuestring);
    scenehub_control_result_t result = {0};
    err = scenehub_control_delete_scenario("http", scenario_id, &result);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        cJSON_Delete(root);
        return gm_scenario_store_send_control_error(req, err, &result, "scenario select failed");
    }
    cJSON_Delete(root);
    return web_ui_send_deleted_result_json(req,
                                           "deleted_scenario_id",
                                           scenario_id,
                                           room_scenario_generation());
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
    scenehub_control_result_t result = {0};
    err = scenehub_control_import_scenarios("http", root, &result);
    cJSON_Delete(root);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        return gm_scenario_store_send_control_error(req, err, &result, "room scenarios operation failed");
    }
    return web_ui_send_import_result_json(req,
                                          "import",
                                          "scenario_count",
                                          scenario_count,
                                          room_scenario_generation());
}

esp_err_t gm_room_scenarios_save_handler(httpd_req_t *req)
{
    scenehub_control_result_t result = {0};
    esp_err_t err = scenehub_control_save_scenarios_store("http", &result);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        return gm_scenario_store_send_control_error(req, err, &result, "room scenarios operation failed");
    }
    return gm_scenario_store_send_ok(req, "save");
}

esp_err_t gm_room_scenarios_load_handler(httpd_req_t *req)
{
    scenehub_control_result_t result = {0};
    esp_err_t err = scenehub_control_load_scenarios("http", &result);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        return gm_scenario_store_send_control_error(req, err, &result, "room scenarios operation failed");
    }
    return gm_scenario_store_send_ok(req, "load");
}
