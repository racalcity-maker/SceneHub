#include "node_control.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "node_capability.h"
#include "node_hardware_io.h"

static const char *TAG = "node_control";
static node_config_t s_config;

#define NODE_CONTROL_MAX_MOSFET_PULSE_MS 60000U
#define NODE_CONTROL_MAX_MOSFET_FADE_MS 60000U
#define NODE_CONTROL_MAX_MOSFET_EFFECT_MS 600000ULL
#define NODE_CONTROL_MAX_LED_STEP_MS 5000U
#define NODE_CONTROL_MAX_LED_DURATION_MS 60000U
#define NODE_CONTROL_MAX_LED_EFFECT_MS 600000ULL

static void copy_text(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) {
        return;
    }
    snprintf(dst, dst_size, "%s", src ? src : "");
}

static void result_done(node_control_result_t *result)
{
    copy_text(result->status, sizeof(result->status), "done");
    result->error_code[0] = '\0';
}

static esp_err_t result_rejected(node_control_result_t *result, const char *code)
{
    copy_text(result->status, sizeof(result->status), "rejected");
    copy_text(result->error_code, sizeof(result->error_code), code);
    return ESP_ERR_INVALID_ARG;
}

static bool read_int_arg(const char *json, const char *key, int *out)
{
    if (!json || !key || !out) {
        return false;
    }
    char pattern[40];
    int pn = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (pn <= 0 || pn >= (int)sizeof(pattern)) {
        return false;
    }
    const char *p = strstr(json, pattern);
    if (!p || !(p = strchr(p + pn, ':'))) {
        return false;
    }
    return sscanf(p + 1, "%d", out) == 1;
}

static bool read_bool_arg(const char *json, const char *key, bool *out)
{
    if (!json || !key || !out) {
        return false;
    }
    char pattern[40];
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
    int value = 0;
    if (sscanf(p, "%d", &value) == 1) {
        *out = value != 0;
        return true;
    }
    return false;
}

static bool read_u32_arg(const char *json, const char *key, uint32_t *out)
{
    if (!json || !key || !out) {
        return false;
    }
    char pattern[40];
    int pn = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (pn <= 0 || pn >= (int)sizeof(pattern)) {
        return false;
    }
    const char *p = strstr(json, pattern);
    unsigned value = 0;
    if (!p || !(p = strchr(p + pn, ':'))) {
        return false;
    }
    if (sscanf(p + 1, "%u", &value) != 1) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

static bool read_pwm_arg(const char *json, const char *key, uint8_t *out)
{
    int value = 0;
    if (!read_int_arg(json, key, &value) || value < 0 || value > 255 || !out) {
        return false;
    }
    *out = (uint8_t)value;
    return true;
}

static bool read_optional_pwm_arg(const char *json, const char *key, uint8_t fallback, uint8_t *out)
{
    int value = fallback;
    if (!out) {
        return false;
    }
    if (!read_int_arg(json, key, &value)) {
        *out = fallback;
        return true;
    }
    if (value < 0 || value > 255) {
        return false;
    }
    *out = (uint8_t)value;
    return true;
}

static bool read_string_arg(const char *json, const char *key, char *out, size_t out_size)
{
    if (!json || !key || !out || out_size == 0) {
        return false;
    }
    char pattern[40];
    int pn = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (pn <= 0 || pn >= (int)sizeof(pattern)) {
        return false;
    }
    const char *p = strstr(json, pattern);
    if (!p || !(p = strchr(p + pn, ':'))) {
        return false;
    }
    while (*p && *p != '\"') {
        ++p;
    }
    if (*p != '\"') {
        return false;
    }
    ++p;
    size_t i = 0;
    while (*p && *p != '\"' && i + 1 < out_size) {
        out[i++] = *p++;
    }
    if (*p != '\"') {
        return false;
    }
    out[i] = '\0';
    return i > 0;
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
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
    uint8_t white = 0;

    if (!p || !*p || !out_red || !out_green || !out_blue || !out_white) {
        return false;
    }
    if (*p == '#') {
        ++p;
    }

    size_t len = strlen(p);
    if (len != 6 && len != 8) {
        return false;
    }
    if (!parse_hex_byte(p[0], p[1], &red) ||
        !parse_hex_byte(p[2], p[3], &green) ||
        !parse_hex_byte(p[4], p[5], &blue)) {
        return false;
    }
    if (len == 8 && !parse_hex_byte(p[6], p[7], &white)) {
        return false;
    }

    *out_red = red;
    *out_green = green;
    *out_blue = blue;
    *out_white = white;
    return true;
}

static bool read_color_arg(const char *json,
                           const char *key,
                           uint8_t *out_red,
                           uint8_t *out_green,
                           uint8_t *out_blue,
                           uint8_t *out_white)
{
    char value[16] = {0};
    if (!read_string_arg(json, key, value, sizeof(value))) {
        return false;
    }
    return parse_color_text(value, out_red, out_green, out_blue, out_white);
}

static bool read_optional_color_arg(const char *json,
                                    const char *key,
                                    uint8_t fallback_red,
                                    uint8_t fallback_green,
                                    uint8_t fallback_blue,
                                    uint8_t fallback_white,
                                    uint8_t *out_red,
                                    uint8_t *out_green,
                                    uint8_t *out_blue,
                                    uint8_t *out_white)
{
    char value[16] = {0};
    if (!out_red || !out_green || !out_blue || !out_white) {
        return false;
    }
    if (!read_string_arg(json, key, value, sizeof(value))) {
        *out_red = fallback_red;
        *out_green = fallback_green;
        *out_blue = fallback_blue;
        *out_white = fallback_white;
        return true;
    }
    return parse_color_text(value, out_red, out_green, out_blue, out_white);
}

static esp_err_t execute_output_set(node_hw_output_kind_t kind,
                                    const char *args_json,
                                    node_control_result_t *result)
{
    int channel = 0;
    bool on = false;
    if (!read_int_arg(args_json, "channel", &channel)) {
        return result_rejected(result, "missing_channel");
    }
    if (!read_bool_arg(args_json, "on", &on)) {
        return result_rejected(result, "missing_on");
    }
    esp_err_t err = node_hardware_io_set_output(kind, (uint8_t)channel, on);
    if (err != ESP_OK) {
        return result_rejected(result, err == ESP_ERR_NOT_FOUND ? "not_configured" : "invalid_channel");
    }
    result_done(result);
    return ESP_OK;
}

static esp_err_t execute_output_pulse(node_hw_output_kind_t kind,
                                      const char *args_json,
                                      node_control_result_t *result)
{
    int channel = 0;
    uint32_t duration_ms = 0;
    if (!read_int_arg(args_json, "channel", &channel)) {
        return result_rejected(result, "missing_channel");
    }
    if (!read_u32_arg(args_json, "duration_ms", &duration_ms) || duration_ms == 0) {
        return result_rejected(result, "missing_duration_ms");
    }
    esp_err_t err = node_hardware_io_pulse_output(kind, (uint8_t)channel, duration_ms);
    if (err != ESP_OK) {
        return result_rejected(result, err == ESP_ERR_NOT_FOUND ? "not_configured" : "invalid_channel");
    }
    result_done(result);
    return ESP_OK;
}

static esp_err_t reject_mosfet_error(node_control_result_t *result, esp_err_t err, const char *fallback)
{
    if (err == ESP_ERR_NOT_FOUND) {
        return result_rejected(result, "not_configured");
    }
    if (err == ESP_ERR_INVALID_ARG) {
        return result_rejected(result, "invalid_args");
    }
    return result_rejected(result, fallback ? fallback : "internal_error");
}

static esp_err_t execute_mosfet_set(const char *args_json, node_control_result_t *result)
{
    int channel = 0;
    uint8_t value = 0;
    if (!read_int_arg(args_json, "channel", &channel)) {
        return result_rejected(result, "missing_channel");
    }
    if (!read_pwm_arg(args_json, "value", &value)) {
        return result_rejected(result, "missing_value");
    }
    esp_err_t err = node_hardware_io_mosfet_set((uint8_t)channel, value);
    if (err != ESP_OK) {
        return reject_mosfet_error(result, err, "mosfet_set_failed");
    }
    result_done(result);
    return ESP_OK;
}

static esp_err_t execute_mosfet_fade(const char *args_json, node_control_result_t *result)
{
    int channel = 0;
    uint8_t target = 0;
    uint32_t duration_ms = 0;
    if (!read_int_arg(args_json, "channel", &channel)) {
        return result_rejected(result, "missing_channel");
    }
    if (!read_pwm_arg(args_json, "target", &target)) {
        return result_rejected(result, "missing_target");
    }
    if (!read_u32_arg(args_json, "duration_ms", &duration_ms)) {
        return result_rejected(result, "missing_duration_ms");
    }
    if (duration_ms > NODE_CONTROL_MAX_MOSFET_FADE_MS) {
        return result_rejected(result, "invalid_args");
    }
    esp_err_t err = node_hardware_io_mosfet_fade((uint8_t)channel, target, duration_ms);
    if (err != ESP_OK) {
        return reject_mosfet_error(result, err, "mosfet_fade_failed");
    }
    result_done(result);
    return ESP_OK;
}

static esp_err_t execute_mosfet_pulse(const char *args_json, node_control_result_t *result)
{
    int channel = 0;
    uint8_t value = 0;
    uint32_t duration_ms = 0;
    if (!read_int_arg(args_json, "channel", &channel)) {
        return result_rejected(result, "missing_channel");
    }
    if (!read_pwm_arg(args_json, "value", &value)) {
        return result_rejected(result, "missing_value");
    }
    if (!read_u32_arg(args_json, "duration_ms", &duration_ms) || duration_ms == 0) {
        return result_rejected(result, "missing_duration_ms");
    }
    if (duration_ms > NODE_CONTROL_MAX_MOSFET_PULSE_MS) {
        return result_rejected(result, "invalid_args");
    }
    esp_err_t err = node_hardware_io_mosfet_pulse((uint8_t)channel, value, duration_ms);
    if (err != ESP_OK) {
        return reject_mosfet_error(result, err, "mosfet_pulse_failed");
    }
    result_done(result);
    return ESP_OK;
}

static esp_err_t execute_mosfet_blink(const char *args_json, node_control_result_t *result)
{
    int channel = 0;
    int count = 0;
    uint8_t value = 0;
    uint8_t final_value = 0;
    uint32_t on_ms = 0;
    uint32_t off_ms = 0;
    if (!read_int_arg(args_json, "channel", &channel)) {
        return result_rejected(result, "missing_channel");
    }
    if (!read_pwm_arg(args_json, "value", &value)) {
        return result_rejected(result, "missing_value");
    }
    if (!read_optional_pwm_arg(args_json, "final_value", 0, &final_value)) {
        return result_rejected(result, "invalid_args");
    }
    if (!read_u32_arg(args_json, "on_ms", &on_ms) || !read_u32_arg(args_json, "off_ms", &off_ms)) {
        return result_rejected(result, "missing_duration_ms");
    }
    if (!read_int_arg(args_json, "count", &count) || count < 0) {
        return result_rejected(result, "missing_count");
    }
    if (on_ms == 0 || off_ms == 0 ||
        (count > 0 && ((uint64_t)(on_ms + off_ms) * (uint64_t)count) > NODE_CONTROL_MAX_MOSFET_EFFECT_MS)) {
        return result_rejected(result, "invalid_args");
    }
    esp_err_t err = node_hardware_io_mosfet_blink((uint8_t)channel,
                                                  value,
                                                  on_ms,
                                                  off_ms,
                                                  (uint32_t)count,
                                                  final_value);
    if (err != ESP_OK) {
        return reject_mosfet_error(result, err, "mosfet_blink_failed");
    }
    result_done(result);
    return ESP_OK;
}

static esp_err_t execute_mosfet_breathe(const char *args_json, node_control_result_t *result)
{
    int channel = 0;
    int count = 0;
    uint8_t min_value = 0;
    uint8_t max_value = 0;
    uint8_t final_value = 0;
    uint32_t fade_ms = 0;
    uint32_t hold_ms = 0;
    if (!read_int_arg(args_json, "channel", &channel)) {
        return result_rejected(result, "missing_channel");
    }
    if (!read_pwm_arg(args_json, "min", &min_value) || !read_pwm_arg(args_json, "max", &max_value)) {
        return result_rejected(result, "missing_value");
    }
    if (!read_optional_pwm_arg(args_json, "final_value", 0, &final_value)) {
        return result_rejected(result, "invalid_args");
    }
    if (!read_u32_arg(args_json, "fade_ms", &fade_ms) || !read_u32_arg(args_json, "hold_ms", &hold_ms)) {
        return result_rejected(result, "missing_duration_ms");
    }
    if (!read_int_arg(args_json, "count", &count) || count < 0) {
        return result_rejected(result, "missing_count");
    }
    if (fade_ms == 0 || min_value > max_value ||
        (count > 0 && ((uint64_t)(fade_ms * 2U + hold_ms * 2U) * (uint64_t)count) > NODE_CONTROL_MAX_MOSFET_EFFECT_MS)) {
        return result_rejected(result, "invalid_args");
    }
    esp_err_t err = node_hardware_io_mosfet_breathe((uint8_t)channel,
                                                    min_value,
                                                    max_value,
                                                    fade_ms,
                                                    hold_ms,
                                                    (uint32_t)count,
                                                    final_value);
    if (err != ESP_OK) {
        return reject_mosfet_error(result, err, "mosfet_breathe_failed");
    }
    result_done(result);
    return ESP_OK;
}

static esp_err_t execute_mosfet_all_off(node_control_result_t *result)
{
    esp_err_t err = node_hardware_io_mosfet_all_off();
    if (err != ESP_OK) {
        return reject_mosfet_error(result, err, "mosfet_all_off_failed");
    }
    result_done(result);
    return ESP_OK;
}

static esp_err_t execute_output_all_off(node_hw_output_kind_t kind,
                                        node_control_result_t *result,
                                        const char *fallback)
{
    esp_err_t err = node_hardware_io_all_off(kind);
    if (err != ESP_OK) {
        return reject_mosfet_error(result, err, fallback);
    }
    result_done(result);
    return ESP_OK;
}

static esp_err_t execute_node_all_off(node_control_result_t *result)
{
    esp_err_t err = node_hardware_io_node_all_off();
    if (err != ESP_OK) {
        return reject_mosfet_error(result, err, "node_all_off_failed");
    }
    result_done(result);
    return ESP_OK;
}

static esp_err_t reject_led_error(node_control_result_t *result, esp_err_t err, const char *fallback)
{
    if (err == ESP_ERR_NOT_FOUND) {
        return result_rejected(result, "not_configured");
    }
    if (err == ESP_ERR_INVALID_ARG) {
        return result_rejected(result, "invalid_args");
    }
    return result_rejected(result, fallback ? fallback : "internal_error");
}

static esp_err_t execute_led_off(const char *args_json, node_control_result_t *result)
{
    int strip = 0;
    if (!read_int_arg(args_json, "channel", &strip)) {
        return result_rejected(result, "missing_channel");
    }
    esp_err_t err = node_hardware_io_led_off((uint8_t)strip);
    if (err != ESP_OK) {
        return reject_led_error(result, err, "led_off_failed");
    }
    result_done(result);
    return ESP_OK;
}

static esp_err_t execute_led_solid(const char *args_json, node_control_result_t *result)
{
    int strip = 0;
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
    uint8_t white = 0;
    uint8_t brightness = 255;

    if (!read_int_arg(args_json, "channel", &strip)) {
        return result_rejected(result, "missing_channel");
    }
    if (!read_color_arg(args_json, "color", &red, &green, &blue, &white)) {
        return result_rejected(result, "missing_color");
    }
    if (!read_optional_pwm_arg(args_json, "brightness", 255, &brightness)) {
        return result_rejected(result, "invalid_args");
    }
    esp_err_t err = node_hardware_io_led_solid((uint8_t)strip,
                                               red,
                                               green,
                                               blue,
                                               white,
                                               brightness);
    if (err != ESP_OK) {
        return reject_led_error(result, err, "led_solid_failed");
    }
    result_done(result);
    return ESP_OK;
}

static esp_err_t execute_led_effect(const char *args_json, node_control_result_t *result)
{
    int strip = 0;
    char effect[24] = {0};
    uint8_t brightness = 255;
    node_hw_led_effect_t led_effect = NODE_HW_LED_EFFECT_BLINK;
    node_hw_led_effect_config_t config = {
        .red = 255,
        .green = 255,
        .blue = 255,
        .white = 0,
        .red2 = 0,
        .green2 = 0,
        .blue2 = 0,
        .white2 = 0,
        .brightness = 255,
        .duration_ms = 250,
        .step_ms = 50,
        .count = 1,
    };

    if (!read_int_arg(args_json, "channel", &strip)) {
        return result_rejected(result, "missing_channel");
    }
    if (!read_string_arg(args_json, "effect", effect, sizeof(effect))) {
        return result_rejected(result, "missing_effect");
    }
    if (!read_optional_color_arg(args_json,
                                 "color",
                                 config.red,
                                 config.green,
                                 config.blue,
                                 config.white,
                                 &config.red,
                                 &config.green,
                                 &config.blue,
                                 &config.white)) {
        return result_rejected(result, "invalid_args");
    }
    if (!read_optional_color_arg(args_json,
                                 "color2",
                                 config.red2,
                                 config.green2,
                                 config.blue2,
                                 config.white2,
                                 &config.red2,
                                 &config.green2,
                                 &config.blue2,
                                 &config.white2)) {
        return result_rejected(result, "invalid_args");
    }
    if (!read_optional_pwm_arg(args_json, "brightness", 255, &brightness)) {
        return result_rejected(result, "invalid_args");
    }
    config.brightness = brightness;
    (void)read_u32_arg(args_json, "duration_ms", &config.duration_ms);
    (void)read_u32_arg(args_json, "step_ms", &config.step_ms);
    (void)read_u32_arg(args_json, "count", &config.count);

    if (config.duration_ms > NODE_CONTROL_MAX_LED_DURATION_MS ||
        config.step_ms > NODE_CONTROL_MAX_LED_STEP_MS ||
        config.count == 0) {
        return result_rejected(result, "invalid_args");
    }

    if (strcmp(effect, "blink") == 0) {
        led_effect = NODE_HW_LED_EFFECT_BLINK;
    } else if (strcmp(effect, "breathe") == 0) {
        led_effect = NODE_HW_LED_EFFECT_BREATHE;
        if ((uint64_t)config.duration_ms * (uint64_t)config.count > NODE_CONTROL_MAX_LED_EFFECT_MS) {
            return result_rejected(result, "invalid_args");
        }
    } else if (strcmp(effect, "rainbow") == 0) {
        led_effect = NODE_HW_LED_EFFECT_RAINBOW;
        if ((uint64_t)config.step_ms * 45ULL * (uint64_t)config.count > NODE_CONTROL_MAX_LED_EFFECT_MS) {
            return result_rejected(result, "invalid_args");
        }
    } else if (strcmp(effect, "color_wipe") == 0) {
        led_effect = NODE_HW_LED_EFFECT_COLOR_WIPE;
    } else if (strcmp(effect, "scanner") == 0) {
        led_effect = NODE_HW_LED_EFFECT_SCANNER;
    } else if (strcmp(effect, "theater") == 0) {
        led_effect = NODE_HW_LED_EFFECT_THEATER;
    } else if (strcmp(effect, "strobe") == 0) {
        led_effect = NODE_HW_LED_EFFECT_STROBE;
    } else {
        return result_rejected(result, "invalid_args");
    }

    if ((strcmp(effect, "blink") == 0 || strcmp(effect, "strobe") == 0) &&
        ((uint64_t)(config.duration_ms + config.step_ms) * (uint64_t)config.count) > NODE_CONTROL_MAX_LED_EFFECT_MS) {
        return result_rejected(result, "invalid_args");
    }

    esp_err_t err = node_hardware_io_led_run_effect((uint8_t)strip, led_effect, &config);
    if (err != ESP_OK) {
        return reject_led_error(result, err, "led_effect_failed");
    }
    result_done(result);
    return ESP_OK;
}

static esp_err_t execute_mosfet_effect_alias(const char *args_json, node_control_result_t *result)
{
    int channel = 0;
    uint32_t duration_ms = 250;
    uint32_t repeat = 1;
    uint8_t value = 255;
    uint8_t target = 255;
    uint8_t min_value = 0;
    uint8_t max_value = 255;
    uint8_t final_value = 0;
    bool on = true;
    char effect[24] = {0};

    if (!read_int_arg(args_json, "channel", &channel)) {
        return result_rejected(result, "missing_channel");
    }
    if (!read_string_arg(args_json, "effect", effect, sizeof(effect))) {
        return result_rejected(result, "missing_effect");
    }
    (void)read_u32_arg(args_json, "duration_ms", &duration_ms);
    (void)read_u32_arg(args_json, "repeat", &repeat);
    (void)read_optional_pwm_arg(args_json, "value", value, &value);
    (void)read_optional_pwm_arg(args_json, "target", target, &target);
    (void)read_optional_pwm_arg(args_json, "min", min_value, &min_value);
    (void)read_optional_pwm_arg(args_json, "max", max_value, &max_value);
    (void)read_optional_pwm_arg(args_json, "final_value", final_value, &final_value);

    if (strcmp(effect, "set") == 0) {
        if (!read_pwm_arg(args_json, "value", &value)) {
            if (!read_bool_arg(args_json, "on", &on)) {
                return result_rejected(result, "missing_value");
            }
            value = on ? 255 : 0;
        }
        esp_err_t err = node_hardware_io_mosfet_set((uint8_t)channel, value);
        if (err != ESP_OK) {
            return reject_mosfet_error(result, err, "mosfet_set_failed");
        }
        result_done(result);
        return ESP_OK;
    }
    if (strcmp(effect, "pulse") == 0) {
        if (duration_ms == 0 || duration_ms > NODE_CONTROL_MAX_MOSFET_PULSE_MS) {
            return result_rejected(result, "invalid_args");
        }
        esp_err_t err = node_hardware_io_mosfet_pulse((uint8_t)channel, value, duration_ms);
        if (err != ESP_OK) {
            return reject_mosfet_error(result, err, "mosfet_pulse_failed");
        }
        result_done(result);
        return ESP_OK;
    }
    if (strcmp(effect, "fade_in") == 0 || strcmp(effect, "fade_out") == 0 || strcmp(effect, "fade") == 0) {
        uint8_t fade_target = target;
        if (strcmp(effect, "fade_in") == 0) {
            fade_target = 255;
        } else if (strcmp(effect, "fade_out") == 0) {
            fade_target = 0;
        }
        if (duration_ms > NODE_CONTROL_MAX_MOSFET_FADE_MS) {
            return result_rejected(result, "invalid_args");
        }
        esp_err_t err = node_hardware_io_mosfet_fade((uint8_t)channel, fade_target, duration_ms);
        if (err != ESP_OK) {
            return reject_mosfet_error(result, err, "mosfet_fade_failed");
        }
        result_done(result);
        return ESP_OK;
    }
    if (strcmp(effect, "blink") == 0) {
        uint32_t on_ms = duration_ms;
        uint32_t off_ms = duration_ms;
        int count = (int)repeat;
        (void)read_u32_arg(args_json, "on_ms", &on_ms);
        (void)read_u32_arg(args_json, "off_ms", &off_ms);
        (void)read_int_arg(args_json, "count", &count);
        if (on_ms == 0 || off_ms == 0 || count < 0 ||
            (count > 0 && ((uint64_t)(on_ms + off_ms) * (uint64_t)count) > NODE_CONTROL_MAX_MOSFET_EFFECT_MS)) {
            return result_rejected(result, "invalid_args");
        }
        esp_err_t err = node_hardware_io_mosfet_blink((uint8_t)channel,
                                                      value,
                                                      on_ms,
                                                      off_ms,
                                                      (uint32_t)count,
                                                      final_value);
        if (err != ESP_OK) {
            return reject_mosfet_error(result, err, "mosfet_blink_failed");
        }
        result_done(result);
        return ESP_OK;
    }
    if (strcmp(effect, "breathe") == 0) {
        uint32_t fade_ms = duration_ms;
        uint32_t hold_ms = duration_ms / 2U;
        int count = (int)repeat;
        (void)read_u32_arg(args_json, "fade_ms", &fade_ms);
        (void)read_u32_arg(args_json, "hold_ms", &hold_ms);
        (void)read_int_arg(args_json, "count", &count);
        if (fade_ms == 0 || min_value > max_value || count < 0 ||
            (count > 0 && ((uint64_t)(fade_ms * 2U + hold_ms * 2U) * (uint64_t)count) > NODE_CONTROL_MAX_MOSFET_EFFECT_MS)) {
            return result_rejected(result, "invalid_args");
        }
        esp_err_t err = node_hardware_io_mosfet_breathe((uint8_t)channel,
                                                        min_value,
                                                        max_value,
                                                        fade_ms,
                                                        hold_ms,
                                                        (uint32_t)count,
                                                        final_value);
        if (err != ESP_OK) {
            return reject_mosfet_error(result, err, "mosfet_breathe_failed");
        }
        result_done(result);
        return ESP_OK;
    }

    return result_rejected(result, "invalid_args");
}

static esp_err_t execute_get_status(node_control_result_t *result)
{
    node_hardware_io_status_t status = node_hardware_io_get_status();
    int n = snprintf(result->data_json,
                     sizeof(result->data_json),
                     "{\"hardware\":{\"relays\":%u,\"mosfets\":%u,\"universal_inputs\":%u,"
                     "\"universal_outputs\":%u,\"led_strips\":%u}}",
                     (unsigned)status.configured_relays,
                     (unsigned)status.configured_mosfets,
                     (unsigned)status.configured_universal_inputs,
                     (unsigned)status.configured_universal_outputs,
                     (unsigned)status.configured_led_strips);
    if (n < 0 || n >= (int)sizeof(result->data_json)) {
        return result_rejected(result, "internal_error");
    }
    result_done(result);
    return ESP_OK;
}

static esp_err_t execute_describe_interface(node_control_result_t *result)
{
    size_t written = 0;
    static const char prefix[] = "{\"device_description\":";
    static const char suffix[] = "}";
    const size_t prefix_len = sizeof(prefix) - 1;
    const size_t suffix_len = sizeof(suffix) - 1;

    if (sizeof(result->data_json) <= prefix_len + suffix_len) {
        return result_rejected(result, "internal_error");
    }

    memcpy(result->data_json, prefix, prefix_len);
    esp_err_t err = node_capability_write_device_description(&s_config,
                                                             result->data_json + prefix_len,
                                                             sizeof(result->data_json) - prefix_len - suffix_len,
                                                             &written);
    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "device_description does not fit result buffer cap=%u err=%s",
                 (unsigned)sizeof(result->data_json),
                 esp_err_to_name(err));
        return result_rejected(result, "internal_error");
    }
    memcpy(result->data_json + prefix_len + written, suffix, suffix_len + 1);
    result_done(result);
    return ESP_OK;
}

esp_err_t node_control_init(const node_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    s_config = *config;
    return ESP_OK;
}

esp_err_t node_control_execute(const node_control_command_t *command, node_control_result_t *out_result)
{
    if (!command || !command->command || !out_result) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_result, 0, sizeof(*out_result));

    if (strcmp(command->command, "node.get_status") == 0) {
        return execute_get_status(out_result);
    }
    if (strcmp(command->command, "node.identify") == 0) {
        esp_err_t err = node_hardware_io_identify(150, 2);
        if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
            return result_rejected(out_result, "internal_error");
        }
        result_done(out_result);
        return ESP_OK;
    }
    if (strcmp(command->command, "describe_interface") == 0) {
        return execute_describe_interface(out_result);
    }
    if (strcmp(command->command, "relay.set") == 0) {
        return execute_output_set(NODE_HW_OUTPUT_RELAY, command->args_json, out_result);
    }
    if (strcmp(command->command, "relay.pulse") == 0) {
        return execute_output_pulse(NODE_HW_OUTPUT_RELAY, command->args_json, out_result);
    }
    if (strcmp(command->command, "relay.all_off") == 0) {
        return execute_output_all_off(NODE_HW_OUTPUT_RELAY, out_result, "relay_all_off_failed");
    }
    if (strcmp(command->command, "mosfet.set") == 0) {
        return execute_mosfet_set(command->args_json, out_result);
    }
    if (strcmp(command->command, "mosfet.fade") == 0) {
        return execute_mosfet_fade(command->args_json, out_result);
    }
    if (strcmp(command->command, "mosfet.pulse") == 0) {
        return execute_mosfet_pulse(command->args_json, out_result);
    }
    if (strcmp(command->command, "mosfet.blink") == 0) {
        return execute_mosfet_blink(command->args_json, out_result);
    }
    if (strcmp(command->command, "mosfet.breathe") == 0) {
        return execute_mosfet_breathe(command->args_json, out_result);
    }
    if (strcmp(command->command, "mosfet.all_off") == 0) {
        return execute_mosfet_all_off(out_result);
    }
    if (strcmp(command->command, "mosfet.effect") == 0) {
        return execute_mosfet_effect_alias(command->args_json, out_result);
    }
    if (strcmp(command->command, "io.set") == 0) {
        return execute_output_set(NODE_HW_OUTPUT_UNIVERSAL_IO, command->args_json, out_result);
    }
    if (strcmp(command->command, "io.all_off") == 0) {
        return execute_output_all_off(NODE_HW_OUTPUT_UNIVERSAL_IO, out_result, "io_all_off_failed");
    }
    if (strcmp(command->command, "node.all_off") == 0) {
        return execute_node_all_off(out_result);
    }
    if (strcmp(command->command, "led.off") == 0) {
        return execute_led_off(command->args_json, out_result);
    }
    if (strcmp(command->command, "led.solid") == 0) {
        return execute_led_solid(command->args_json, out_result);
    }
    if (strcmp(command->command, "led.effect") == 0) {
        return execute_led_effect(command->args_json, out_result);
    }
    return result_rejected(out_result, "not_supported");
}
