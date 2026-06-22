#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "node_config.h"
#include "node_limits.h"
#include "node_rule_model.h"
#include "node_rule_store.h"

typedef struct {
    bool initialized;
    bool paused;
    bool rules_enabled_by_mode;
    bool has_bundle;
    uint32_t generation;
    size_t compiled_rules;
    size_t compiled_actions;
    char bundle_id[NODE_RULE_BUNDLE_ID_MAX_LEN + 1];
    char compile_status[16];
} node_rule_api_runtime_status_t;

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
esp_err_t node_rule_api_dispatch_mqtt_command(const char *command_name);
void node_rule_api_get_runtime_status(node_rule_api_runtime_status_t *out_status);
