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

typedef enum {
    NODE_HW_LED_EFFECT_BLINK = 0,
    NODE_HW_LED_EFFECT_BREATHE,
    NODE_HW_LED_EFFECT_RAINBOW,
    NODE_HW_LED_EFFECT_COLOR_WIPE,
    NODE_HW_LED_EFFECT_SCANNER,
    NODE_HW_LED_EFFECT_THEATER,
    NODE_HW_LED_EFFECT_STROBE,
} node_hw_led_effect_t;

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t white;
    uint8_t red2;
    uint8_t green2;
    uint8_t blue2;
    uint8_t white2;
    uint8_t brightness;
    uint32_t duration_ms;
    uint32_t step_ms;
    uint32_t count;
} node_hw_led_effect_config_t;

typedef struct {
    uint32_t configured_relays;
    uint32_t configured_mosfets;
    uint32_t configured_universal_inputs;
    uint32_t configured_universal_outputs;
    uint32_t configured_led_strips;
} node_hardware_io_status_t;

esp_err_t node_hardware_io_init(const node_config_t *config);
esp_err_t node_hardware_io_set_output(node_hw_output_kind_t kind, uint8_t channel, bool on);
esp_err_t node_hardware_io_pulse_output(node_hw_output_kind_t kind, uint8_t channel, uint32_t duration_ms);
esp_err_t node_hardware_io_all_off(node_hw_output_kind_t kind);
esp_err_t node_hardware_io_mosfet_set(uint8_t channel, uint8_t value);
esp_err_t node_hardware_io_mosfet_fade(uint8_t channel, uint8_t target, uint32_t duration_ms);
esp_err_t node_hardware_io_mosfet_pulse(uint8_t channel, uint8_t value, uint32_t duration_ms);
esp_err_t node_hardware_io_mosfet_blink(uint8_t channel,
                                        uint8_t value,
                                        uint32_t on_ms,
                                        uint32_t off_ms,
                                        uint32_t count,
                                        uint8_t final_value);
esp_err_t node_hardware_io_mosfet_breathe(uint8_t channel,
                                          uint8_t min_value,
                                          uint8_t max_value,
                                          uint32_t fade_ms,
                                          uint32_t hold_ms,
                                          uint32_t count,
                                          uint8_t final_value);
esp_err_t node_hardware_io_mosfet_all_off(void);
esp_err_t node_hardware_io_led_all_off(void);
esp_err_t node_hardware_io_led_off(uint8_t strip);
esp_err_t node_hardware_io_led_solid(uint8_t strip,
                                     uint8_t red,
                                     uint8_t green,
                                     uint8_t blue,
                                     uint8_t white,
                                     uint8_t brightness);
esp_err_t node_hardware_io_led_run_effect(uint8_t strip,
                                          node_hw_led_effect_t effect,
                                          const node_hw_led_effect_config_t *config);
esp_err_t node_hardware_io_node_all_off(void);
esp_err_t node_hardware_io_identify(uint32_t duration_ms, uint8_t repeat_count);
esp_err_t node_hardware_io_read_input(uint8_t channel, bool *out_active);
esp_err_t node_hardware_io_observe_input_change(uint8_t channel,
                                                bool current_active,
                                                bool *out_changed);
node_hardware_io_status_t node_hardware_io_get_status(void);
