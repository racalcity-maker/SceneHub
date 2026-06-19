#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "node_config.h"
#include "node_rule_model.h"

esp_err_t node_rule_schema_validate_bundle(const char *raw_json,
                                           node_rule_bundle_metadata_t *out_metadata,
                                           char *out_error_code,
                                           size_t out_error_code_size);
esp_err_t node_rule_schema_validate_bundle_for_config(const char *raw_json,
                                                      const node_config_t *config,
                                                      node_rule_bundle_metadata_t *out_metadata,
                                                      char *out_error_code,
                                                      size_t out_error_code_size);
