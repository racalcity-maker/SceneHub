#include "node_text.h"

#include <ctype.h>
#include <string.h>

bool node_text_nonempty_bounded(const char *text, size_t max_len)
{
    size_t len = 0;

    if (!text) {
        return false;
    }
    len = strnlen(text, max_len + 1U);
    return len > 0U && len <= max_len;
}

bool node_text_identifier_valid(const char *text, size_t max_len)
{
    size_t len = 0;

    if (!node_text_nonempty_bounded(text, max_len)) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (!(isalnum(*p) || *p == '_' || *p == '.' || *p == ':' || *p == '-')) {
            return false;
        }
        ++len;
    }
    return len > 0U && len <= max_len;
}
