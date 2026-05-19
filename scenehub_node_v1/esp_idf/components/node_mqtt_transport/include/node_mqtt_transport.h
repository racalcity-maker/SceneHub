#pragma once

#include "esp_err.h"
#include "node_config.h"

esp_err_t node_mqtt_transport_start(const node_config_t *config);
