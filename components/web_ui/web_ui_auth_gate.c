#include "web_ui_auth_internal.h"

#include <string.h>

#include "esp_timer.h"
#include "esp_log.h"
#include "config_store.h"
#include "web_ui_utils.h"

#define WEB_UI_SLOW_HANDLER_MS 750

static esp_err_t web_same_origin_reject(httpd_req_t *req)
{
    httpd_resp_set_status(req, "403 Forbidden");
    httpd_resp_set_type(req, "application/json");
    return WEB_HTTP_CHECK(httpd_resp_send(req,
                                          "{\"error\":\"csrf\",\"message\":\"same-origin check failed\"}",
                                          HTTPD_RESP_USE_STRLEN));
}

static bool admin_password_change_allowed_uri(const char *uri)
{
    char path[96];
    size_t len = 0;
    if (!uri) {
        return false;
    }
    while (uri[len] && uri[len] != '?' && len < sizeof(path) - 1) {
        path[len] = uri[len];
        ++len;
    }
    path[len] = '\0';
    return strcmp(path, "/") == 0 ||
           strcmp(path, "/api/status") == 0 ||
           strcmp(path, "/api/session/info") == 0 ||
           strcmp(path, "/api/auth/password") == 0 ||
           strcmp(path, "/api/auth/logout") == 0;
}

static esp_err_t admin_password_change_required_response(httpd_req_t *req)
{
    char path[96];
    size_t len = 0;
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }
    while (req->uri[len] && req->uri[len] != '?' && len < sizeof(path) - 1) {
        path[len] = req->uri[len];
        ++len;
    }
    path[len] = '\0';
    if (req->method == HTTP_GET && strcmp(path, "/gm") == 0) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/?force_password_change=1");
        return WEB_HTTP_CHECK(httpd_resp_send(req, NULL, 0));
    }
    if (req->method == HTTP_GET && strcmp(path, "/") == 0) {
        return ESP_OK;
    }
    httpd_resp_set_status(req, "403 Forbidden");
    httpd_resp_set_type(req, "application/json");
    return WEB_HTTP_CHECK(httpd_resp_send(req,
                                          "{\"error\":\"password_change_required\",\"message\":\"admin password must be changed before normal use\"}",
                                          HTTPD_RESP_USE_STRLEN));
}

esp_err_t auth_gate_handler(httpd_req_t *req)
{
    const web_route_t *route = (const web_route_t *)req->user_ctx;
    if (!route || !route->fn) {
        return WEB_HTTP_CHECK(httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "route missing"));
    }
    web_user_role_t role = WEB_USER_ROLE_ADMIN;
    if (!web_ui_require_session(req, route->redirect_on_fail, &role)) {
        return ESP_OK;
    }
    if (!web_role_allows(role, route->min_role)) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "application/json");
        WEB_HTTP_CHECK(httpd_resp_send(req, "{\"error\":\"forbidden\"}", HTTPD_RESP_USE_STRLEN));
        return ESP_OK;
    }
    const app_config_t *cfg = config_store_get();
    if (role == WEB_USER_ROLE_ADMIN &&
        cfg &&
        !cfg->web.password_initialized &&
        !admin_password_change_allowed_uri(req->uri)) {
        return admin_password_change_required_response(req);
    }
    if (req->method != HTTP_GET && !web_ui_is_same_origin_request(req)) {
        return web_same_origin_reject(req);
    }
    const char *uri = req ? req->uri : "?";
#if WEB_AUTH_TRACE_HTTP
    ESP_LOGI(g_web_ui_auth_tag,
             "HTTP begin method=%d uri=%s stack_hwm=%u",
             (int)req->method,
             uri,
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
#endif
    int64_t started_us = esp_timer_get_time();
    esp_err_t err = route->fn(req);
    int64_t elapsed_ms = (esp_timer_get_time() - started_us) / 1000;
    if (elapsed_ms >= WEB_UI_SLOW_HANDLER_MS) {
        ESP_LOGW(g_web_ui_auth_tag,
                 "HTTP slow method=%d uri=%s elapsed_ms=%lld err=%s stack_hwm=%u",
                 (int)req->method,
                 uri,
                 (long long)elapsed_ms,
                 esp_err_to_name(err),
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
    }
#if WEB_AUTH_TRACE_HTTP
    ESP_LOGI(g_web_ui_auth_tag,
             "HTTP end method=%d uri=%s err=%s stack_hwm=%u",
             (int)req->method,
             uri,
             esp_err_to_name(err),
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
#endif
    return err;
}
