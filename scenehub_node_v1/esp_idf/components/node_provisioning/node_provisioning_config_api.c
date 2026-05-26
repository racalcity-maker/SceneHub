#include "node_provisioning_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "node_admin_control.h"
#include "node_board.h"
#include "node_control.h"
#include "node_limits.h"

static const char *TAG = "node_prov_api";
static char s_config_json[24576];
static char s_post_body[10241];
static node_config_t s_next_config;
static node_config_t s_get_config;
static node_control_result_t s_preview_result;
static StaticSemaphore_t s_post_body_mutex_storage;
static SemaphoreHandle_t s_post_body_mutex;
static StaticSemaphore_t s_config_json_mutex_storage;
static SemaphoreHandle_t s_config_json_mutex;

#define NODE_PROV_LED_EDITOR_MAX_TIMING_MS 60000U

static bool json_copy_string_field(const char *json, const char *key, char *out, size_t out_size);

static esp_err_t send_preview_json(httpd_req_t *req,
                                   bool ok,
                                   const char *status,
                                   const char *error_code,
                                   const char *command)
{
    char response[192];
    int written = 0;

    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }

    written = snprintf(response,
                       sizeof(response),
                       "{\"ok\":%s,\"status\":\"%s\",\"error_code\":\"%s\",\"command\":\"%s\"}",
                       ok ? "true" : "false",
                       status ? status : "",
                       error_code ? error_code : "",
                       command ? command : "");
    if (written < 0 || written >= (int)sizeof(response)) {
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, response, written);
}

static bool preview_status_ok(const char *status)
{
    return status &&
           (strcmp(status, "done") == 0 ||
            strcmp(status, "started") == 0 ||
            strcmp(status, "accepted") == 0 ||
            strcmp(status, "scheduled") == 0);
}

static bool led_preview_command_allowed(const char *command)
{
    return command &&
           (strcmp(command, "led.off") == 0 || strcmp(command, "led.preview.blink") == 0 ||
            strcmp(command, "led.preview.breathe") == 0 || strcmp(command, "led.preview.effect") == 0);
}

static bool read_request_body(httpd_req_t *req, char *out, size_t out_size)
{
    int remaining = 0;
    int offset = 0;

    if (!req || !out || out_size == 0 || req->content_len <= 0 || req->content_len >= (int)out_size) {
        return false;
    }

    remaining = req->content_len;
    while (remaining > 0) {
        int received = httpd_req_recv(req, out + offset, remaining);
        if (received <= 0) {
            return false;
        }
        offset += received;
        remaining -= received;
    }
    out[offset] = '\0';
    return true;
}

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

static bool lock_config_json(void)
{
    if (!s_config_json_mutex) {
        s_config_json_mutex = xSemaphoreCreateMutexStatic(&s_config_json_mutex_storage);
    }
    return s_config_json_mutex && xSemaphoreTake(s_config_json_mutex, pdMS_TO_TICKS(1000)) == pdTRUE;
}

static void unlock_config_json(void)
{
    if (s_config_json_mutex) {
        xSemaphoreGive(s_config_json_mutex);
    }
}

static bool json_escape_string(char *out, size_t out_size, const char *value)
{
    size_t len = 0;
    const unsigned char *p = (const unsigned char *)(value ? value : "");

    if (!out || out_size == 0) {
        return false;
    }

    while (*p) {
        char escape[7] = {0};
        const char *chunk = NULL;
        size_t chunk_len = 0;

        switch (*p) {
        case '\"':
            chunk = "\\\"";
            chunk_len = 2;
            break;
        case '\\':
            chunk = "\\\\";
            chunk_len = 2;
            break;
        case '\b':
            chunk = "\\b";
            chunk_len = 2;
            break;
        case '\f':
            chunk = "\\f";
            chunk_len = 2;
            break;
        case '\n':
            chunk = "\\n";
            chunk_len = 2;
            break;
        case '\r':
            chunk = "\\r";
            chunk_len = 2;
            break;
        case '\t':
            chunk = "\\t";
            chunk_len = 2;
            break;
        default:
            if (*p < 0x20) {
                snprintf(escape, sizeof(escape), "\\u%04x", (unsigned)*p);
                chunk = escape;
                chunk_len = 6;
            }
            break;
        }

        if (!chunk) {
            if (len + 1 >= out_size) {
                out[0] = '\0';
                return false;
            }
            out[len++] = (char)*p++;
            continue;
        }
        if (len + chunk_len >= out_size) {
            out[0] = '\0';
            return false;
        }
        memcpy(out + len, chunk, chunk_len);
        len += chunk_len;
        ++p;
    }

    out[len] = '\0';
    return true;
}

static uint32_t clamp_u32_or_default(int value, uint32_t fallback, uint32_t max_value)
{
    uint32_t parsed = 0;

    if (value < 0) {
        return fallback;
    }
    parsed = (uint32_t)value;
    return parsed > max_value ? max_value : parsed;
}

static uint16_t clamp_u16_or_default(int value, uint16_t fallback)
{
    if (value < 0) {
        return fallback;
    }
    if (value > 65535) {
        return 65535;
    }
    return (uint16_t)value;
}

static const char *led_chipset_text(node_led_chipset_t chipset)
{
    switch (chipset) {
    case NODE_LED_CHIPSET_WS2812:
        return "ws2812";
    case NODE_LED_CHIPSET_WS2815:
        return "ws2815";
    case NODE_LED_CHIPSET_SK6812:
        return "sk6812";
    default:
        return "ws2812";
    }
}

static const char *led_color_order_text(node_led_color_order_t color_order)
{
    switch (color_order) {
    case NODE_LED_COLOR_ORDER_RGB:
        return "rgb";
    case NODE_LED_COLOR_ORDER_RBG:
        return "rbg";
    case NODE_LED_COLOR_ORDER_GRB:
        return "grb";
    case NODE_LED_COLOR_ORDER_GBR:
        return "gbr";
    case NODE_LED_COLOR_ORDER_BRG:
        return "brg";
    case NODE_LED_COLOR_ORDER_BGR:
        return "bgr";
    default:
        return "grb";
    }
}

static void format_led_color_text(char *out,
                                  size_t out_size,
                                  uint8_t red,
                                  uint8_t green,
                                  uint8_t blue,
                                  uint8_t white)
{
    if (!out || out_size == 0) {
        return;
    }
    if (white > 0) {
        snprintf(out, out_size, "#%02x%02x%02x%02x", red, green, blue, white);
    } else {
        snprintf(out, out_size, "#%02x%02x%02x", red, green, blue);
    }
}

static bool parse_hex_byte(char high, char low, uint8_t *out)
{
    unsigned value = 0;
    if (!isxdigit((unsigned char)high) || !isxdigit((unsigned char)low) || !out) {
        return false;
    }
    if (sscanf((char[]){high, low, '\0'}, "%02x", &value) != 1) {
        return false;
    }
    *out = (uint8_t)value;
    return true;
}

static bool parse_color_text(const char *text,
                             uint8_t *out_red,
                             uint8_t *out_green,
                             uint8_t *out_blue,
                             uint8_t *out_white)
{
    const char *p = text;
    size_t len = 0;

    if (!p || !*p || !out_red || !out_green || !out_blue || !out_white) {
        return false;
    }
    if (*p == '#') {
        ++p;
    }

    len = strlen(p);
    if (len != 6 && len != 8) {
        return false;
    }
    if (!parse_hex_byte(p[0], p[1], out_red) ||
        !parse_hex_byte(p[2], p[3], out_green) ||
        !parse_hex_byte(p[4], p[5], out_blue)) {
        return false;
    }
    *out_white = 0;
    if (len == 8 && !parse_hex_byte(p[6], p[7], out_white)) {
        return false;
    }
    return true;
}

static void json_copy_led_color_field(const char *json,
                                      const char *key,
                                      uint8_t *out_red,
                                      uint8_t *out_green,
                                      uint8_t *out_blue,
                                      uint8_t *out_white)
{
    char value[16] = {0};
    if (!json_copy_string_field(json, key, value, sizeof(value))) {
        return;
    }
    (void)parse_color_text(value, out_red, out_green, out_blue, out_white);
}

static void make_led_effect_key(char *out,
                                size_t out_size,
                                size_t index,
                                const char *effect_name,
                                const char *field)
{
    snprintf(out, out_size, "led%u_effect_%s_%s", (unsigned)(index + 1), effect_name, field);
}

static esp_err_t append_effect_controls_json(char *out,
                                             size_t out_size,
                                             size_t *len,
                                             uint32_t controls)
{
    bool first = true;

#define APPEND_SCHEMA_JSON(...) do { \
        int _n = snprintf((out) + *(len), (out_size) - *(len), __VA_ARGS__); \
        if (_n < 0 || (size_t)_n >= (out_size) - *(len)) { \
            return ESP_ERR_NO_MEM; \
        } \
        *(len) += (size_t)_n; \
    } while (0)
#define APPEND_CONTROL(name_) do { \
        APPEND_SCHEMA_JSON("%s\"%s\"", first ? "" : ",", (name_)); \
        first = false; \
    } while (0)

    if (controls & NODE_LED_CTRL_FLASH_ON) {
        APPEND_CONTROL("duration_ms");
    }
    if (controls & NODE_LED_CTRL_SPEED) {
        APPEND_CONTROL("step_ms");
    }
    if (controls & NODE_LED_CTRL_FLASH_OFF) {
        APPEND_CONTROL("step_ms");
    }
    if (controls & NODE_LED_CTRL_REPEAT_COUNT) {
        APPEND_CONTROL("repeat_count");
    }
    if (controls & NODE_LED_CTRL_SIZE) {
        APPEND_CONTROL("size");
    }
    if (controls & NODE_LED_CTRL_INTENSITY) {
        APPEND_CONTROL("intensity");
    }
    if (controls & NODE_LED_CTRL_DENSITY) {
        APPEND_CONTROL("density");
    }
    if (controls & NODE_LED_CTRL_FADE) {
        APPEND_CONTROL("fade");
    }
    if (controls & NODE_LED_CTRL_PALETTE_MODE) {
        APPEND_CONTROL("palette_mode");
    }
    if (controls & NODE_LED_CTRL_PRIMARY_COLOR) {
        APPEND_CONTROL("color");
    }
    if (controls & NODE_LED_CTRL_SECONDARY_COLOR) {
        APPEND_CONTROL("secondary_color");
    }
    if (controls & NODE_LED_CTRL_BACKGROUND_COLOR) {
        APPEND_CONTROL("background_color");
    }

#undef APPEND_CONTROL
#undef APPEND_SCHEMA_JSON
    return ESP_OK;
}

static esp_err_t append_effect_defaults_json(char *out,
                                             size_t out_size,
                                             size_t *len,
                                             const node_led_effect_descriptor_t *desc)
{
    char color[16];
    char secondary_color[16];
    char background_color[16];
    bool first = true;
    const node_led_effect_defaults_t *defaults = desc ? &desc->defaults : NULL;

    if (!desc || !defaults) {
        return ESP_ERR_INVALID_ARG;
    }

#define APPEND_SCHEMA_JSON(...) do { \
        int _n = snprintf((out) + *(len), (out_size) - *(len), __VA_ARGS__); \
        if (_n < 0 || (size_t)_n >= (out_size) - *(len)) { \
            return ESP_ERR_NO_MEM; \
        } \
        *(len) += (size_t)_n; \
    } while (0)
#define APPEND_DEFAULT_NUM(key_, value_) do { \
        APPEND_SCHEMA_JSON("%s\"%s\":%lu", first ? "" : ",", (key_), (unsigned long)(value_)); \
        first = false; \
    } while (0)
#define APPEND_DEFAULT_STR(key_, value_) do { \
        APPEND_SCHEMA_JSON("%s\"%s\":\"%s\"", first ? "" : ",", (key_), (value_)); \
        first = false; \
    } while (0)

    if (desc->controls & NODE_LED_CTRL_FLASH_ON) {
        APPEND_DEFAULT_NUM("duration_ms", defaults->duration_ms);
    }
    if (desc->controls & NODE_LED_CTRL_SPEED) {
        APPEND_DEFAULT_NUM("step_ms", defaults->step_ms);
    }
    if (desc->controls & NODE_LED_CTRL_FLASH_OFF) {
        APPEND_DEFAULT_NUM("step_ms", defaults->step_ms);
    }
    if (desc->controls & NODE_LED_CTRL_REPEAT_COUNT) {
        APPEND_DEFAULT_NUM("repeat_count", defaults->repeat_count);
    }
    if (desc->controls & NODE_LED_CTRL_SIZE) {
        APPEND_DEFAULT_NUM("size", defaults->size);
    }
    if (desc->controls & NODE_LED_CTRL_INTENSITY) {
        APPEND_DEFAULT_NUM("intensity", defaults->intensity);
    }
    if (desc->controls & NODE_LED_CTRL_DENSITY) {
        APPEND_DEFAULT_NUM("density", defaults->density);
    }
    if (desc->controls & NODE_LED_CTRL_FADE) {
        APPEND_DEFAULT_NUM("fade", defaults->fade);
    }
    if (desc->controls & NODE_LED_CTRL_PALETTE_MODE) {
        APPEND_DEFAULT_STR("palette_mode", node_led_palette_mode_name(defaults->palette_mode));
    }
    if (desc->controls & NODE_LED_CTRL_PRIMARY_COLOR) {
        format_led_color_text(color,
                              sizeof(color),
                              defaults->red,
                              defaults->green,
                              defaults->blue,
                              defaults->white);
        APPEND_DEFAULT_STR("color", color);
    }
    if (desc->controls & NODE_LED_CTRL_SECONDARY_COLOR) {
        format_led_color_text(secondary_color,
                              sizeof(secondary_color),
                              defaults->red2,
                              defaults->green2,
                              defaults->blue2,
                              defaults->white2);
        APPEND_DEFAULT_STR("secondary_color", secondary_color);
    }
    if (desc->controls & NODE_LED_CTRL_BACKGROUND_COLOR) {
        format_led_color_text(background_color,
                              sizeof(background_color),
                              defaults->bg_red,
                              defaults->bg_green,
                              defaults->bg_blue,
                              defaults->bg_white);
        APPEND_DEFAULT_STR("background_color", background_color);
    }

#undef APPEND_DEFAULT_STR
#undef APPEND_DEFAULT_NUM
#undef APPEND_SCHEMA_JSON
    return ESP_OK;
}

esp_err_t node_provisioning_status_get(httpd_req_t *req)
{
    char body[480];
    int n = snprintf(body,
                     sizeof(body),
                     "{\"ok\":true,\"mode\":\"%s\",\"ap_started\":%s,\"web_started\":%s,"
                     "\"ap_ssid\":\"%s\",\"ap_password\":\"%s\","
                     "\"sta_got_ip\":%s,\"sta_disconnected\":%s,\"sta_disconnect_reason\":%u,"
                     "\"auto_close_supported\":%s,\"auto_close_running\":%s,"
                     "\"auto_close_keep_open\":%s,\"auto_close_timeout_sec\":%u,"
                     "\"auto_close_remaining_sec\":%u}",
                     g_node_prov.status.mode == NODE_PROVISIONING_MODE_AP ? "ap" : "sta",
                     g_node_prov.status.ap_started ? "true" : "false",
                     g_node_prov.status.web_started ? "true" : "false",
                     g_node_prov.status.ap_ssid,
                     g_node_prov.status.ap_password,
                     g_node_prov.status.sta_got_ip ? "true" : "false",
                     g_node_prov.status.sta_disconnected ? "true" : "false",
                     (unsigned)g_node_prov.status.sta_disconnect_reason,
                     g_node_prov.status.auto_close_supported ? "true" : "false",
                     g_node_prov.status.auto_close_running ? "true" : "false",
                     g_node_prov.status.auto_close_keep_open ? "true" : "false",
                     (unsigned)g_node_prov.status.auto_close_timeout_sec,
                     (unsigned)g_node_prov.status.auto_close_remaining_sec);
    if (n < 0 || n >= (int)sizeof(body)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "status too large");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body, (ssize_t)n);
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

esp_err_t node_provisioning_config_get(httpd_req_t *req)
{
    char node_id_json[NODE_ID_MAX_LEN * 2 + 1];
    char node_name_json[NODE_NAME_MAX_LEN * 2 + 1];
    char wifi_ssid_json[NODE_WIFI_SSID_MAX_LEN * 2 + 1];
    char controller_host_json[NODE_HOST_MAX_LEN * 2 + 1];
    char mqtt_client_id_json[NODE_MQTT_CLIENT_ID_MAX_LEN * 2 + 1];
    int n = 0;
    size_t len = 0;

    if (!lock_config_json()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config busy");
    }
    if (node_admin_control_get_config(&s_get_config) != ESP_OK) {
        unlock_config_json();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config unavailable");
    }
    if (!json_escape_string(node_id_json, sizeof(node_id_json), s_get_config.node_id) ||
        !json_escape_string(node_name_json, sizeof(node_name_json), s_get_config.node_name) ||
        !json_escape_string(wifi_ssid_json, sizeof(wifi_ssid_json), s_get_config.wifi_ssid) ||
        !json_escape_string(controller_host_json, sizeof(controller_host_json), s_get_config.controller_host) ||
        !json_escape_string(mqtt_client_id_json, sizeof(mqtt_client_id_json), s_get_config.mqtt_client_id)) {
        unlock_config_json();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config too large");
    }

    n = snprintf(s_config_json,
                 sizeof(s_config_json),
                 "{\"ok\":true,\"node_id\":\"%s\",\"node_name\":\"%s\","
                 "\"wifi_ssid\":\"%s\",\"controller_host\":\"%s\","
                 "\"mqtt_port\":%u,\"mqtt_client_id\":\"%s\","
                 "\"reset_gpio\":%d,\"pin_config_locked\":%s,"
                 "\"relays\":[",
                 node_id_json,
                 node_name_json,
                 wifi_ssid_json,
                 controller_host_json,
                 (unsigned)s_get_config.mqtt_port,
                 mqtt_client_id_json,
                 s_get_config.reset_gpio,
                 s_get_config.pin_config_locked ? "true" : "false");
    if (n < 0 || n >= (int)sizeof(s_config_json)) {
        unlock_config_json();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config too large");
    }
    len = (size_t)n;
#define APPEND_JSON(...) do { \
        int _n = snprintf(s_config_json + len, sizeof(s_config_json) - len, __VA_ARGS__); \
        if (_n < 0 || (size_t)_n >= sizeof(s_config_json) - len) { \
            unlock_config_json(); \
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config too large"); \
        } \
        len += (size_t)_n; \
    } while (0)
    for (size_t i = 0; i < NODE_RELAY_MAX; ++i) {
        const node_output_pin_config_t *p = &s_get_config.relays[i];
        char label_json[sizeof(p->label) * 2 + 1];
        if (!json_escape_string(label_json, sizeof(label_json), p->label)) {
            unlock_config_json();
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config too large");
        }
        APPEND_JSON("%s{\"enabled\":%s,\"gpio\":%d,\"active_low\":%s,\"label\":\"%s\"}",
                    i ? "," : "",
                    p->enabled ? "true" : "false",
                    p->gpio,
                    p->active_low ? "true" : "false",
                    label_json);
    }
    APPEND_JSON("],\"mosfets\":[");
    for (size_t i = 0; i < NODE_MOSFET_MAX; ++i) {
        const node_output_pin_config_t *p = &s_get_config.mosfets[i];
        char label_json[sizeof(p->label) * 2 + 1];
        if (!json_escape_string(label_json, sizeof(label_json), p->label)) {
            unlock_config_json();
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config too large");
        }
        APPEND_JSON("%s{\"enabled\":%s,\"gpio\":%d,\"active_low\":%s,\"pulse_duration_ms\":%u,\"fade_duration_ms\":%u,\"blink_on_ms\":%u,\"blink_off_ms\":%u,\"blink_repeat_count\":%u,\"breathe_fade_ms\":%u,\"breathe_hold_ms\":%u,\"breathe_repeat_count\":%u,\"default_value\":%u,\"default_target\":%u,\"default_min\":%u,\"default_max\":%u,\"default_final_value\":%u,\"label\":\"%s\"}",
                    i ? "," : "",
                    p->enabled ? "true" : "false",
                    p->gpio,
                    p->active_low ? "true" : "false",
                    (unsigned)p->pulse_duration_ms,
                    (unsigned)p->fade_duration_ms,
                    (unsigned)p->blink_on_ms,
                    (unsigned)p->blink_off_ms,
                    (unsigned)p->blink_repeat_count,
                    (unsigned)p->breathe_fade_ms,
                    (unsigned)p->breathe_hold_ms,
                    (unsigned)p->breathe_repeat_count,
                    (unsigned)p->default_value,
                    (unsigned)p->default_target,
                    (unsigned)p->default_min,
                    (unsigned)p->default_max,
                    (unsigned)p->default_final_value,
                    label_json);
    }
    APPEND_JSON("],\"universal_io\":[");
    for (size_t i = 0; i < NODE_UNIVERSAL_IO_MAX; ++i) {
        const node_universal_pin_config_t *p = &s_get_config.universal_io[i];
        char label_json[sizeof(p->label) * 2 + 1];
        if (!json_escape_string(label_json, sizeof(label_json), p->label)) {
            unlock_config_json();
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config too large");
        }
        APPEND_JSON("%s{\"enabled\":%s,\"gpio\":%d,\"role\":%d,\"active_low\":%s,\"label\":\"%s\"}",
                    i ? "," : "",
                    p->enabled ? "true" : "false",
                    p->gpio,
                    (int)p->role,
                    p->active_low ? "true" : "false",
                    label_json);
    }
    APPEND_JSON("],\"led_strips\":[");
    for (size_t i = 0; i < NODE_LED_STRIP_MAX; ++i) {
        const node_led_strip_config_t *p = &s_get_config.led_strips[i];
        char label_json[sizeof(p->label) * 2 + 1];
        if (!json_escape_string(label_json, sizeof(label_json), p->label)) {
            unlock_config_json();
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config too large");
        }
        APPEND_JSON("%s{\"enabled\":%s,\"gpio\":%d,\"pixel_count\":%u,\"chipset\":\"%s\",\"color_order\":\"%s\",\"rgbw\":%s,\"label\":\"%s\"}",
                    i ? "," : "",
                    p->enabled ? "true" : "false",
                    p->gpio,
                    (unsigned)p->pixel_count,
                    led_chipset_text(p->chipset),
                    led_color_order_text(p->color_order),
                    p->rgbw ? "true" : "false",
                    label_json);
    }
    APPEND_JSON("]}");
#undef APPEND_JSON
    httpd_resp_set_type(req, "application/json");
    esp_err_t resp = httpd_resp_send(req, s_config_json, (ssize_t)len);
    unlock_config_json();
    return resp;
}

esp_err_t node_provisioning_led_config_get(httpd_req_t *req)
{
    int n = 0;
    size_t len = 0;

    if (!lock_config_json()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config busy");
    }
    if (node_admin_control_get_config(&s_get_config) != ESP_OK) {
        unlock_config_json();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config unavailable");
    }

    n = snprintf(s_config_json, sizeof(s_config_json), "{\"ok\":true,\"led_strips\":[");
    if (n < 0 || n >= (int)sizeof(s_config_json)) {
        unlock_config_json();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "led config too large");
    }
    len = (size_t)n;
#define APPEND_LED_JSON(...) do { \
        int _n = snprintf(s_config_json + len, sizeof(s_config_json) - len, __VA_ARGS__); \
        if (_n < 0 || (size_t)_n >= sizeof(s_config_json) - len) { \
            unlock_config_json(); \
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "led config too large"); \
        } \
        len += (size_t)_n; \
    } while (0)
    for (size_t i = 0; i < NODE_LED_STRIP_MAX; ++i) {
        const node_led_strip_config_t *p = &s_get_config.led_strips[i];
        char blink_color[16];
        char breathe_color[16];
        format_led_color_text(blink_color, sizeof(blink_color), p->blink.red, p->blink.green, p->blink.blue, p->blink.white);
        format_led_color_text(breathe_color, sizeof(breathe_color), p->breathe.red, p->breathe.green, p->breathe.blue, p->breathe.white);
        APPEND_LED_JSON("%s{\"blink\":{\"on_ms\":%u,\"off_ms\":%u,\"repeat_count\":%u,\"color\":\"%s\"},\"breathe\":{\"cycle_ms\":%u,\"step_ms\":%u,\"repeat_count\":%u,\"color\":\"%s\"},\"effects\":{",
                        i ? "," : "",
                        (unsigned)p->blink.on_ms,
                        (unsigned)p->blink.off_ms,
                        (unsigned)p->blink.repeat_count,
                        blink_color,
                        (unsigned)p->breathe.cycle_ms,
                        (unsigned)p->breathe.step_ms,
                        (unsigned)p->breathe.repeat_count,
                        breathe_color);
        bool first_effect = true;
        for (size_t effect_index = 0; effect_index < node_led_effect_descriptor_count(); ++effect_index) {
            const node_led_effect_descriptor_t *desc = &node_led_effect_descriptors()[effect_index];
            const node_led_effect_preset_t *effect = &p->effects.items[desc->id];
            char color[16];
            char secondary_color[16];
            char background_color[16];
            if (!node_led_effect_is_advanced(desc->id)) {
                continue;
            }
            format_led_color_text(color, sizeof(color), effect->red, effect->green, effect->blue, effect->white);
            format_led_color_text(secondary_color, sizeof(secondary_color), effect->red2, effect->green2, effect->blue2, effect->white2);
            format_led_color_text(background_color, sizeof(background_color), effect->bg_red, effect->bg_green, effect->bg_blue, effect->bg_white);
            APPEND_LED_JSON("%s\"%s\":{\"duration_ms\":%u,\"step_ms\":%u,\"repeat_count\":%u,\"size\":%u,\"intensity\":%u,\"density\":%u,\"fade\":%u,\"palette_mode\":\"%s\",\"color\":\"%s\",\"secondary_color\":\"%s\",\"background_color\":\"%s\"}",
                            first_effect ? "" : ",",
                            desc->name,
                            (unsigned)effect->duration_ms,
                            (unsigned)effect->step_ms,
                            (unsigned)effect->repeat_count,
                            (unsigned)effect->size,
                            (unsigned)effect->intensity,
                            (unsigned)effect->density,
                            (unsigned)effect->fade,
                            node_led_palette_mode_name(effect->palette_mode),
                            color,
                            secondary_color,
                            background_color);
            first_effect = false;
        }
        APPEND_LED_JSON("}}");
    }
    APPEND_LED_JSON("]}");
#undef APPEND_LED_JSON
    httpd_resp_set_type(req, "application/json");
    esp_err_t resp = httpd_resp_send(req, s_config_json, (ssize_t)len);
    unlock_config_json();
    return resp;
}

esp_err_t node_provisioning_led_effects_schema_get(httpd_req_t *req)
{
    const node_led_effect_defaults_t *blink_defaults = node_led_effect_defaults(NODE_LED_EFFECT_BLINK);
    const node_led_effect_defaults_t *breathe_defaults = node_led_effect_defaults(NODE_LED_EFFECT_BREATHE);
    bool first_effect = true;
    int n = 0;
    size_t len = 0;

    if (!lock_config_json()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "schema busy");
    }

    n = snprintf(s_config_json,
                 sizeof(s_config_json),
                 "{\"ok\":true,\"blink_defaults\":{\"on_ms\":%lu,\"off_ms\":%lu,\"repeat_count\":%u,\"color\":\"#%02x%02x%02x\"},"
                 "\"breathe_defaults\":{\"cycle_ms\":%lu,\"step_ms\":%lu,\"repeat_count\":%u,\"color\":\"#%02x%02x%02x\"},"
                 "\"groups\":[",
                 blink_defaults ? (unsigned long)blink_defaults->duration_ms : 500UL,
                 blink_defaults ? (unsigned long)blink_defaults->step_ms : 500UL,
                 blink_defaults ? (unsigned)blink_defaults->repeat_count : 0U,
                 blink_defaults ? (unsigned)blink_defaults->red : 255U,
                 blink_defaults ? (unsigned)blink_defaults->green : 255U,
                 blink_defaults ? (unsigned)blink_defaults->blue : 255U,
                 breathe_defaults ? (unsigned long)breathe_defaults->duration_ms : 2000UL,
                 breathe_defaults ? (unsigned long)breathe_defaults->step_ms : 40UL,
                 breathe_defaults ? (unsigned)breathe_defaults->repeat_count : 0U,
                 breathe_defaults ? (unsigned)breathe_defaults->red : 255U,
                 breathe_defaults ? (unsigned)breathe_defaults->green : 255U,
                 breathe_defaults ? (unsigned)breathe_defaults->blue : 255U);
    if (n < 0 || n >= (int)sizeof(s_config_json)) {
        unlock_config_json();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "schema too large");
    }
    len = (size_t)n;
#define APPEND_SCHEMA_JSON(...) do { \
        int _n = snprintf(s_config_json + len, sizeof(s_config_json) - len, __VA_ARGS__); \
        if (_n < 0 || (size_t)_n >= sizeof(s_config_json) - len) { \
            unlock_config_json(); \
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "schema too large"); \
        } \
        len += (size_t)_n; \
    } while (0)

    for (size_t i = 0; i < node_led_effect_group_count(); ++i) {
        char group_json[64];
        if (!json_escape_string(group_json, sizeof(group_json), node_led_effect_group_name(i))) {
            unlock_config_json();
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "schema too large");
        }
        APPEND_SCHEMA_JSON("%s\"%s\"", i ? "," : "", group_json);
    }

    APPEND_SCHEMA_JSON("],\"effects\":[");
    for (size_t i = 0; i < node_led_effect_descriptor_count(); ++i) {
        const node_led_effect_descriptor_t *desc = &node_led_effect_descriptors()[i];
        char name_json[64];
        char label_json[128];
        char group_json[64];
        char note_json[256];
        if (!node_led_effect_is_advanced(desc->id)) {
            continue;
        }
        if (!json_escape_string(name_json, sizeof(name_json), desc->name) ||
            !json_escape_string(label_json, sizeof(label_json), desc->label) ||
            !json_escape_string(group_json, sizeof(group_json), desc->group ? desc->group : "Other") ||
            !json_escape_string(note_json, sizeof(note_json), desc->note ? desc->note : "")) {
            unlock_config_json();
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "schema too large");
        }
        APPEND_SCHEMA_JSON("%s{\"name\":\"%s\",\"label\":\"%s\",\"group\":\"%s\",\"note\":\"%s\",\"controls\":[",
                           first_effect ? "" : ",",
                           name_json,
                           label_json,
                           group_json,
                           note_json);
        if (append_effect_controls_json(s_config_json, sizeof(s_config_json), &len, desc->controls) != ESP_OK) {
            unlock_config_json();
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "schema too large");
        }
        APPEND_SCHEMA_JSON("],\"defaults\":{");
        if (append_effect_defaults_json(s_config_json, sizeof(s_config_json), &len, desc) != ESP_OK) {
            unlock_config_json();
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "schema too large");
        }
        APPEND_SCHEMA_JSON("}}");
        first_effect = false;
    }
    APPEND_SCHEMA_JSON("]}");
#undef APPEND_SCHEMA_JSON

    httpd_resp_set_type(req, "application/json");
    esp_err_t resp = httpd_resp_send(req, s_config_json, (ssize_t)len);
    unlock_config_json();
    return resp;
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

static void json_copy_led_chipset_field(const char *json, const char *key, node_led_chipset_t *out)
{
    char value[16];
    if (!out || !json_copy_string_field(json, key, value, sizeof(value))) {
        return;
    }
    if (strcmp(value, "ws2812") == 0) {
        *out = NODE_LED_CHIPSET_WS2812;
    } else if (strcmp(value, "ws2815") == 0) {
        *out = NODE_LED_CHIPSET_WS2815;
    } else if (strcmp(value, "sk6812") == 0) {
        *out = NODE_LED_CHIPSET_SK6812;
    }
}

static void json_copy_led_color_order_field(const char *json, const char *key, node_led_color_order_t *out)
{
    char value[16];
    if (!out || !json_copy_string_field(json, key, value, sizeof(value))) {
        return;
    }
    if (strcmp(value, "rgb") == 0) {
        *out = NODE_LED_COLOR_ORDER_RGB;
    } else if (strcmp(value, "rbg") == 0) {
        *out = NODE_LED_COLOR_ORDER_RBG;
    } else if (strcmp(value, "grb") == 0) {
        *out = NODE_LED_COLOR_ORDER_GRB;
    } else if (strcmp(value, "gbr") == 0) {
        *out = NODE_LED_COLOR_ORDER_GBR;
    } else if (strcmp(value, "brg") == 0) {
        *out = NODE_LED_COLOR_ORDER_BRG;
    } else if (strcmp(value, "bgr") == 0) {
        *out = NODE_LED_COLOR_ORDER_BGR;
    }
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

static void apply_mosfet_fields(const char *body, node_output_pin_config_t *pins, size_t count)
{
    char key[48];
    for (size_t i = 0; i < count; ++i) {
        node_output_pin_config_t *pin = &pins[i];
        int value = 0;
        make_indexed_key(key, sizeof(key), "mosfet", i, "pulse_duration_ms");
        value = (int)pin->pulse_duration_ms;
        json_copy_int_field(body, key, &value);
        pin->pulse_duration_ms = value > 0 ? (uint32_t)value : pin->pulse_duration_ms;
        make_indexed_key(key, sizeof(key), "mosfet", i, "fade_duration_ms");
        value = (int)pin->fade_duration_ms;
        json_copy_int_field(body, key, &value);
        pin->fade_duration_ms = value > 0 ? (uint32_t)value : pin->fade_duration_ms;
        make_indexed_key(key, sizeof(key), "mosfet", i, "blink_on_ms");
        value = (int)pin->blink_on_ms;
        json_copy_int_field(body, key, &value);
        pin->blink_on_ms = value > 0 ? (uint32_t)value : pin->blink_on_ms;
        make_indexed_key(key, sizeof(key), "mosfet", i, "blink_off_ms");
        value = (int)pin->blink_off_ms;
        json_copy_int_field(body, key, &value);
        pin->blink_off_ms = value > 0 ? (uint32_t)value : pin->blink_off_ms;
        make_indexed_key(key, sizeof(key), "mosfet", i, "blink_repeat_count");
        value = (int)pin->blink_repeat_count;
        json_copy_int_field(body, key, &value);
        pin->blink_repeat_count = value > 0 ? (uint16_t)value : pin->blink_repeat_count;
        make_indexed_key(key, sizeof(key), "mosfet", i, "breathe_fade_ms");
        value = (int)pin->breathe_fade_ms;
        json_copy_int_field(body, key, &value);
        pin->breathe_fade_ms = value > 0 ? (uint32_t)value : pin->breathe_fade_ms;
        make_indexed_key(key, sizeof(key), "mosfet", i, "breathe_hold_ms");
        value = (int)pin->breathe_hold_ms;
        json_copy_int_field(body, key, &value);
        pin->breathe_hold_ms = value >= 0 ? (uint32_t)value : pin->breathe_hold_ms;
        make_indexed_key(key, sizeof(key), "mosfet", i, "breathe_repeat_count");
        value = (int)pin->breathe_repeat_count;
        json_copy_int_field(body, key, &value);
        pin->breathe_repeat_count = value > 0 ? (uint16_t)value : pin->breathe_repeat_count;
        make_indexed_key(key, sizeof(key), "mosfet", i, "default_value");
        value = (int)pin->default_value;
        json_copy_int_field(body, key, &value);
        if (value >= 0 && value <= 255) pin->default_value = (uint8_t)value;
        make_indexed_key(key, sizeof(key), "mosfet", i, "default_target");
        value = (int)pin->default_target;
        json_copy_int_field(body, key, &value);
        if (value >= 0 && value <= 255) pin->default_target = (uint8_t)value;
        make_indexed_key(key, sizeof(key), "mosfet", i, "default_min");
        value = (int)pin->default_min;
        json_copy_int_field(body, key, &value);
        if (value >= 0 && value <= 255) pin->default_min = (uint8_t)value;
        make_indexed_key(key, sizeof(key), "mosfet", i, "default_max");
        value = (int)pin->default_max;
        json_copy_int_field(body, key, &value);
        if (value >= 0 && value <= 255) pin->default_max = (uint8_t)value;
        make_indexed_key(key, sizeof(key), "mosfet", i, "default_final_value");
        value = (int)pin->default_final_value;
        json_copy_int_field(body, key, &value);
        if (value >= 0 && value <= 255) pin->default_final_value = (uint8_t)value;
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
        ESP_LOGI(TAG,
                 "config io%u requested enabled=%d gpio=%d role=%d label=%s",
                 (unsigned)pin->channel,
                 pin->enabled,
                 pin->gpio,
                 (int)pin->role,
                 pin->label);
        if (!node_board_gpio_is_allowed(pin->gpio) || pin->role == NODE_PIN_DISABLED) {
            pin->enabled = false;
            pin->gpio = -1;
        }
    }
}

static void apply_led_editor_fields(const char *body, node_led_strip_config_t *pins, size_t count)
{
    char key[64];
    for (size_t i = 0; i < count; ++i) {
        node_led_strip_config_t *pin = &pins[i];
        int blink_on_ms = (int)pin->blink.on_ms;
        int blink_off_ms = (int)pin->blink.off_ms;
        int blink_repeat_count = (int)pin->blink.repeat_count;
        int breathe_cycle_ms = (int)pin->breathe.cycle_ms;
        int breathe_step_ms = (int)pin->breathe.step_ms;
        int breathe_repeat_count = (int)pin->breathe.repeat_count;
        make_indexed_key(key, sizeof(key), "led", i, "blink_on_ms");
        json_copy_int_field(body, key, &blink_on_ms);
        make_indexed_key(key, sizeof(key), "led", i, "blink_off_ms");
        json_copy_int_field(body, key, &blink_off_ms);
        make_indexed_key(key, sizeof(key), "led", i, "blink_repeat_count");
        json_copy_int_field(body, key, &blink_repeat_count);
        make_indexed_key(key, sizeof(key), "led", i, "blink_color");
        json_copy_led_color_field(body, key, &pin->blink.red, &pin->blink.green, &pin->blink.blue, &pin->blink.white);
        make_indexed_key(key, sizeof(key), "led", i, "breathe_cycle_ms");
        json_copy_int_field(body, key, &breathe_cycle_ms);
        make_indexed_key(key, sizeof(key), "led", i, "breathe_step_ms");
        json_copy_int_field(body, key, &breathe_step_ms);
        make_indexed_key(key, sizeof(key), "led", i, "breathe_repeat_count");
        json_copy_int_field(body, key, &breathe_repeat_count);
        make_indexed_key(key, sizeof(key), "led", i, "breathe_color");
        json_copy_led_color_field(body, key, &pin->breathe.red, &pin->breathe.green, &pin->breathe.blue, &pin->breathe.white);
        pin->blink.on_ms = blink_on_ms > 0
                               ? clamp_u32_or_default(blink_on_ms, 500U, NODE_PROV_LED_EDITOR_MAX_TIMING_MS)
                               : 500U;
        pin->blink.off_ms = blink_off_ms > 0
                                ? clamp_u32_or_default(blink_off_ms, 500U, NODE_PROV_LED_EDITOR_MAX_TIMING_MS)
                                : 500U;
        pin->blink.repeat_count = clamp_u16_or_default(blink_repeat_count, pin->blink.repeat_count);
        pin->breathe.cycle_ms = breathe_cycle_ms > 0
                                    ? clamp_u32_or_default(breathe_cycle_ms, 2000U, NODE_PROV_LED_EDITOR_MAX_TIMING_MS)
                                    : 2000U;
        pin->breathe.step_ms = breathe_step_ms > 0
                                   ? clamp_u32_or_default(breathe_step_ms, 40U, NODE_PROV_LED_EDITOR_MAX_TIMING_MS)
                                   : 40U;
        pin->breathe.repeat_count = clamp_u16_or_default(breathe_repeat_count, pin->breathe.repeat_count);
        for (size_t effect_index = 0; effect_index < node_led_effect_descriptor_count(); ++effect_index) {
            const node_led_effect_descriptor_t *desc = &node_led_effect_descriptors()[effect_index];
            node_led_effect_preset_t *effect = &pin->effects.items[desc->id];
            int duration_ms = (int)effect->duration_ms;
            int step_ms = (int)effect->step_ms;
            int repeat_count = (int)effect->repeat_count;
            int size = (int)effect->size;
            int intensity = (int)effect->intensity;
            int density = (int)effect->density;
            int fade = (int)effect->fade;
            char palette_mode[16] = {0};
            char color_value[16] = {0};
            uint8_t legacy_red = 0;
            uint8_t legacy_green = 0;
            uint8_t legacy_blue = 0;
            uint8_t legacy_white = 0;
            bool has_secondary = false;
            bool has_background = false;
            if (!node_led_effect_is_advanced(desc->id)) {
                continue;
            }
            snprintf(palette_mode, sizeof(palette_mode), "%s", node_led_palette_mode_name(effect->palette_mode));
            make_led_effect_key(key, sizeof(key), i, desc->name, "duration_ms");
            json_copy_int_field(body, key, &duration_ms);
            make_led_effect_key(key, sizeof(key), i, desc->name, "step_ms");
            json_copy_int_field(body, key, &step_ms);
            make_led_effect_key(key, sizeof(key), i, desc->name, "repeat_count");
            json_copy_int_field(body, key, &repeat_count);
            make_led_effect_key(key, sizeof(key), i, desc->name, "size");
            json_copy_int_field(body, key, &size);
            make_led_effect_key(key, sizeof(key), i, desc->name, "intensity");
            json_copy_int_field(body, key, &intensity);
            make_led_effect_key(key, sizeof(key), i, desc->name, "density");
            json_copy_int_field(body, key, &density);
            make_led_effect_key(key, sizeof(key), i, desc->name, "fade");
            json_copy_int_field(body, key, &fade);
            make_led_effect_key(key, sizeof(key), i, desc->name, "palette_mode");
            json_copy_string_field(body, key, palette_mode, sizeof(palette_mode));
            make_led_effect_key(key, sizeof(key), i, desc->name, "color");
            json_copy_led_color_field(body, key, &effect->red, &effect->green, &effect->blue, &effect->white);
            make_led_effect_key(key, sizeof(key), i, desc->name, "secondary_color");
            has_secondary = json_copy_string_field(body, key, color_value, sizeof(color_value));
            if (has_secondary) {
                (void)parse_color_text(color_value, &effect->red2, &effect->green2, &effect->blue2, &effect->white2);
            }
            make_led_effect_key(key, sizeof(key), i, desc->name, "background_color");
            has_background = json_copy_string_field(body, key, color_value, sizeof(color_value));
            if (has_background) {
                (void)parse_color_text(color_value, &effect->bg_red, &effect->bg_green, &effect->bg_blue, &effect->bg_white);
            }
            make_led_effect_key(key, sizeof(key), i, desc->name, "color2");
            if (!has_secondary &&
                !has_background &&
                json_copy_string_field(body, key, color_value, sizeof(color_value)) &&
                parse_color_text(color_value, &legacy_red, &legacy_green, &legacy_blue, &legacy_white)) {
                if (desc->controls & NODE_LED_CTRL_SECONDARY_COLOR) {
                    effect->red2 = legacy_red;
                    effect->green2 = legacy_green;
                    effect->blue2 = legacy_blue;
                    effect->white2 = legacy_white;
                }
                if ((desc->controls & NODE_LED_CTRL_BACKGROUND_COLOR) &&
                    !(desc->controls & NODE_LED_CTRL_SECONDARY_COLOR)) {
                    effect->bg_red = legacy_red;
                    effect->bg_green = legacy_green;
                    effect->bg_blue = legacy_blue;
                    effect->bg_white = legacy_white;
                }
            }
            effect->duration_ms = clamp_u32_or_default(duration_ms,
                                                       effect->duration_ms,
                                                       NODE_PROV_LED_EDITOR_MAX_TIMING_MS);
            effect->step_ms = clamp_u32_or_default(step_ms,
                                                   effect->step_ms,
                                                   NODE_PROV_LED_EDITOR_MAX_TIMING_MS);
            effect->repeat_count = clamp_u16_or_default(repeat_count, effect->repeat_count);
            effect->size = size > 0 ? clamp_u16_or_default(size, effect->size) : effect->size;
            effect->intensity = clamp_u16_or_default(intensity, effect->intensity);
            effect->density = clamp_u16_or_default(density, effect->density);
            effect->fade = clamp_u16_or_default(fade, effect->fade);
            effect->palette_mode = node_led_palette_mode_from_name(palette_mode);
        }
    }
}

static void apply_led_fields(const char *body, node_led_strip_config_t *pins, size_t count)
{
    char key[64];
    for (size_t i = 0; i < count; ++i) {
        node_led_strip_config_t *pin = &pins[i];
        int pixels = pin->pixel_count;
        make_indexed_key(key, sizeof(key), "led", i, "enabled");
        json_copy_bool_field(body, key, &pin->enabled);
        make_indexed_key(key, sizeof(key), "led", i, "gpio");
        json_copy_int_field(body, key, &pin->gpio);
        make_indexed_key(key, sizeof(key), "led", i, "pixel_count");
        json_copy_int_field(body, key, &pixels);
        make_indexed_key(key, sizeof(key), "led", i, "chipset");
        json_copy_led_chipset_field(body, key, &pin->chipset);
        make_indexed_key(key, sizeof(key), "led", i, "color_order");
        json_copy_led_color_order_field(body, key, &pin->color_order);
        make_indexed_key(key, sizeof(key), "led", i, "rgbw");
        json_copy_bool_field(body, key, &pin->rgbw);
        make_indexed_key(key, sizeof(key), "led", i, "label");
        json_copy_string_field(body, key, pin->label, sizeof(pin->label));
        pin->pixel_count = pixels > 0 ? (uint16_t)pixels : 30;
        ESP_LOGI(TAG,
                 "config led%u requested enabled=%d gpio=%d pixels=%u chipset=%s color_order=%s rgbw=%d label=%s",
                 (unsigned)pin->channel,
                 pin->enabled,
                 pin->gpio,
                 (unsigned)pin->pixel_count,
                 led_chipset_text(pin->chipset),
                 led_color_order_text(pin->color_order),
                 pin->rgbw,
                 pin->label);
        if (!node_board_gpio_is_allowed(pin->gpio)) {
            pin->enabled = false;
            pin->gpio = -1;
        }
    }
    apply_led_editor_fields(body, pins, count);
}

esp_err_t node_provisioning_config_post(httpd_req_t *req)
{
    enum { MAX_BODY = 10240 };
    if (req->content_len <= 0 || req->content_len > MAX_BODY) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body size");
    }
    if (!lock_post_body()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config busy");
    }

    if (!read_request_body(req, s_post_body, sizeof(s_post_body))) {
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "body read failed");
    }

    if (node_admin_control_get_config(&s_next_config) != ESP_OK) {
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config unavailable");
    }
    json_copy_string_field(s_post_body, "node_id", s_next_config.node_id, sizeof(s_next_config.node_id));
    json_copy_string_field(s_post_body, "node_name", s_next_config.node_name, sizeof(s_next_config.node_name));
    json_copy_string_field(s_post_body, "wifi_ssid", s_next_config.wifi_ssid, sizeof(s_next_config.wifi_ssid));
    json_copy_nonempty_string_field(s_post_body, "wifi_password", s_next_config.wifi_password, sizeof(s_next_config.wifi_password));
    json_copy_string_field(s_post_body, "controller_host", s_next_config.controller_host, sizeof(s_next_config.controller_host));
    json_copy_string_field(s_post_body, "mqtt_client_id", s_next_config.mqtt_client_id, sizeof(s_next_config.mqtt_client_id));
    json_copy_u16_field(s_post_body, "mqtt_port", &s_next_config.mqtt_port);
    json_copy_int_field(s_post_body, "reset_gpio", &s_next_config.reset_gpio);
    if (!s_next_config.pin_config_locked) {
        apply_output_pin_fields(s_post_body, "relay", s_next_config.relays, NODE_RELAY_MAX);
        apply_output_pin_fields(s_post_body, "mosfet", s_next_config.mosfets, NODE_MOSFET_MAX);
        apply_mosfet_fields(s_post_body, s_next_config.mosfets, NODE_MOSFET_MAX);
        apply_universal_io_fields(s_post_body, s_next_config.universal_io, NODE_UNIVERSAL_IO_MAX);
        apply_led_fields(s_post_body, s_next_config.led_strips, NODE_LED_STRIP_MAX);
        if (node_board_sanitize_pin_config(&s_next_config)) {
            ESP_LOGW(TAG, "base config sanitized before save");
        }
    }
    if (s_next_config.mqtt_port == 0) {
        s_next_config.mqtt_port = 1883;
    }
    if (s_next_config.node_id[0] == '\0' || s_next_config.mqtt_client_id[0] == '\0') {
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "node_id and mqtt_client_id required");
    }

    node_admin_control_result_t admin_result = {0};
    esp_err_t err = node_admin_control_save_base(&s_next_config, &admin_result);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config save failed: %s", esp_err_to_name(err));
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
    }
    unlock_post_body();
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"restart_required\":true}");
}

esp_err_t node_provisioning_led_config_post(httpd_req_t *req)
{
    enum { MAX_BODY = 10240 };
    if (req->content_len <= 0 || req->content_len > MAX_BODY) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body size");
    }
    if (!lock_post_body()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config busy");
    }

    if (!read_request_body(req, s_post_body, sizeof(s_post_body))) {
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "body read failed");
    }

    if (node_admin_control_get_config(&s_next_config) != ESP_OK) {
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config unavailable");
    }
    apply_led_editor_fields(s_post_body, s_next_config.led_strips, NODE_LED_STRIP_MAX);

    node_admin_control_result_t admin_result = {0};
    esp_err_t err = node_admin_control_save_led(s_next_config.led_strips, NODE_LED_STRIP_MAX, &admin_result);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led config save failed: %s", esp_err_to_name(err));
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
    }
    unlock_post_body();
    if (!admin_result.applied) {
        ESP_LOGE(TAG, "led config apply failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "apply failed");
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"applied\":true,\"restart_required\":false}");
}

esp_err_t node_provisioning_led_preview_post(httpd_req_t *req)
{
    enum { MAX_BODY = 10240 };
    char command[32] = {0};
    node_control_command_t control_command = {0};
    esp_err_t err = ESP_OK;

    if (req->content_len <= 0 || req->content_len > MAX_BODY) {
        return send_preview_json(req, false, "rejected", "invalid_body_size", "");
    }
    if (!lock_post_body()) {
        return send_preview_json(req, false, "failed", "config_busy", "");
    }
    if (!read_request_body(req, s_post_body, sizeof(s_post_body))) {
        unlock_post_body();
        return send_preview_json(req, false, "failed", "body_read_failed", "");
    }
    if (!json_copy_string_field(s_post_body, "command", command, sizeof(command)) ||
        !led_preview_command_allowed(command)) {
        unlock_post_body();
        return send_preview_json(req, false, "rejected", "invalid_command", command);
    }

    ESP_LOGI(TAG, "led preview command=%s", command);
    control_command.request_id = "led_preview";
    control_command.command = command;
    control_command.args_json = s_post_body;
    control_command.source = NODE_CONTROL_SOURCE_LOCAL_PREVIEW;
    memset(&s_preview_result, 0, sizeof(s_preview_result));
    err = node_control_execute(&control_command, &s_preview_result);
    unlock_post_body();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led preview execute failed: %s", esp_err_to_name(err));
        if (s_preview_result.status[0] == '\0') {
            snprintf(s_preview_result.status, sizeof(s_preview_result.status), "%s", "failed");
        }
        if (s_preview_result.error_code[0] == '\0') {
            snprintf(s_preview_result.error_code, sizeof(s_preview_result.error_code), "%s", "preview_failed");
        }
        return send_preview_json(req,
                                 false,
                                 s_preview_result.status,
                                 s_preview_result.error_code,
                                 command);
    }

    return send_preview_json(req,
                             preview_status_ok(s_preview_result.status),
                             s_preview_result.status[0] ? s_preview_result.status : "failed",
                             s_preview_result.error_code,
                             command);
}

esp_err_t node_provisioning_restart_post(httpd_req_t *req)
{
    node_admin_control_result_t admin_result = {0};
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
        ESP_LOGE(TAG, "wifi reset failed: %s", esp_err_to_name(err));
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
        ESP_LOGE(TAG, "factory reset failed: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "factory reset failed");
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"restart_required\":true}");
}
