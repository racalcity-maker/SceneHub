#pragma once

#include <stdbool.h>
#include <stddef.h>

bool node_text_nonempty_bounded(const char *text, size_t max_len);
bool node_text_identifier_valid(const char *text, size_t max_len);
