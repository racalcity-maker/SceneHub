#pragma once

#include <stdbool.h>

#include "node_config.h"

node_operation_mode_t node_runtime_mode_normalize(node_operation_mode_t mode);
const char *node_runtime_mode_name(node_operation_mode_t mode);
bool node_runtime_mode_from_name(const char *name, node_operation_mode_t *out_mode);
bool node_runtime_mode_requires_controller(const node_config_t *config);
bool node_runtime_mode_should_start_mqtt(const node_config_t *config);
bool node_runtime_mode_rules_enabled(const node_config_t *config);
