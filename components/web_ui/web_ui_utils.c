#define WEB_UI_UTILS_NO_HTTP_MACROS
#include "web_ui_utils.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static esp_err_t default_resp_send(httpd_req_t *req, const char *body, ssize_t body_len)
{
    return httpd_resp_send(req, body, body_len);
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

esp_err_t web_ui_http_resp_send(httpd_req_t *req, const char *body, ssize_t body_len)
{
    return s_http_adapter->resp_send(req, body, body_len);
}

esp_err_t web_ui_http_resp_send_err(httpd_req_t *req, httpd_err_code_t error, const char *message)
{
    return s_http_adapter->resp_send_err(req, error, message);
}

esp_err_t web_ui_http_resp_set_status(httpd_req_t *req, const char *status)
{
    return s_http_adapter->resp_set_status(req, status);
}

esp_err_t web_ui_http_resp_set_type(httpd_req_t *req, const char *type)
{
    return s_http_adapter->resp_set_type(req, type);
}

esp_err_t web_ui_http_resp_set_hdr(httpd_req_t *req, const char *field, const char *value)
{
    return s_http_adapter->resp_set_hdr(req, field, value);
}

int web_ui_http_req_recv(httpd_req_t *req, char *buf, size_t buf_len)
{
    return s_http_adapter->req_recv(req, buf, buf_len);
}

esp_err_t web_ui_http_req_get_url_query_str(httpd_req_t *req, char *buf, size_t buf_len)
{
    return s_http_adapter->req_get_url_query_str(req, buf, buf_len);
}

esp_err_t web_ui_http_query_key_value(const char *query, const char *key, char *val, size_t val_size)
{
    return s_http_adapter->query_key_value(query, key, val, val_size);
}

size_t web_ui_http_req_get_hdr_value_len(httpd_req_t *req, const char *field)
{
    return s_http_adapter->req_get_hdr_value_len(req, field);
}

esp_err_t web_ui_http_req_get_hdr_value_str(httpd_req_t *req, const char *field, char *val, size_t val_size)
{
    return s_http_adapter->req_get_hdr_value_str(req, field, val, val_size);
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
    esp_err_t err = web_ui_send_ok(req, "application/json", json);
    free(json);
    return err;
}

void web_ui_url_decode(char *out, size_t out_len, const char *in)
{
    if (!out || out_len == 0 || !in) {
        return;
    }
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 1 < out_len; ++i) {
        if (in[i] == '%' && isxdigit((unsigned char)in[i + 1]) && isxdigit((unsigned char)in[i + 2])) {
            char hex[3] = {in[i + 1], in[i + 2], 0};
            out[o++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else if (in[i] == '+') {
            out[o++] = ' ';
        } else {
            out[o++] = in[i];
        }
    }
    out[o] = 0;
}

void web_ui_sanitize_filename_token(char *out, size_t out_len, const char *in, const char *fallback)
{
    if (!out || out_len == 0) {
        return;
    }
    size_t o = 0;
    const char *src = (in && in[0]) ? in : fallback;
    if (!src) {
        src = "file";
    }
    for (size_t i = 0; src[i] && o + 1 < out_len; ++i) {
        unsigned char c = (unsigned char)src[i];
        if (isalnum(c) || c == '-' || c == '_') {
            out[o++] = (char)c;
        } else {
            out[o++] = '_';
        }
    }
    if (o == 0) {
        const char *def = (fallback && fallback[0]) ? fallback : "file";
        for (size_t i = 0; def[i] && o + 1 < out_len; ++i) {
            unsigned char c = (unsigned char)def[i];
            if (isalnum(c) || c == '-' || c == '_') {
                out[o++] = (char)c;
            }
        }
    }
    if (o == 0 && out_len > 1) {
        out[o++] = 'f';
    }
    out[o] = 0;
}

static bool web_ui_origin_matches_host(const char *origin, const char *host)
{
    if (!origin || !origin[0] || !host || !host[0]) {
        return false;
    }
    const char *http_prefix = "http://";
    const char *https_prefix = "https://";
    size_t host_len = strlen(host);
    if (strncmp(origin, http_prefix, strlen(http_prefix)) == 0) {
        const char *p = origin + strlen(http_prefix);
        return strncmp(p, host, host_len) == 0 && (p[host_len] == '\0' || p[host_len] == '/');
    }
    if (strncmp(origin, https_prefix, strlen(https_prefix)) == 0) {
        const char *p = origin + strlen(https_prefix);
        return strncmp(p, host, host_len) == 0 && (p[host_len] == '\0' || p[host_len] == '/');
    }
    return false;
}

bool web_ui_is_same_origin_request(httpd_req_t *req)
{
    if (!req) {
        return false;
    }
    size_t host_len = httpd_req_get_hdr_value_len(req, "Host");
    if (host_len == 0 || host_len >= 128) {
        return false;
    }
    char host[128];
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK || !host[0]) {
        return false;
    }

    size_t origin_len = httpd_req_get_hdr_value_len(req, "Origin");
    if (origin_len > 0 && origin_len < 256) {
        char origin[256];
        if (httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) == ESP_OK) {
            return web_ui_origin_matches_host(origin, host);
        }
        return false;
    }

    size_t referer_len = httpd_req_get_hdr_value_len(req, "Referer");
    if (referer_len > 0 && referer_len < 384) {
        char referer[384];
        if (httpd_req_get_hdr_value_str(req, "Referer", referer, sizeof(referer)) == ESP_OK) {
            return web_ui_origin_matches_host(referer, host);
        }
        return false;
    }

    return false;
}
