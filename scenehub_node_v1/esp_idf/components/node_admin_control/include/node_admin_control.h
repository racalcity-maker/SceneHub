#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "node_config.h"

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
esp_err_t node_admin_control_reset_wifi(node_admin_control_result_t *out_result);
esp_err_t node_admin_control_factory_reset(node_admin_control_result_t *out_result);
esp_err_t node_admin_control_restart(node_admin_control_result_t *out_result);
