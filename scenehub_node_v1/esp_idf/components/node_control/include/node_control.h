#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "node_config.h"
#include "node_limits.h"

typedef struct {
    const char *request_id;
    const char *command;
    const char *args_json;
} node_control_command_t;

typedef struct {
    char status[16];
    char error_code[32];
    char data_json[NODE_CONTROL_DATA_JSON_MAX_LEN];
} node_control_result_t;

esp_err_t node_control_init(const node_config_t *config);
esp_err_t node_control_execute(const node_control_command_t *command, node_control_result_t *out_result);
