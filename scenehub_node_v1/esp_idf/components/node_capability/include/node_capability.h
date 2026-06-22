#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "node_config.h"

esp_err_t node_capability_write_device_description(const node_config_t *config,
                                                   char *out,
                                                   size_t out_size,
                                                   size_t *out_written);
esp_err_t node_capability_write_node_status_json(const node_config_t *config,
                                                 char *out,
                                                 size_t out_size,
                                                 size_t *out_written);
