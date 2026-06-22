#include "node_json.h"

#include <stdio.h>
#include <string.h>

bool node_json_append_escaped(char *out, size_t out_size, size_t *io_len, const char *value)
{
    size_t len = io_len ? *io_len : 0;
    const unsigned char *p = (const unsigned char *)(value ? value : "");

    if (!out || out_size == 0 || !io_len || len >= out_size) {
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
            out[len] = '\0';
            continue;
        }
        if (len + chunk_len >= out_size) {
            out[0] = '\0';
            return false;
        }
        memcpy(out + len, chunk, chunk_len);
        len += chunk_len;
        out[len] = '\0';
        ++p;
    }

    *io_len = len;
    return true;
}

bool node_json_escape_string(char *out, size_t out_size, const char *value)
{
    size_t len = 0;

    if (!out || out_size == 0) {
        return false;
    }

    out[0] = '\0';
    return node_json_append_escaped(out, out_size, &len, value);
}
