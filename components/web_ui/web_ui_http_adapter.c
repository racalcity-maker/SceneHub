#define WEB_UI_UTILS_NO_HTTP_MACROS
#include "web_ui_utils.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "web_ui_http";
#define WEB_UI_JSON_CHUNK_SIZE 2048

static esp_err_t default_resp_send(httpd_req_t *req, const char *body, ssize_t body_len)
{
    return httpd_resp_send(req, body, body_len);
}

static esp_err_t default_resp_send_chunk(httpd_req_t *req, const char *body, ssize_t body_len)
{
    return httpd_resp_send_chunk(req, body, body_len);
}

static esp_err_t default_resp_send_err(httpd_req_t *req, httpd_err_code_t error, const char *message)
{
    return httpd_resp_send_err(req, error, message);
}

static esp_err_t default_resp_set_status(httpd_req_t *req, const char *status)
{
    return httpd_resp_set_status(req, status);
}

static esp_err_t default_resp_set_type(httpd_req_t *req, const char *type)
{
    return httpd_resp_set_type(req, type);
}

static esp_err_t default_resp_set_hdr(httpd_req_t *req, const char *field, const char *value)
{
    return httpd_resp_set_hdr(req, field, value);
}

static int default_req_recv(httpd_req_t *req, char *buf, size_t buf_len)
{
    return httpd_req_recv(req, buf, buf_len);
}

static esp_err_t default_req_get_url_query_str(httpd_req_t *req, char *buf, size_t buf_len)
{
    return httpd_req_get_url_query_str(req, buf, buf_len);
}

static esp_err_t default_query_key_value(const char *query, const char *key, char *val, size_t val_size)
{
    return httpd_query_key_value(query, key, val, val_size);
}

static size_t default_req_get_hdr_value_len(httpd_req_t *req, const char *field)
{
    return httpd_req_get_hdr_value_len(req, field);
}

static esp_err_t default_req_get_hdr_value_str(httpd_req_t *req, const char *field, char *val, size_t val_size)
{
    return httpd_req_get_hdr_value_str(req, field, val, val_size);
}

static const web_ui_http_adapter_t s_default_http_adapter = {
    .resp_send = default_resp_send,
    .resp_send_chunk = default_resp_send_chunk,
    .resp_send_err = default_resp_send_err,
    .resp_set_status = default_resp_set_status,
    .resp_set_type = default_resp_set_type,
    .resp_set_hdr = default_resp_set_hdr,
    .req_recv = default_req_recv,
    .req_get_url_query_str = default_req_get_url_query_str,
    .query_key_value = default_query_key_value,
    .req_get_hdr_value_len = default_req_get_hdr_value_len,
    .req_get_hdr_value_str = default_req_get_hdr_value_str,
};

static const web_ui_http_adapter_t *s_http_adapter = &s_default_http_adapter;

static const web_ui_http_adapter_t *web_ui_http_adapter_or_default(void)
{
    return s_http_adapter ? s_http_adapter : &s_default_http_adapter;
}

esp_err_t web_ui_http_resp_send(httpd_req_t *req, const char *body, ssize_t body_len)
{
    const web_ui_http_adapter_t *adapter = web_ui_http_adapter_or_default();
    return (adapter->resp_send ? adapter->resp_send : s_default_http_adapter.resp_send)(req, body, body_len);
}

esp_err_t web_ui_http_resp_send_chunk(httpd_req_t *req, const char *body, ssize_t body_len)
{
    const web_ui_http_adapter_t *adapter = web_ui_http_adapter_or_default();
    if (adapter->resp_send_chunk) {
        return adapter->resp_send_chunk(req, body, body_len);
    }
    if (!body) {
        return ESP_OK;
    }
    return (adapter->resp_send ? adapter->resp_send : s_default_http_adapter.resp_send)(req, body, body_len);
}

esp_err_t web_ui_http_resp_send_err(httpd_req_t *req, httpd_err_code_t error, const char *message)
{
    const web_ui_http_adapter_t *adapter = web_ui_http_adapter_or_default();
    const char *uri = (req && req->uri[0]) ? req->uri : "?";
    ESP_LOGW(TAG,
             "HTTP error uri=%s status=%d msg=%s free_int=%u largest_int=%u free_psram=%u",
             uri,
             (int)error,
             message ? message : "",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return (adapter->resp_send_err ? adapter->resp_send_err : s_default_http_adapter.resp_send_err)(
        req, error, message);
}

esp_err_t web_ui_http_resp_set_status(httpd_req_t *req, const char *status)
{
    const web_ui_http_adapter_t *adapter = web_ui_http_adapter_or_default();
    return (adapter->resp_set_status ? adapter->resp_set_status : s_default_http_adapter.resp_set_status)(
        req, status);
}

esp_err_t web_ui_http_resp_set_type(httpd_req_t *req, const char *type)
{
    const web_ui_http_adapter_t *adapter = web_ui_http_adapter_or_default();
    return (adapter->resp_set_type ? adapter->resp_set_type : s_default_http_adapter.resp_set_type)(req, type);
}

esp_err_t web_ui_http_resp_set_hdr(httpd_req_t *req, const char *field, const char *value)
{
    const web_ui_http_adapter_t *adapter = web_ui_http_adapter_or_default();
    return (adapter->resp_set_hdr ? adapter->resp_set_hdr : s_default_http_adapter.resp_set_hdr)(
        req, field, value);
}

int web_ui_http_req_recv(httpd_req_t *req, char *buf, size_t buf_len)
{
    const web_ui_http_adapter_t *adapter = web_ui_http_adapter_or_default();
    return (adapter->req_recv ? adapter->req_recv : s_default_http_adapter.req_recv)(req, buf, buf_len);
}

esp_err_t web_ui_http_req_get_url_query_str(httpd_req_t *req, char *buf, size_t buf_len)
{
    const web_ui_http_adapter_t *adapter = web_ui_http_adapter_or_default();
    return (adapter->req_get_url_query_str ? adapter->req_get_url_query_str
                                           : s_default_http_adapter.req_get_url_query_str)(req, buf, buf_len);
}

esp_err_t web_ui_http_query_key_value(const char *query, const char *key, char *val, size_t val_size)
{
    const web_ui_http_adapter_t *adapter = web_ui_http_adapter_or_default();
    return (adapter->query_key_value ? adapter->query_key_value : s_default_http_adapter.query_key_value)(
        query, key, val, val_size);
}

size_t web_ui_http_req_get_hdr_value_len(httpd_req_t *req, const char *field)
{
    const web_ui_http_adapter_t *adapter = web_ui_http_adapter_or_default();
    return (adapter->req_get_hdr_value_len ? adapter->req_get_hdr_value_len
                                           : s_default_http_adapter.req_get_hdr_value_len)(req, field);
}

esp_err_t web_ui_http_req_get_hdr_value_str(httpd_req_t *req, const char *field, char *val, size_t val_size)
{
    const web_ui_http_adapter_t *adapter = web_ui_http_adapter_or_default();
    return (adapter->req_get_hdr_value_str ? adapter->req_get_hdr_value_str
                                           : s_default_http_adapter.req_get_hdr_value_str)(
        req, field, val, val_size);
}

void web_ui_http_set_adapter_for_test(const web_ui_http_adapter_t *adapter)
{
    s_http_adapter = adapter ? adapter : &s_default_http_adapter;
}

void web_ui_http_reset_adapter_for_test(void)
{
    s_http_adapter = &s_default_http_adapter;
}

esp_err_t web_ui_send_json(httpd_req_t *req, cJSON *root)
{
    esp_err_t err = ESP_OK;
    if (!req || !root) {
        if (root) {
            cJSON_Delete(root);
        }
        return ESP_ERR_INVALID_ARG;
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return web_ui_http_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json encode failed");
    }
    err = web_ui_http_resp_set_type(req, "application/json");
    if (err == ESP_OK) {
        size_t len = strlen(json);
        size_t offset = 0;
        while (offset < len) {
            size_t chunk = len - offset;
            if (chunk > WEB_UI_JSON_CHUNK_SIZE) {
                chunk = WEB_UI_JSON_CHUNK_SIZE;
            }
            err = web_ui_http_resp_send_chunk(req, json + offset, chunk);
            if (err != ESP_OK) {
                break;
            }
            offset += chunk;
        }
    }
    if (err == ESP_OK) {
        err = web_ui_http_resp_send_chunk(req, NULL, 0);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "json chunk send failed: %s", esp_err_to_name(err));
    }
    cJSON_free(json);
    return err;
}
