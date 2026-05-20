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
    bool last_active;
    bool last_active_valid;
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
esp_err_t node_hw_relay_all_off(void);
esp_err_t node_hw_mosfet_set(uint8_t channel, bool on);
esp_err_t node_hw_mosfet_set_value(uint8_t channel, uint8_t value);
esp_err_t node_hw_mosfet_fade(uint8_t channel, uint8_t target, uint32_t duration_ms);
esp_err_t node_hw_mosfet_pulse(uint8_t channel, uint8_t value, uint32_t duration_ms);
esp_err_t node_hw_mosfet_blink(uint8_t channel,
                               uint8_t value,
                               uint32_t on_ms,
                               uint32_t off_ms,
                               uint32_t count,
                               uint8_t final_value);
esp_err_t node_hw_mosfet_breathe(uint8_t channel,
                                 uint8_t min_value,
                                 uint8_t max_value,
                                 uint32_t fade_ms,
                                 uint32_t hold_ms,
                                 uint32_t count,
                                 uint8_t final_value);
esp_err_t node_hw_mosfet_all_off(void);
esp_err_t node_hw_universal_output_set(uint8_t channel, bool on);
esp_err_t node_hw_universal_output_all_off(void);
esp_err_t node_hw_led_all_off(void);
esp_err_t node_hw_led_off(uint8_t strip);
esp_err_t node_hw_led_solid(uint8_t strip,
                            uint8_t red,
                            uint8_t green,
                            uint8_t blue,
                            uint8_t white,
                            uint8_t brightness);
esp_err_t node_hw_led_run_effect(uint8_t strip,
                                 node_hw_led_effect_t effect,
                                 const node_hw_led_effect_config_t *config);
