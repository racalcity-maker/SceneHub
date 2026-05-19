#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "node_config.h"

typedef enum {
    NODE_HW_OUTPUT_RELAY = 0,
    NODE_HW_OUTPUT_MOSFET,
    NODE_HW_OUTPUT_UNIVERSAL_IO,
} node_hw_output_kind_t;

typedef struct {
    uint32_t configured_relays;
    uint32_t configured_mosfets;
    uint32_t configured_universal_inputs;
    uint32_t configured_universal_outputs;
    uint32_t configured_led_strips;
} node_hardware_io_status_t;

esp_err_t node_hardware_io_init(const node_config_t *config);
esp_err_t node_hardware_io_set_output(node_hw_output_kind_t kind, uint8_t channel, bool on);
node_hardware_io_status_t node_hardware_io_get_status(void);
