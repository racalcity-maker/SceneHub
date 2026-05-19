#include "node_mqtt_internal.h"

#include <stdio.h>
#include <string.h>

bool node_mqtt_json_extract_string(const char *json, const char *key, char *out, size_t out_size)
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
    if (*p != '"') {
        return false;
    }
    ++p;
    size_t len = 0;
    while (*p && *p != '"') {
        if (*p == '\\' || len + 1 >= out_size) {
            return false;
        }
        out[len++] = *p++;
    }
    if (*p != '"') {
        return false;
    }
    out[len] = '\0';
    return len > 0;
}

bool node_mqtt_json_copy_object(const char *json, const char *key, char *out, size_t out_size)
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
        snprintf(out, out_size, "{}");
        return true;
    }
    ++p;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        ++p;
    }
    if (*p != '{') {
        return false;
    }

    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    size_t len = 0;
    for (; *p; ++p) {
        if (len + 1 >= out_size) {
            return false;
        }
        out[len++] = *p;
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (*p == '\\') {
                escaped = true;
            } else if (*p == '"') {
                in_string = false;
            }
            continue;
        }
        if (*p == '"') {
            in_string = true;
        } else if (*p == '{') {
            ++depth;
        } else if (*p == '}') {
            --depth;
            if (depth == 0) {
                out[len] = '\0';
                return true;
            }
        }
    }
    return false;
}
