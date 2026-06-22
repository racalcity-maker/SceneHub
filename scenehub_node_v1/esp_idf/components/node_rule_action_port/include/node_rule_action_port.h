#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef struct {
    esp_err_t (*execute_command)(const char *command, const char *args_json);
    esp_err_t (*emit_event)(const char *event_name, const char *args_json);
    esp_err_t (*publish_input_change)(uint8_t channel, int32_t value);
} node_rule_action_port_t;

esp_err_t node_rule_action_port_bind(const node_rule_action_port_t *port);
esp_err_t node_rule_action_port_execute_command(const char *command, const char *args_json);
esp_err_t node_rule_action_port_emit_event(const char *event_name, const char *args_json);
esp_err_t node_rule_action_port_publish_input_change(uint8_t channel, int32_t value);
