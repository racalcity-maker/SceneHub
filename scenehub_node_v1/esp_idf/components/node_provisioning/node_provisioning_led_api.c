#include "node_provisioning_config_api_internal.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

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

esp_err_t node_provisioning_led_config_get(httpd_req_t *req)
{
    int n = 0;
    size_t len = 0;

    if (!ensure_provisioning_scratch()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config no mem");
    }
    if (!lock_config_json()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config busy");
    }
    if (!ensure_config_json_buffer()) {
        unlock_config_json();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config no mem");
    }
    if (node_admin_control_get_config(&s_get_config) != ESP_OK) {
        unlock_config_json();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config unavailable");
    }

    n = snprintf(s_config_json, NODE_PROVISIONING_CONFIG_JSON_CAPACITY, "{\"ok\":true,\"led_strips\":[");
    if (n < 0 || n >= NODE_PROVISIONING_CONFIG_JSON_CAPACITY) {
        unlock_config_json();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "led config too large");
    }
    len = (size_t)n;
#define APPEND_LED_JSON(...) do { \
        int _n = snprintf(s_config_json + len, NODE_PROVISIONING_CONFIG_JSON_CAPACITY - len, __VA_ARGS__); \
        if (_n < 0 || (size_t)_n >= NODE_PROVISIONING_CONFIG_JSON_CAPACITY - len) { \
            unlock_config_json(); \
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "led config too large"); \
        } \
        len += (size_t)_n; \
    } while (0)
    for (size_t i = 0; i < NODE_LED_STRIP_MAX; ++i) {
        const node_led_strip_config_t *p = &s_get_config.led_strips[i];
        char blink_color[16];
        char breathe_color[16];
        format_led_color_text(blink_color,
                              sizeof(blink_color),
                              p->blink.red,
                              p->blink.green,
                              p->blink.blue,
                              p->blink.white);
        format_led_color_text(breathe_color,
                              sizeof(breathe_color),
                              p->breathe.red,
                              p->breathe.green,
                              p->breathe.blue,
                              p->breathe.white);
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
            format_led_color_text(secondary_color,
                                  sizeof(secondary_color),
                                  effect->red2,
                                  effect->green2,
                                  effect->blue2,
                                  effect->white2);
            format_led_color_text(background_color,
                                  sizeof(background_color),
                                  effect->bg_red,
                                  effect->bg_green,
                                  effect->bg_blue,
                                  effect->bg_white);
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
    if (!ensure_config_json_buffer()) {
        unlock_config_json();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "schema no mem");
    }

    n = snprintf(s_config_json,
                 NODE_PROVISIONING_CONFIG_JSON_CAPACITY,
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
    if (n < 0 || n >= NODE_PROVISIONING_CONFIG_JSON_CAPACITY) {
        unlock_config_json();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "schema too large");
    }
    len = (size_t)n;
#define APPEND_SCHEMA_JSON(...) do { \
        int _n = snprintf(s_config_json + len, NODE_PROVISIONING_CONFIG_JSON_CAPACITY - len, __VA_ARGS__); \
        if (_n < 0 || (size_t)_n >= NODE_PROVISIONING_CONFIG_JSON_CAPACITY - len) { \
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
        if (append_effect_controls_json(s_config_json,
                                        NODE_PROVISIONING_CONFIG_JSON_CAPACITY,
                                        &len,
                                        desc->controls) != ESP_OK) {
            unlock_config_json();
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "schema too large");
        }
        APPEND_SCHEMA_JSON("],\"defaults\":{");
        if (append_effect_defaults_json(s_config_json,
                                        NODE_PROVISIONING_CONFIG_JSON_CAPACITY,
                                        &len,
                                        desc) != ESP_OK) {
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

esp_err_t node_provisioning_led_config_post(httpd_req_t *req)
{
    enum { MAX_BODY = 10240 };
    if (req->content_len <= 0 || req->content_len > MAX_BODY) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid body size");
    }
    if (!ensure_provisioning_scratch()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config no mem");
    }
    if (!lock_post_body()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config busy");
    }
    if (!ensure_post_body_buffer()) {
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "config no mem");
    }

    if (!read_request_body(req, s_post_body, NODE_PROVISIONING_POST_BODY_CAPACITY)) {
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
        ESP_LOGE(g_node_provisioning_tag, "led config save failed: %s", esp_err_to_name(err));
        unlock_post_body();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
    }
    unlock_post_body();
    if (!admin_result.applied) {
        ESP_LOGE(g_node_provisioning_tag, "led config apply failed: %s", esp_err_to_name(err));
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
    if (!ensure_provisioning_scratch()) {
        return send_preview_json(req, false, "failed", "config_no_mem", "");
    }
    if (!lock_post_body()) {
        return send_preview_json(req, false, "failed", "config_busy", "");
    }
    if (!ensure_post_body_buffer()) {
        unlock_post_body();
        return send_preview_json(req, false, "failed", "config_no_mem", "");
    }
    if (!read_request_body(req, s_post_body, NODE_PROVISIONING_POST_BODY_CAPACITY)) {
        unlock_post_body();
        return send_preview_json(req, false, "failed", "body_read_failed", "");
    }
    if (!json_copy_string_field(s_post_body, "command", command, sizeof(command)) ||
        !led_preview_command_allowed(command)) {
        unlock_post_body();
        return send_preview_json(req, false, "rejected", "invalid_command", command);
    }

    ESP_LOGI(g_node_provisioning_tag, "led preview command=%s", command);
    control_command.request_id = "led_preview";
    control_command.command = command;
    control_command.args_json = s_post_body;
    control_command.source = NODE_CONTROL_SOURCE_LOCAL_PREVIEW;
    memset(&s_preview_result, 0, sizeof(s_preview_result));
    err = node_control_submit(&control_command, &s_preview_result);
    unlock_post_body();
    if (err != ESP_OK) {
        ESP_LOGE(g_node_provisioning_tag, "led preview execute failed: %s", esp_err_to_name(err));
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
