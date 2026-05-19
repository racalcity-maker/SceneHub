#include "web_ui_gm_runtime_json_writer.h"

#include <stdio.h>
#include <string.h>

#include "web_ui_utils.h"

esp_err_t gm_runtime_json_flush(gm_runtime_json_writer_t *writer)
{
    if (!writer || !writer->req) {
        return ESP_ERR_INVALID_ARG;
    }
    if (writer->len == 0) {
        return ESP_OK;
    }
    esp_err_t err = web_ui_http_resp_send_chunk(writer->req, writer->chunk, writer->len);
    if (err != ESP_OK) {
        return err;
    }
    writer->len = 0;
    return ESP_OK;
}

esp_err_t gm_runtime_json_write_len(gm_runtime_json_writer_t *writer,
                                    const char *data,
                                    size_t len)
{
    if (!writer || (!data && len > 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    while (len > 0) {
        size_t space = writer->capacity - writer->len;
        if (space == 0) {
            esp_err_t err = gm_runtime_json_flush(writer);
            if (err != ESP_OK) {
                return err;
            }
            space = writer->capacity;
        }
        size_t chunk = len < space ? len : space;
        memcpy(writer->chunk + writer->len, data, chunk);
        writer->len += chunk;
        data += chunk;
        len -= chunk;
    }
    return ESP_OK;
}

esp_err_t gm_runtime_json_write_raw(gm_runtime_json_writer_t *writer, const char *text)
{
    if (!text) {
        text = "";
    }
    return gm_runtime_json_write_len(writer, text, strlen(text));
}

esp_err_t gm_runtime_json_write_uint64(gm_runtime_json_writer_t *writer, uint64_t value)
{
    char buf[32];
    int written = snprintf(buf, sizeof(buf), "%llu", (unsigned long long)value);
    if (written <= 0 || (size_t)written >= sizeof(buf)) {
        return ESP_FAIL;
    }
    return gm_runtime_json_write_len(writer, buf, (size_t)written);
}

esp_err_t gm_runtime_json_write_int32(gm_runtime_json_writer_t *writer, int32_t value)
{
    char buf[24];
    int written = snprintf(buf, sizeof(buf), "%ld", (long)value);
    if (written <= 0 || (size_t)written >= sizeof(buf)) {
        return ESP_FAIL;
    }
    return gm_runtime_json_write_len(writer, buf, (size_t)written);
}

esp_err_t gm_runtime_json_write_bool(gm_runtime_json_writer_t *writer, bool value)
{
    return gm_runtime_json_write_raw(writer, value ? "true" : "false");
}

esp_err_t gm_runtime_json_write_string(gm_runtime_json_writer_t *writer, const char *value)
{
    const unsigned char *p = (const unsigned char *)(value ? value : "");
    esp_err_t err = gm_runtime_json_write_raw(writer, "\"");
    if (err != ESP_OK) {
        return err;
    }
    for (; *p; ++p) {
        switch (*p) {
            case '\"':
                err = gm_runtime_json_write_raw(writer, "\\\"");
                break;
            case '\\':
                err = gm_runtime_json_write_raw(writer, "\\\\");
                break;
            case '\b':
                err = gm_runtime_json_write_raw(writer, "\\b");
                break;
            case '\f':
                err = gm_runtime_json_write_raw(writer, "\\f");
                break;
            case '\n':
                err = gm_runtime_json_write_raw(writer, "\\n");
                break;
            case '\r':
                err = gm_runtime_json_write_raw(writer, "\\r");
                break;
            case '\t':
                err = gm_runtime_json_write_raw(writer, "\\t");
                break;
            default:
                if (*p < 0x20) {
                    char escaped[7];
                    int written = snprintf(escaped, sizeof(escaped), "\\u%04x", (unsigned)*p);
                    if (written <= 0 || (size_t)written >= sizeof(escaped)) {
                        return ESP_FAIL;
                    }
                    err = gm_runtime_json_write_len(writer, escaped, (size_t)written);
                } else {
                    char ch = (char)*p;
                    err = gm_runtime_json_write_len(writer, &ch, 1);
                }
                break;
        }
        if (err != ESP_OK) {
            return err;
        }
    }
    return gm_runtime_json_write_raw(writer, "\"");
}

esp_err_t gm_runtime_json_begin_field(gm_runtime_json_writer_t *writer,
                                      bool *first,
                                      const char *key)
{
    esp_err_t err = ESP_OK;
    if (!writer || !first || !key) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!*first) {
        err = gm_runtime_json_write_raw(writer, ",");
        if (err != ESP_OK) {
            return err;
        }
    }
    *first = false;
    err = gm_runtime_json_write_string(writer, key);
    if (err != ESP_OK) {
        return err;
    }
    return gm_runtime_json_write_raw(writer, ":");
}

esp_err_t gm_runtime_json_write_string_field(gm_runtime_json_writer_t *writer,
                                             bool *first,
                                             const char *key,
                                             const char *value)
{
    esp_err_t err = gm_runtime_json_begin_field(writer, first, key);
    if (err != ESP_OK) {
        return err;
    }
    return gm_runtime_json_write_string(writer, value);
}

esp_err_t gm_runtime_json_write_bool_field(gm_runtime_json_writer_t *writer,
                                           bool *first,
                                           const char *key,
                                           bool value)
{
    esp_err_t err = gm_runtime_json_begin_field(writer, first, key);
    if (err != ESP_OK) {
        return err;
    }
    return gm_runtime_json_write_bool(writer, value);
}

esp_err_t gm_runtime_json_write_uint64_field(gm_runtime_json_writer_t *writer,
                                             bool *first,
                                             const char *key,
                                             uint64_t value)
{
    esp_err_t err = gm_runtime_json_begin_field(writer, first, key);
    if (err != ESP_OK) {
        return err;
    }
    return gm_runtime_json_write_uint64(writer, value);
}

esp_err_t gm_runtime_json_write_int32_field(gm_runtime_json_writer_t *writer,
                                            bool *first,
                                            const char *key,
                                            int32_t value)
{
    esp_err_t err = gm_runtime_json_begin_field(writer, first, key);
    if (err != ESP_OK) {
        return err;
    }
    return gm_runtime_json_write_int32(writer, value);
}
