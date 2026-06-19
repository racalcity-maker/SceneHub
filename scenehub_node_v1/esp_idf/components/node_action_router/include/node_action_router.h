#pragma once

#include <stdint.h>

#include "esp_err.h"

esp_err_t node_action_router_execute_command(const char *command, const char *args_json);
esp_err_t node_action_router_emit_event(const char *event_name, const char *args_json);
esp_err_t node_action_router_publish_input_change(uint8_t channel, int32_t value);
