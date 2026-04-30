#include "web_ui_devices.h"

#include "esp_log.h"
#include "web_ui_utils.h"

static const char *TAG = "web_ui_assets";

extern const uint8_t _binary_gm_panel_css_start[] asm("_binary_gm_panel_css_start");
extern const uint8_t _binary_gm_panel_css_end[] asm("_binary_gm_panel_css_end");
extern const uint8_t _binary_gm_panel_js_start[] asm("_binary_gm_panel_js_start");
extern const uint8_t _binary_gm_panel_js_end[] asm("_binary_gm_panel_js_end");

static esp_err_t send_embedded_asset(httpd_req_t *req,
                                     const uint8_t *start,
                                     const uint8_t *end,
                                     const char *content_type,
                                     const char *path)
{
    httpd_resp_set_type(req, content_type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    const size_t len = end - start;
    const size_t send_len = (len > 0) ? len - 1 : 0;
    esp_err_t err = httpd_resp_send(req, (const char *)start, send_len);
    if (err != ESP_OK) {
        web_ui_report_httpd_error(err, path);
    }
    return err;
}

static esp_err_t gm_css_handler(httpd_req_t *req)
{
    return send_embedded_asset(req,
                               _binary_gm_panel_css_start,
                               _binary_gm_panel_css_end,
                               "text/css",
                               "/ui/gm_panel.css");
}

static esp_err_t gm_js_handler(httpd_req_t *req)
{
    return send_embedded_asset(req,
                               _binary_gm_panel_js_start,
                               _binary_gm_panel_js_end,
                               "application/javascript",
                               "/ui/gm_panel.js");
}

static const httpd_uri_t s_gm_css_uri = {
    .uri = "/ui/gm_panel.css",
    .method = HTTP_GET,
    .handler = gm_css_handler,
    .user_ctx = NULL,
};

static const httpd_uri_t s_gm_js_uri = {
    .uri = "/ui/gm_panel.js",
    .method = HTTP_GET,
    .handler = gm_js_handler,
    .user_ctx = NULL,
};

esp_err_t web_ui_devices_register_assets(httpd_handle_t server)
{
    esp_err_t err = ESP_OK;

    err = httpd_register_uri_handler(server, &s_gm_css_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register gm css failed: %s", esp_err_to_name(err));
        web_ui_report_httpd_error(err, "/ui/gm_panel.css register");
        return err;
    }
    err = httpd_register_uri_handler(server, &s_gm_js_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register gm js failed: %s", esp_err_to_name(err));
        web_ui_report_httpd_error(err, "/ui/gm_panel.js register");
        return err;
    }
    return ESP_OK;
}
