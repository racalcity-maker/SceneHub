#include "web_ui_auth_internal.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "web_ui_utils.h"

const char *g_web_ui_auth_tag = "web_ui_auth";
SemaphoreHandle_t g_web_ui_auth_session_mutex = NULL;
web_session_entry_t g_web_ui_auth_sessions[WEB_SESSION_MAX];

esp_err_t web_http_check(esp_err_t err, const char *context)
{
    if (err != ESP_OK) {
        web_ui_report_httpd_error(err, context);
    }
    return err;
}

bool auth_hash_equal_consttime(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t diff = 0;
    if (!a || !b) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff == 0;
}

bool read_cookie_value(httpd_req_t *req, const char *name, char *out, size_t out_len)
{
    if (!req || !name || !out || out_len == 0) {
        return false;
    }
    size_t hdr_len = httpd_req_get_hdr_value_len(req, "Cookie");
    if (hdr_len <= 0 || hdr_len >= 512) {
        return false;
    }
    char *buf = web_ui_malloc(hdr_len + 1);
    if (!buf) {
        return false;
    }
    if (httpd_req_get_hdr_value_str(req, "Cookie", buf, hdr_len + 1) != ESP_OK) {
        web_ui_free(buf);
        return false;
    }
    bool found = false;
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "%s=", name);
    char *start = strstr(buf, pattern);
    if (start) {
        start += strlen(pattern);
        char *end = strchr(start, ';');
        size_t copy_len = end ? (size_t)(end - start) : strlen(start);
        if (copy_len >= out_len) {
            copy_len = out_len - 1;
        }
        memcpy(out, start, copy_len);
        out[copy_len] = 0;
        found = true;
    }
    web_ui_free(buf);
    return found;
}

const char *web_role_to_string(web_user_role_t role)
{
    return (role == WEB_USER_ROLE_USER) ? "user" : "admin";
}

bool web_role_allows(web_user_role_t have, web_user_role_t required)
{
    if (required == WEB_USER_ROLE_USER) {
        return true;
    }
    return have == WEB_USER_ROLE_ADMIN;
}

bool web_ui_require_session(httpd_req_t *req, bool redirect_on_fail, web_user_role_t *role_out)
{
    if (!req) {
        return false;
    }
    char token[WEB_SESSION_TOKEN_LEN] = {0};
    web_user_role_t role = WEB_USER_ROLE_ADMIN;
    if (read_cookie_value(req, "broker_sid", token, sizeof(token)) &&
        web_session_validate(token, &role, NULL, 0)) {
        if (role_out) {
            *role_out = role;
        }
        return true;
    }
    if (redirect_on_fail) {
        char location[128];
        const char *prefix = "/login?next=";
        const char *next_uri = req->uri[0] ? req->uri : "/";
        size_t prefix_len = strlen(prefix);
        size_t copy_len = sizeof(location) > prefix_len ? sizeof(location) - prefix_len - 1 : 0;
        memcpy(location, prefix, prefix_len);
        location[prefix_len] = '\0';
        if (copy_len > 0) {
            strncat(location, next_uri, copy_len);
        }
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", location);
        WEB_HTTP_CHECK(httpd_resp_send(req, NULL, 0));
    } else {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_type(req, "application/json");
        WEB_HTTP_CHECK(httpd_resp_send(req, "{\"error\":\"unauthorized\"}", HTTPD_RESP_USE_STRLEN));
    }
    return false;
}

char *read_request_body(httpd_req_t *req, size_t max_len)
{
    if (!req) {
        return NULL;
    }
    size_t len = req->content_len;
    if (len == 0 || len > max_len) {
        return NULL;
    }
    char *body = web_ui_malloc(len + 1);
    if (!body) {
        return NULL;
    }
    size_t received = 0;
    while (received < len) {
        int r = httpd_req_recv(req, body + received, len - received);
        if (r <= 0) {
            if (r == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            web_ui_free(body);
            return NULL;
        }
        received += (size_t)r;
    }
    body[len] = 0;
    return body;
}

esp_err_t auth_login_reject(httpd_req_t *req, const char *message)
{
    const char *reason = (message && message[0]) ? message : "Login failed.";
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem"));
    }
    cJSON_AddStringToObject(root, "error", "auth_failed");
    cJSON_AddStringToObject(root, "message", reason);
    httpd_resp_set_status(req, "401 Unauthorized");
    return WEB_HTTP_CHECK(web_ui_send_json(req, root));
}
