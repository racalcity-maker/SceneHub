#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "node_limits.h"
#include "node_rule_model.h"

typedef struct {
    node_rule_bundle_metadata_t metadata;
    char raw_json[NODE_RULE_BUNDLE_MAX_LEN + 1];
} node_rule_store_entry_t;

esp_err_t node_rule_store_load(node_rule_store_entry_t *out_entry);
esp_err_t node_rule_store_save(const char *raw_json,
                               size_t raw_size,
                               const node_rule_bundle_metadata_t *metadata);
esp_err_t node_rule_store_clear(void);
