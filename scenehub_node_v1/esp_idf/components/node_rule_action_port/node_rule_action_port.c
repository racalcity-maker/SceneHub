#include "node_rule_action_port.h"

#include <stdbool.h>

static node_rule_action_port_t s_port;
static bool s_bound;

esp_err_t node_rule_action_port_bind(const node_rule_action_port_t *port)
{
    if (!port ||
        !port->execute_command ||
        !port->emit_event ||
        !port->publish_input_change) {
        return ESP_ERR_INVALID_ARG;
    }

    s_port = *port;
    s_bound = true;
    return ESP_OK;
}

esp_err_t node_rule_action_port_execute_command(const char *command, const char *args_json)
{
    if (!s_bound || !s_port.execute_command) {
        return ESP_ERR_INVALID_STATE;
    }
    return s_port.execute_command(command, args_json);
}

esp_err_t node_rule_action_port_emit_event(const char *event_name, const char *args_json)
{
    if (!s_bound || !s_port.emit_event) {
        return ESP_ERR_INVALID_STATE;
    }
    return s_port.emit_event(event_name, args_json);
}

esp_err_t node_rule_action_port_publish_input_change(uint8_t channel, int32_t value)
{
    if (!s_bound || !s_port.publish_input_change) {
        return ESP_ERR_INVALID_STATE;
    }
    return s_port.publish_input_change(channel, value);
}
