#pragma once

#include "node_hardware_io.h"

typedef struct {
    bool configured;
    int gpio;
    bool active_low;
    bool state_on;
} node_hw_output_slot_t;

typedef struct {
    bool configured;
    int gpio;
    bool active_low;
} node_hw_input_slot_t;

typedef struct {
    node_hw_output_slot_t relays[NODE_RELAY_MAX];
    node_hw_output_slot_t mosfets[NODE_MOSFET_MAX];
    node_hw_output_slot_t universal_outputs[NODE_UNIVERSAL_IO_MAX];
    node_hw_input_slot_t universal_inputs[NODE_UNIVERSAL_IO_MAX];
    node_hardware_io_status_t status;
} node_hw_state_t;

extern node_hw_state_t g_node_hw;

esp_err_t node_hw_configure_output_gpio(int gpio, bool active_low);
esp_err_t node_hw_configure_input_gpio(int gpio, bool active_low);
void node_hw_assign_output_slot(node_hw_output_slot_t *slot, int gpio, bool active_low);
void node_hw_assign_input_slot(node_hw_input_slot_t *slot, int gpio, bool active_low);
esp_err_t node_hw_output_slot_set(node_hw_output_slot_t *slot, bool on);

esp_err_t node_hw_relay_init(const node_config_t *config);
esp_err_t node_hw_mosfet_init(const node_config_t *config);
esp_err_t node_hw_universal_io_init(const node_config_t *config);
esp_err_t node_hw_led_init(const node_config_t *config);
esp_err_t node_hw_relay_set(uint8_t channel, bool on);
esp_err_t node_hw_mosfet_set(uint8_t channel, bool on);
esp_err_t node_hw_universal_output_set(uint8_t channel, bool on);
