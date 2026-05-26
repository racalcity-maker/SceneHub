#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "node_config.h"
#include "node_limits.h"

typedef enum {
    NODE_CONTROL_SOURCE_UNKNOWN = 0,
    NODE_CONTROL_SOURCE_HUB,
    NODE_CONTROL_SOURCE_LOCAL_PREVIEW,
    NODE_CONTROL_SOURCE_LOCAL_UI,
} node_control_command_source_t;

typedef struct {
    const char *request_id;
    const char *command;
    const char *args_json;
    node_control_command_source_t source;
} node_control_command_t;

typedef struct {
    char status[16];
    char error_code[32];
    char data_json[NODE_CONTROL_DATA_JSON_MAX_LEN];
} node_control_result_t;

esp_err_t node_control_init(const node_config_t *config);
esp_err_t node_control_update_led_config(const node_led_strip_config_t *led_strips, size_t count);
esp_err_t node_control_execute(const node_control_command_t *command, node_control_result_t *out_result);
