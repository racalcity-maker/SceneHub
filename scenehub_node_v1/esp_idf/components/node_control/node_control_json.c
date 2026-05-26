#include "node_control_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

bool read_int_arg(const char *json, const char *key, int *out)
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

bool read_bool_arg(const char *json, const char *key, bool *out)
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

bool json_has_key(const char *json, const char *key)
{
    char pattern[48];
    int pn = 0;

    if (!json || !key) {
        return false;
    }
    pn = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (pn <= 0 || pn >= (int)sizeof(pattern)) {
        return false;
    }
    return strstr(json, pattern) != NULL;
}

bool json_has_any_key(const char *json, const char *const *keys, size_t count)
{
    if (!json || !keys) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (keys[i] && json_has_key(json, keys[i])) {
            return true;
        }
    }
    return false;
}

bool read_u32_arg(const char *json, const char *key, uint32_t *out)
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

bool read_pwm_arg(const char *json, const char *key, uint8_t *out)
{
    int value = 0;
    if (!read_int_arg(json, key, &value) || value < 0 || value > 255 || !out) {
        return false;
    }
    *out = (uint8_t)value;
    return true;
}

bool read_optional_pwm_arg(const char *json, const char *key, uint8_t fallback, uint8_t *out)
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

bool read_string_arg(const char *json, const char *key, char *out, size_t out_size)
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

bool parse_color_text(const char *text,
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

bool read_color_arg(const char *json,
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

bool read_optional_color_arg(const char *json,
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
