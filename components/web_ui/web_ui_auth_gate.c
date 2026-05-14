#include "web_ui_auth_internal.h"

#include "esp_log.h"
#include "web_ui_utils.h"

static esp_err_t web_same_origin_reject(httpd_req_t *req)
{
    httpd_resp_set_status(req, "403 Forbidden");
    httpd_resp_set_type(req, "application/json");
    return WEB_HTTP_CHECK(httpd_resp_send(req,
                                          "{\"error\":\"csrf\",\"message\":\"same-origin check failed\"}",
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
    if (req->method != HTTP_GET && !web_ui_is_same_origin_request(req)) {
        return web_same_origin_reject(req);
    }
#if WEB_AUTH_TRACE_HTTP
    const char *uri = req ? req->uri : "?";
    ESP_LOGI(g_web_ui_auth_tag,
             "HTTP begin method=%d uri=%s stack_hwm=%u",
             (int)req->method,
             uri,
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
#endif
    esp_err_t err = route->fn(req);
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
