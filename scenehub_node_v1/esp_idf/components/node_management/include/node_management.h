#pragma once

#include "esp_err.h"
#include "node_config.h"
#include "node_reset_button.h"

esp_err_t node_management_start(const node_config_t *config);
void node_management_handle_reset_button_event(node_reset_button_event_t event, void *ctx);
