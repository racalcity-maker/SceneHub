#include "node_provisioning_internal.h"

esp_err_t node_provisioning_register_routes(httpd_handle_t httpd)
{
    const httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = node_provisioning_ui_root_get,
        .user_ctx = NULL,
    };
    const httpd_uri_t status = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = node_provisioning_status_get,
        .user_ctx = NULL,
    };
    const httpd_uri_t config_get = {
        .uri = "/api/config",
        .method = HTTP_GET,
        .handler = node_provisioning_config_get,
        .user_ctx = NULL,
    };
    const httpd_uri_t config_post = {
        .uri = "/api/config",
        .method = HTTP_POST,
        .handler = node_provisioning_config_post,
        .user_ctx = NULL,
    };
    const httpd_uri_t led_config_get = {
        .uri = "/api/led-config",
        .method = HTTP_GET,
        .handler = node_provisioning_led_config_get,
        .user_ctx = NULL,
    };
    const httpd_uri_t led_effects_schema_get = {
        .uri = "/api/led-effects-schema",
        .method = HTTP_GET,
        .handler = node_provisioning_led_effects_schema_get,
        .user_ctx = NULL,
    };
    const httpd_uri_t led_config_post = {
        .uri = "/api/led-config",
        .method = HTTP_POST,
        .handler = node_provisioning_led_config_post,
        .user_ctx = NULL,
    };
    const httpd_uri_t led_preview_post = {
        .uri = "/api/led-preview",
        .method = HTTP_POST,
        .handler = node_provisioning_led_preview_post,
        .user_ctx = NULL,
    };
    const httpd_uri_t restart_post = {
        .uri = "/api/restart",
        .method = HTTP_POST,
        .handler = node_provisioning_restart_post,
        .user_ctx = NULL,
    };
    const httpd_uri_t keep_open_post = {
        .uri = "/api/provisioning/keep-open",
        .method = HTTP_POST,
        .handler = node_provisioning_keep_open_post,
        .user_ctx = NULL,
    };
    const httpd_uri_t reset_wifi_post = {
        .uri = "/api/reset-wifi",
        .method = HTTP_POST,
        .handler = node_provisioning_reset_wifi_post,
        .user_ctx = NULL,
    };
    const httpd_uri_t factory_reset_post = {
        .uri = "/api/factory-reset",
        .method = HTTP_POST,
        .handler = node_provisioning_factory_reset_post,
        .user_ctx = NULL,
    };

    ESP_ERROR_CHECK(httpd_register_uri_handler(httpd, &root));
    ESP_ERROR_CHECK(httpd_register_uri_handler(httpd, &status));
    ESP_ERROR_CHECK(httpd_register_uri_handler(httpd, &config_get));
    ESP_ERROR_CHECK(httpd_register_uri_handler(httpd, &config_post));
    ESP_ERROR_CHECK(httpd_register_uri_handler(httpd, &led_config_get));
    ESP_ERROR_CHECK(httpd_register_uri_handler(httpd, &led_effects_schema_get));
    ESP_ERROR_CHECK(httpd_register_uri_handler(httpd, &led_config_post));
    ESP_ERROR_CHECK(httpd_register_uri_handler(httpd, &led_preview_post));
    ESP_ERROR_CHECK(httpd_register_uri_handler(httpd, &restart_post));
    ESP_ERROR_CHECK(httpd_register_uri_handler(httpd, &keep_open_post));
    ESP_ERROR_CHECK(httpd_register_uri_handler(httpd, &reset_wifi_post));
    ESP_ERROR_CHECK(httpd_register_uri_handler(httpd, &factory_reset_post));
    return ESP_OK;
}
