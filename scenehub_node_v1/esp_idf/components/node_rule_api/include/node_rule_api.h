#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "node_config.h"
#include "node_limits.h"
#include "node_rule_model.h"
#include "node_rule_store.h"

esp_err_t node_rule_api_validate_bundle(const char *raw_json,
                                        node_rule_bundle_metadata_t *out_metadata,
                                        char *out_error_code,
                                        size_t out_error_code_size);
esp_err_t node_rule_api_validate_bundle_for_config(const char *raw_json,
                                                   const node_config_t *config,
                                                   node_rule_bundle_metadata_t *out_metadata,
                                                   char *out_error_code,
                                                   size_t out_error_code_size);
esp_err_t node_rule_api_apply_bundle(const char *raw_json,
                                     node_rule_bundle_metadata_t *out_metadata,
                                     char *out_error_code,
                                     size_t out_error_code_size);
esp_err_t node_rule_api_apply_bundle_for_config(const char *raw_json,
                                                const node_config_t *config,
                                                node_rule_bundle_metadata_t *out_metadata,
                                                char *out_error_code,
                                                size_t out_error_code_size);
esp_err_t node_rule_api_get_bundle(node_rule_store_entry_t *out_entry);
esp_err_t node_rule_api_clear_bundle(void);
esp_err_t node_rule_api_pause(void);
esp_err_t node_rule_api_resume(void);
