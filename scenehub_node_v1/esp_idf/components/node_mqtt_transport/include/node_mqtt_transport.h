#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "node_config.h"

esp_err_t node_mqtt_transport_start(const node_config_t *config);
bool node_mqtt_transport_is_connected(void);
esp_err_t node_mqtt_transport_publish_event(const char *event_name, const char *args_json);
esp_err_t node_mqtt_transport_publish_input_change(uint8_t channel, int32_t value);
