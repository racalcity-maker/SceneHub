#pragma once

#include <stddef.h>

#include "esp_err.h"

esp_err_t node_protocol_topic(char *out, size_t out_size, const char *node_id, const char *suffix);
esp_err_t node_protocol_command_topic(char *out, size_t out_size, const char *node_id);
esp_err_t node_protocol_result_topic(char *out, size_t out_size, const char *node_id);
esp_err_t node_protocol_status_topic(char *out, size_t out_size, const char *node_id);
esp_err_t node_protocol_heartbeat_topic(char *out, size_t out_size, const char *node_id);
esp_err_t node_protocol_event_topic(char *out, size_t out_size, const char *node_id);
