#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"

typedef struct {
    httpd_req_t *req;
    char *chunk;
    size_t capacity;
    size_t len;
} gm_runtime_json_writer_t;

esp_err_t gm_runtime_json_flush(gm_runtime_json_writer_t *writer);
esp_err_t gm_runtime_json_write_len(gm_runtime_json_writer_t *writer,
                                    const char *data,
                                    size_t len);
esp_err_t gm_runtime_json_write_raw(gm_runtime_json_writer_t *writer, const char *text);
esp_err_t gm_runtime_json_write_uint64(gm_runtime_json_writer_t *writer, uint64_t value);
esp_err_t gm_runtime_json_write_int32(gm_runtime_json_writer_t *writer, int32_t value);
esp_err_t gm_runtime_json_write_bool(gm_runtime_json_writer_t *writer, bool value);
esp_err_t gm_runtime_json_write_string(gm_runtime_json_writer_t *writer, const char *value);
esp_err_t gm_runtime_json_begin_field(gm_runtime_json_writer_t *writer,
                                      bool *first,
                                      const char *key);
esp_err_t gm_runtime_json_write_string_field(gm_runtime_json_writer_t *writer,
                                             bool *first,
                                             const char *key,
                                             const char *value);
esp_err_t gm_runtime_json_write_bool_field(gm_runtime_json_writer_t *writer,
                                           bool *first,
                                           const char *key,
                                           bool value);
esp_err_t gm_runtime_json_write_uint64_field(gm_runtime_json_writer_t *writer,
                                             bool *first,
                                             const char *key,
                                             uint64_t value);
esp_err_t gm_runtime_json_write_int32_field(gm_runtime_json_writer_t *writer,
                                            bool *first,
                                            const char *key,
                                            int32_t value);
