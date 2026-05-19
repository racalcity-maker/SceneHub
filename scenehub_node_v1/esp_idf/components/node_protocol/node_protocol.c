#include "node_protocol.h"

#include <stdio.h>

esp_err_t node_protocol_topic(char *out, size_t out_size, const char *node_id, const char *suffix)
{
    if (!out || out_size == 0 || !node_id || !suffix || node_id[0] == '\0' || suffix[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    int written = snprintf(out, out_size, "cp/v1/dev/%s/%s", node_id, suffix);
    return (written > 0 && (size_t)written < out_size) ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t node_protocol_command_topic(char *out, size_t out_size, const char *node_id)
{
    return node_protocol_topic(out, out_size, node_id, "control/command");
}

esp_err_t node_protocol_result_topic(char *out, size_t out_size, const char *node_id)
{
    return node_protocol_topic(out, out_size, node_id, "result");
}

esp_err_t node_protocol_status_topic(char *out, size_t out_size, const char *node_id)
{
    return node_protocol_topic(out, out_size, node_id, "status");
}

esp_err_t node_protocol_heartbeat_topic(char *out, size_t out_size, const char *node_id)
{
    return node_protocol_topic(out, out_size, node_id, "heartbeat");
}
