#include "node_provisioning_internal.h"
#include "node_provisioning_config_api_internal.h"

#include <string.h>

#include "esp_log.h"

static bool http_copy_header_value(httpd_req_t *req, const char *name, char *out, size_t out_size)
{
    size_t value_len = 0;

    if (!req || !name || !out || out_size == 0) {
        return false;
    }
    out[0] = '\0';
    value_len = httpd_req_get_hdr_value_len(req, name);
    if (value_len == 0 || value_len >= out_size) {
        return false;
    }
    return httpd_req_get_hdr_value_str(req, name, out, out_size) == ESP_OK;
}

static void log_admin_request_headers(const char *action, httpd_req_t *req)
{
    char user_agent[96] = {0};
    char origin[96] = {0};
    char referer[128] = {0};
    char host[64] = {0};
    char action_header[32] = {0};

    if (!req) {
        return;
    }
    (void)http_copy_header_value(req, "User-Agent", user_agent, sizeof(user_agent));
    (void)http_copy_header_value(req, "Origin", origin, sizeof(origin));
    (void)http_copy_header_value(req, "Referer", referer, sizeof(referer));
    (void)http_copy_header_value(req, "Host", host, sizeof(host));
    (void)http_copy_header_value(req, "X-Node-Action", action_header, sizeof(action_header));
    ESP_LOGW(g_node_provisioning_tag,
             "admin request action=%s method=%d content_len=%d host=%s x_node_action=%s origin=%s referer=%s ua=%s",
             action ? action : "",
             req->method,
             req->content_len,
             host,
             action_header,
             origin,
             referer,
             user_agent);
}

esp_err_t node_provisioning_keep_open_post(httpd_req_t *req)
{
    if (!g_node_prov.status.auto_close_supported) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req,
                                  "{\"ok\":true,\"auto_close_supported\":false,"
                                  "\"auto_close_keep_open\":false}");
    }

    node_provisioning_keep_open_for_boot();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req,
                              "{\"ok\":true,\"auto_close_supported\":true,"
                              "\"auto_close_keep_open\":true}");
}

esp_err_t node_provisioning_restart_post(httpd_req_t *req)
{
    char action_header[32] = {0};
    node_admin_control_result_t admin_result = {0};

    log_admin_request_headers("restart", req);
    if (!http_copy_header_value(req, "X-Node-Action", action_header, sizeof(action_header)) ||
        strcmp(action_header, "restart") != 0) {
        ESP_LOGW(g_node_provisioning_tag, "restart rejected: missing or invalid X-Node-Action header");
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "restart confirmation required");
    }
    if (node_admin_control_restart(&admin_result) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "restart failed");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"restarting\":true}");
}

esp_err_t node_provisioning_reset_wifi_post(httpd_req_t *req)
{
    node_admin_control_result_t admin_result = {0};
    esp_err_t err = node_admin_control_reset_wifi(&admin_result);
    if (err != ESP_OK) {
        ESP_LOGE(g_node_provisioning_tag, "wifi reset failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "wifi reset failed");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"restart_required\":true}");
}

esp_err_t node_provisioning_factory_reset_post(httpd_req_t *req)
{
    node_admin_control_result_t admin_result = {0};
    esp_err_t err = node_admin_control_factory_reset(&admin_result);
    if (err != ESP_OK) {
        ESP_LOGE(g_node_provisioning_tag, "factory reset failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "factory reset failed");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"restart_required\":true}");
}
