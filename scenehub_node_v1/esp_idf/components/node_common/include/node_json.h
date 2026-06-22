#pragma once

#include <stdbool.h>
#include <stddef.h>

bool node_json_escape_string(char *out, size_t out_size, const char *value);
bool node_json_append_escaped(char *out, size_t out_size, size_t *io_len, const char *value);
