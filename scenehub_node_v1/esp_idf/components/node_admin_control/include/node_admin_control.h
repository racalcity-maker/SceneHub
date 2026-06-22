#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "node_config.h"
#include "node_driver_nfc_contract.h"
#include "node_rule_model.h"
#include "node_rule_store.h"

typedef struct {
    esp_err_t err;
    bool restart_required;
    bool applied;
    bool restarting;
} node_admin_control_result_t;

esp_err_t node_admin_control_init(node_config_t *live_config);
esp_err_t node_admin_control_get_config(node_config_t *out_config);
esp_err_t node_admin_control_save_base(const node_config_t *config, node_admin_control_result_t *out_result);
esp_err_t node_admin_control_save_led(const node_led_strip_config_t *led_strips,
                                      size_t count,
                                      node_admin_control_result_t *out_result);
esp_err_t node_admin_control_save_nfc_cards(const node_nfc_known_card_t *cards,
                                            size_t count,
                                            node_admin_control_result_t *out_result);
esp_err_t node_admin_control_reset_wifi(node_admin_control_result_t *out_result);
esp_err_t node_admin_control_factory_reset(node_admin_control_result_t *out_result);
esp_err_t node_admin_control_restart(node_admin_control_result_t *out_result);
esp_err_t node_admin_control_validate_rules(const char *raw_json,
                                            node_rule_bundle_metadata_t *out_metadata,
                                            char *out_error_code,
                                            size_t out_error_code_size,
                                            node_admin_control_result_t *out_result);
esp_err_t node_admin_control_apply_rules(const char *raw_json,
                                         node_rule_bundle_metadata_t *out_metadata,
                                         char *out_error_code,
                                         size_t out_error_code_size,
                                         node_admin_control_result_t *out_result);
esp_err_t node_admin_control_get_rules(node_rule_store_entry_t *out_entry);
esp_err_t node_admin_control_clear_rules(node_admin_control_result_t *out_result);
esp_err_t node_admin_control_pause_rules(node_admin_control_result_t *out_result);
esp_err_t node_admin_control_resume_rules(node_admin_control_result_t *out_result);
esp_err_t node_admin_control_reinit_nfc(node_admin_control_result_t *out_result);
