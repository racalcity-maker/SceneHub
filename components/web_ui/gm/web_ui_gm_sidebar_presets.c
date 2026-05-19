#include "web_ui_handlers.h"

#include <stddef.h>

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "gm_sidebar_presets.h"
#include "scenehub_control.h"
#include "web_ui_utils.h"

#define GM_SIDEBAR_PRESET_IMPORT_MAX_BYTES (64 * 1024)
static bool s_sidebar_presets_loaded = false;

static void *gm_sidebar_alloc(size_t size)
{
    void *ptr = heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
        ptr = heap_caps_calloc(1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return ptr;
}

static esp_err_t gm_sidebar_read_body(httpd_req_t *req,
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
    body = gm_sidebar_alloc((size_t)req->content_len + 1);
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

static esp_err_t gm_sidebar_read_json(httpd_req_t *req,
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
    err = gm_sidebar_read_body(req, max_len, &body, &body_len);
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

static esp_err_t gm_sidebar_send_error(httpd_req_t *req, esp_err_t err)
{
    return web_ui_send_scenehub_control_error(req, err, NULL, "sidebar presets operation failed");
}

static esp_err_t gm_sidebar_send_control_error(httpd_req_t *req,
                                               esp_err_t call_err,
                                               const scenehub_control_result_t *result)
{
    return web_ui_send_scenehub_control_error(req,
                                              call_err,
                                              result,
                                              "sidebar presets operation failed");
}

static esp_err_t gm_sidebar_ensure_loaded(void)
{
    esp_err_t err = gm_sidebar_presets_init();
    if (err != ESP_OK) {
        return err;
    }
    if (s_sidebar_presets_loaded) {
        return ESP_OK;
    }
    err = gm_sidebar_preset_load();
    if (err == ESP_ERR_NOT_FOUND) {
        s_sidebar_presets_loaded = true;
        return ESP_OK;
    }
    if (err == ESP_OK) {
        s_sidebar_presets_loaded = true;
    }
    return err;
}

static esp_err_t gm_sidebar_send_list_json(httpd_req_t *req)
{
    cJSON *root = NULL;
    cJSON *items = NULL;
    int count = 0;
    esp_err_t err = gm_sidebar_ensure_loaded();
    if (err != ESP_OK) {
        return gm_sidebar_send_error(req, err);
    }
    err = gm_sidebar_preset_export_json(&root);
    if (err != ESP_OK) {
        return gm_sidebar_send_error(req, err);
    }
    items = cJSON_GetObjectItemCaseSensitive(root, "gm_sidebar_presets");
    if (cJSON_IsArray(items)) {
        count = cJSON_GetArraySize(items);
    }
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "path", GM_SIDEBAR_PRESET_STORAGE_PATH);
    cJSON_AddNumberToObject(root, "preset_count", count);
    cJSON_AddNumberToObject(root, "generation", gm_sidebar_preset_generation());
    return web_ui_send_json(req, root);
}

esp_err_t gm_sidebar_presets_handler(httpd_req_t *req)
{
    return gm_sidebar_send_list_json(req);
}

esp_err_t gm_sidebar_presets_save_handler(httpd_req_t *req)
{
    cJSON *root = NULL;
    scenehub_control_result_t result = {0};
    esp_err_t err = gm_sidebar_read_json(req, 32768, &root);
    if (err != ESP_OK) {
        return gm_sidebar_send_error(req, err);
    }
    err = scenehub_control_save_sidebar_presets_payload("http", root, &result);
    cJSON_Delete(root);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        return gm_sidebar_send_control_error(req, err, &result);
    }
    return gm_sidebar_send_list_json(req);
}

esp_err_t gm_sidebar_presets_load_handler(httpd_req_t *req)
{
    scenehub_control_result_t result = {0};
    esp_err_t err = scenehub_control_load_sidebar_presets("http", &result);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        return gm_sidebar_send_control_error(req, err, &result);
    }
    s_sidebar_presets_loaded = true;
    return gm_sidebar_send_list_json(req);
}

esp_err_t gm_sidebar_presets_export_handler(httpd_req_t *req)
{
    cJSON *root = NULL;
    esp_err_t err = gm_sidebar_ensure_loaded();
    if (err != ESP_OK) {
        return gm_sidebar_send_error(req, err);
    }
    err = gm_sidebar_preset_export_json(&root);
    if (err != ESP_OK) {
        return gm_sidebar_send_error(req, err);
    }
    httpd_resp_set_hdr(req,
                       "Content-Disposition",
                       "attachment; filename=\"gm_sidebar_presets.json\"");
    return web_ui_send_json(req, root);
}

esp_err_t gm_sidebar_presets_import_handler(httpd_req_t *req)
{
    cJSON *root = NULL;
    const cJSON *items = NULL;
    int preset_count = 0;
    scenehub_control_result_t result = {0};
    esp_err_t err = gm_sidebar_read_json(req, GM_SIDEBAR_PRESET_IMPORT_MAX_BYTES, &root);
    if (err != ESP_OK) {
        return gm_sidebar_send_error(req, err);
    }
    items = cJSON_GetObjectItemCaseSensitive(root, "gm_sidebar_presets");
    if (!cJSON_IsArray(items)) {
        items = cJSON_GetObjectItemCaseSensitive(root, "presets");
    }
    if (cJSON_IsArray(items)) {
        preset_count = cJSON_GetArraySize(items);
    }
    err = scenehub_control_import_sidebar_presets("http", root, &result);
    cJSON_Delete(root);
    if (!web_ui_scenehub_control_is_done(err, &result)) {
        return gm_sidebar_send_control_error(req, err, &result);
    }
    s_sidebar_presets_loaded = true;
    return web_ui_send_import_result_json(req,
                                          "import",
                                          "preset_count",
                                          preset_count,
                                          gm_sidebar_preset_generation());
}
