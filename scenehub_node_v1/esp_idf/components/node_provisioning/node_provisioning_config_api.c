#include "node_provisioning_internal.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "node_board.h"
#include "node_limits.h"

static const char *TAG = "node_prov_api";
static char s_config_json[4096];
static char s_post_body[4097];
static StaticSemaphore_t s_post_body_mutex_storage;
static SemaphoreHandle_t s_post_body_mutex;

static bool lock_post_body(void)
{
    if (!s_post_body_mutex) {
        s_post_body_mutex = xSemaphoreCreateMutexStatic(&s_post_body_mutex_storage);
    }
    return s_post_body_mutex && xSemaphoreTake(s_post_body_mutex, pdMS_TO_TICKS(1000)) == pdTRUE;
}

static void unlock_post_body(void)
{
    if (s_post_body_mutex) {
        xSemaphoreGive(s_post_body_mutex);
    }
}

esp_err_t node_provisioning_status_get(httpd_req_t *req)
{
    char body[160];
    int n = snprintf(body,
                     sizeof(body),
                     "{\"ok\":true,\"mode\":\"%s\",\"ap_started\":%s,\"web_started\":%s,"
                     "\"sta_got_ip\":%s,\"sta_disconnected\":%s,\"sta_disconnect_reason\":%u}",
                     g_node_prov.status.mode == NODE_PROVISIONING_MODE_AP ? "ap" : "sta",
                     g_node_prov.status.ap_started ? "true" : "false",
                     g_node_prov.status.web_started ? "true" : "false",
                     g_node_prov.status.sta_got_ip ? "true" : "false",
                     g_node_prov.status.sta_disconnected ? "true" : "false",
                     (unsigned)g_node_prov.status.sta_disconnect_reason);
    if (n < 0 || n >= (int)sizeof(body)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "status too large");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body, (ssize_t)n);
}

esp_err_t node_provisioning_config_get(httpd_req_t *req)
{
    int n = snprintf(s_config_json,
                     sizeof(s_config_json),
                     "{\"ok\":true,\"node_id\":\"%s\",\"node_name\":\"%s\","
                     "\"wifi_ssid\":\"%s\",\"controller_host\":\"%s\","
                     "\"mqtt_port\":%u,\"mqtt_client_id\":\"%s\","
                     "\"reset_gpio\":%d,\"pin_config_locked\":%s,"
                     "\"relays\":[",
                     g_node_prov.config.node_id,
                     g_node_prov.config.node_name,
                     g_node_prov.config.wifi_ssid,
                     g_node_prov.config.controller_host,
                     (unsigned)g_node_prov.config.mqtt_port,
                     g_node_prov.config.mqtt_client_id,
                     g_node_prov.config.reset_gpio,
                     g_node_prov.config.pin_config_locked ? "true" : "false");
    if (n < 0 || n >= (int)sizeof(s_config_json)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config too large");
    }
    size_t len = (size_t)n;
#define APPEND_JSON(...) do { \
        int _n = snprintf(s_config_json + len, sizeof(s_config_json) - len, __VA_ARGS__); \
        if (_n < 0 || (size_t)_n >= sizeof(s_config_json) - len) { \
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config too large"); \
        } \
        len += (size_t)_n; \
    } while (0)
    for (size_t i = 0; i < NODE_RELAY_MAX; ++i) {
        const node_output_pin_config_t *p = &g_node_prov.config.relays[i];
        APPEND_JSON("%s{\"enabled\":%s,\"gpio\":%d,\"active_low\":%s,\"label\":\"%s\"}",
                    i ? "," : "",
                    p->enabled ? "true" : "false",
                    p->gpio,
                    p->active_low ? "true" : "false",
                    p->label);
    }
    APPEND_JSON("],\"mosfets\":[");
    for (size_t i = 0; i < NODE_MOSFET_MAX; ++i) {
        const node_output_pin_config_t *p = &g_node_prov.config.mosfets[i];
        APPEND_JSON("%s{\"enabled\":%s,\"gpio\":%d,\"active_low\":%s,\"label\":\"%s\"}",
                    i ? "," : "",
                    p->enabled ? "true" : "false",
                    p->gpio,
                    p->active_low ? "true" : "false",
                    p->label);
    }
    APPEND_JSON("],\"universal_io\":[");
    for (size_t i = 0; i < NODE_UNIVERSAL_IO_MAX; ++i) {
        const node_universal_pin_config_t *p = &g_node_prov.config.universal_io[i];
        APPEND_JSON("%s{\"enabled\":%s,\"gpio\":%d,\"role\":%d,\"active_low\":%s,\"label\":\"%s\"}",
                    i ? "," : "",
                    p->enabled ? "true" : "false",
                    p->gpio,
                    (int)p->role,
                    p->active_low ? "true" : "false",
                    p->label);
    }
    APPEND_JSON("],\"led_strips\":[");
    for (size_t i = 0; i < NODE_LED_STRIP_MAX; ++i) {
        const node_led_strip_config_t *p = &g_node_prov.config.led_strips[i];
        APPEND_JSON("%s{\"enabled\":%s,\"gpio\":%d,\"pixel_count\":%u,\"label\":\"%s\"}",
                    i ? "," : "",
                    p->enabled ? "true" : "false",
                    p->gpio,
                    (unsigned)p->pixel_count,
                    p->label);
    }
    APPEND_JSON("]}");
#undef APPEND_JSON
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, s_config_json, (ssize_t)len);
}

static bool json_copy_string_field(const char *json, const char *key, char *out, size_t out_size)
{
    if (!json || !key || !out || out_size == 0) {
        return false;
    }
    char pattern[48];
    int pn = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (pn <= 0 || pn >= (int)sizeof(pattern)) {
        return false;
    }
    const char *p = strstr(json, pattern);
    if (!p || !(p = strchr(p + pn, ':'))) {
        return false;
    }
    ++p;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        ++p;
    }
    if (*p++ != '"') {
        return false;
    }
    size_t len = 0;
    while (p[len] && p[len] != '"' && len + 1 < out_size) {
        if (p[len] == '\\') {
            return false;
        }
        out[len] = p[len];
        ++len;
    }
    if (p[len] != '"') {
        out[0] = '\0';
        return false;
    }
    out[len] = '\0';
    return true;
}

static bool json_copy_nonempty_string_field(const char *json, const char *key, char *out, size_t out_size)
{
    char scratch[NODE_WIFI_PASSWORD_MAX_LEN];
    if (!json_copy_string_field(json, key, scratch, sizeof(scratch))) {
        return false;
    }
    if (scratch[0] == '\0') {
        return false;
    }
    snprintf(out, out_size, "%s", scratch);
    return true;
}

static bool json_copy_int_field(const char *json, const char *key, int *out)
{
    char pattern[48];
    int pn = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (pn <= 0 || pn >= (int)sizeof(pattern)) {
        return false;
    }
    const char *p = strstr(json, pattern);
    return p && (p = strchr(p + pn, ':')) && sscanf(p + 1, "%d", out) == 1;
}

static bool json_copy_u16_field(const char *json, const char *key, uint16_t *out)
{
    int value = 0;
    if (!json_copy_int_field(json, key, &value) || value < 0 || value > 65535) {
        return false;
    }
    *out = (uint16_t)value;
    return true;
}

static bool json_copy_bool_field(const char *json, const char *key, bool *out)
{
    char pattern[48];
    int pn = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (pn <= 0 || pn >= (int)sizeof(pattern)) {
        return false;
    }
    const char *p = strstr(json, pattern);
    if (!p || !(p = strchr(p + pn, ':'))) {
        return false;
    }
    ++p;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        ++p;
    }
    if (strncmp(p, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if (strncmp(p, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}

static void make_indexed_key(char *out, size_t out_size, const char *prefix, size_t index, const char *field)
{
    snprintf(out, out_size, "%s%u_%s", prefix, (unsigned)(index + 1), field);
}

static void apply_output_pin_fields(const char *body, const char *prefix, node_output_pin_config_t *pins, size_t count)
{
    char key[40];
    for (size_t i = 0; i < count; ++i) {
        node_output_pin_config_t *pin = &pins[i];
        make_indexed_key(key, sizeof(key), prefix, i, "enabled");
        json_copy_bool_field(body, key, &pin->enabled);
        make_indexed_key(key, sizeof(key), prefix, i, "gpio");
        json_copy_int_field(body, key, &pin->gpio);
        make_indexed_key(key, sizeof(key), prefix, i, "active_low");
        json_copy_bool_field(body, key, &pin->active_low);
        make_indexed_key(key, sizeof(key), prefix, i, "label");
        json_copy_string_field(body, key, pin->label, sizeof(pin->label));
        if (!node_board_gpio_is_allowed(pin->gpio)) {
            pin->enabled = false;
            pin->gpio = -1;
        }
    }
}

static void apply_universal_io_fields(const char *body, node_universal_pin_config_t *pins, size_t count)
{
    char key[40];
    for (size_t i = 0; i < count; ++i) {
        node_universal_pin_config_t *pin = &pins[i];
        int role = (int)pin->role;
        make_indexed_key(key, sizeof(key), "io", i, "enabled");
        json_copy_bool_field(body, key, &pin->enabled);
        make_indexed_key(key, sizeof(key), "io", i, "gpio");
        json_copy_int_field(body, key, &pin->gpio);
        make_indexed_key(key, sizeof(key), "io", i, "role");
        json_copy_int_field(body, key, &role);
        make_indexed_key(key, sizeof(key), "io", i, "active_low");
        json_copy_bool_field(body, key, &pin->active_low);
        make_indexed_key(key, sizeof(key), "io", i, "label");
        json_copy_string_field(body, key, pin->label, sizeof(pin->label));
        pin->role = role == (int)NODE_PIN_UNIVERSAL_INPUT
                        ? NODE_PIN_UNIVERSAL_INPUT
                        : (role == (int)NODE_PIN_UNIVERSAL_OUTPUT ? NODE_PIN_UNIVERSAL_OUTPUT : NODE_PIN_DISABLED);
        if (!node_board_gpio_is_allowed(pin->gpio) || pin->role == NODE_PIN_DISABLED) {
            pin->enabled = false;
            pin->gpio = -1;
        }
    }
}

static void apply_led_fields(const char *body, node_led_strip_config_t *pins, size_t count)
{
    char key[40];
    for (size_t i = 0; i < count; ++i) {
        node_led_strip_config_t *pin = &pins[i];
        int pixels = pin->pixel_count;
        make_indexed_key(key, sizeof(key), "led", i, "enabled");
        json_copy_bool_field(body, key, &pin->enabled);
        make_indexed_key(key, sizeof(key), "led", i, "gpio");
        json_copy_int_field(body, key, &pin->gpio);
        make_indexed_key(key, sizeof(key), "led", i, "pixel_count");
        json_copy_int_field(body, key, &pixels);
        make_indexed_key(key, sizeof(key), "led", i, "label");
        json_copy_string_field(body, key, pin->label, sizeof(pin->label));
        pin->pixel_count = pixels > 0 ? (uint16_t)pixels : 30;
        if (!node_board_gpio_is_allowed(pin->gpio)) {
            pin->enabled = false;
            pin->gpio = -1;
        }
    }
}

esp_err_t node_provisioning_config_post(httpd_req_t *req)
{
    enum { MAX_BODY = 4096 };
    if (req->content_len <= 0 || req->content_len > MAX_BODY) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body size");
    }
    if (!lock_post_body()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config busy");
    }

    int received = httpd_req_recv(req, s_post_body, req->content_len);
    if (received <= 0) {
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "body read failed");
    }
    s_post_body[received] = '\0';

    node_config_t next = g_node_prov.config;
    json_copy_string_field(s_post_body, "node_id", next.node_id, sizeof(next.node_id));
    json_copy_string_field(s_post_body, "node_name", next.node_name, sizeof(next.node_name));
    json_copy_string_field(s_post_body, "wifi_ssid", next.wifi_ssid, sizeof(next.wifi_ssid));
    json_copy_nonempty_string_field(s_post_body, "wifi_password", next.wifi_password, sizeof(next.wifi_password));
    json_copy_string_field(s_post_body, "controller_host", next.controller_host, sizeof(next.controller_host));
    json_copy_string_field(s_post_body, "mqtt_client_id", next.mqtt_client_id, sizeof(next.mqtt_client_id));
    json_copy_u16_field(s_post_body, "mqtt_port", &next.mqtt_port);
    json_copy_int_field(s_post_body, "reset_gpio", &next.reset_gpio);
    if (!next.pin_config_locked) {
        apply_output_pin_fields(s_post_body, "relay", next.relays, NODE_RELAY_MAX);
        apply_output_pin_fields(s_post_body, "mosfet", next.mosfets, NODE_MOSFET_MAX);
        apply_universal_io_fields(s_post_body, next.universal_io, NODE_UNIVERSAL_IO_MAX);
        apply_led_fields(s_post_body, next.led_strips, NODE_LED_STRIP_MAX);
    }
    if (next.mqtt_port == 0) {
        next.mqtt_port = 1883;
    }
    if (next.node_id[0] == '\0' || next.mqtt_client_id[0] == '\0') {
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "node_id and mqtt_client_id required");
    }

    esp_err_t err = node_config_save(&next);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config save failed: %s", esp_err_to_name(err));
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
    }
    g_node_prov.config = next;
    unlock_post_body();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"restart_required\":true}");
}

esp_err_t node_provisioning_restart_post(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, "{\"ok\":true,\"restarting\":true}");
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
    }
    return err;
}

esp_err_t node_provisioning_reset_wifi_post(httpd_req_t *req)
{
    esp_err_t err = node_config_reset_wifi();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi reset failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "wifi reset failed");
    }
    g_node_prov.config.wifi_ssid[0] = '\0';
    g_node_prov.config.wifi_password[0] = '\0';
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"restart_required\":true}");
}

esp_err_t node_provisioning_factory_reset_post(httpd_req_t *req)
{
    esp_err_t err = node_config_factory_reset();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "factory reset failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "factory reset failed");
    }
    node_config_set_factory_defaults(&g_node_prov.config);
    node_board_apply_factory_pin_config(&g_node_prov.config);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"restart_required\":true}");
}
