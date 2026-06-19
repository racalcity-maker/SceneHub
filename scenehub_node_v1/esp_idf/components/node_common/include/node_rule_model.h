#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "node_limits.h"

typedef struct {
    bool has_bundle;
    uint32_t version;
    uint32_t generation;
    size_t raw_size;
    char bundle_id[NODE_RULE_BUNDLE_ID_MAX_LEN + 1];
    char mode[NODE_RULE_MODE_MAX_LEN];
} node_rule_bundle_metadata_t;
