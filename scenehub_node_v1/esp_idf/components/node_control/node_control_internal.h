#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "node_control.h"
#include "node_hardware_io.h"

#define NODE_CONTROL_MAX_MOSFET_PULSE_MS 60000U
#define NODE_CONTROL_MAX_MOSFET_FADE_MS 60000U
#define NODE_CONTROL_MAX_MOSFET_EFFECT_MS 600000ULL
#define NODE_CONTROL_MAX_LED_STEP_MS 5000U
#define NODE_CONTROL_MAX_LED_DURATION_MS 60000U
#define NODE_CONTROL_MAX_LED_EFFECT_MS 600000ULL

extern node_config_t g_node_control_config;

void copy_text(char *dst, size_t dst_size, const char *src);
const char *command_source_text(node_control_command_source_t source);
const char *command_request_id_text(const node_control_command_t *command);
const char *command_source_safe(const node_control_command_t *command);
void result_done(node_control_result_t *result);
void result_started(node_control_result_t *result);
esp_err_t result_rejected(node_control_result_t *result, const char *code);

bool read_int_arg(const char *json, const char *key, int *out);
bool read_bool_arg(const char *json, const char *key, bool *out);
bool json_has_key(const char *json, const char *key);
bool json_has_any_key(const char *json, const char *const *keys, size_t count);
bool read_u32_arg(const char *json, const char *key, uint32_t *out);
bool read_pwm_arg(const char *json, const char *key, uint8_t *out);
bool read_optional_pwm_arg(const char *json, const char *key, uint8_t fallback, uint8_t *out);
bool read_string_arg(const char *json, const char *key, char *out, size_t out_size);
bool parse_color_text(const char *text,
                      uint8_t *out_red,
                      uint8_t *out_green,
                      uint8_t *out_blue,
                      uint8_t *out_white);
bool read_color_arg(const char *json,
                    const char *key,
                    uint8_t *out_red,
                    uint8_t *out_green,
                    uint8_t *out_blue,
                    uint8_t *out_white);
bool read_optional_color_arg(const char *json,
                             const char *key,
                             uint8_t fallback_red,
                             uint8_t fallback_green,
                             uint8_t fallback_blue,
                             uint8_t fallback_white,
                             uint8_t *out_red,
                             uint8_t *out_green,
                             uint8_t *out_blue,
                             uint8_t *out_white);

esp_err_t execute_led_off(const node_control_command_t *command, node_control_result_t *result);
esp_err_t execute_led_solid(const node_control_command_t *command, node_control_result_t *result);
esp_err_t execute_led_blink(const node_control_command_t *command,
                            node_control_result_t *result,
                            bool allow_timing_overrides);
esp_err_t execute_led_breathe(const node_control_command_t *command,
                              node_control_result_t *result,
                              bool allow_motion_overrides);
esp_err_t execute_led_effect(const node_control_command_t *command,
                             node_control_result_t *result,
                             bool allow_advanced_overrides);
esp_err_t execute_output_set(node_hw_output_kind_t kind,
                             const char *args_json,
                             node_control_result_t *result);
esp_err_t execute_output_pulse(node_hw_output_kind_t kind,
                               const char *args_json,
                               node_control_result_t *result);
esp_err_t execute_relay_effect(const char *args_json, node_control_result_t *result);
esp_err_t execute_output_all_off(node_hw_output_kind_t kind,
                                 node_control_result_t *result,
                                 const char *fallback);
esp_err_t execute_node_all_off(node_control_result_t *result);
esp_err_t execute_mosfet_set(const char *args_json, node_control_result_t *result);
esp_err_t execute_mosfet_fade(const char *args_json, node_control_result_t *result);
esp_err_t execute_mosfet_pulse(const char *args_json, node_control_result_t *result);
esp_err_t execute_mosfet_blink(const char *args_json, node_control_result_t *result);
esp_err_t execute_mosfet_breathe(const char *args_json, node_control_result_t *result);
esp_err_t execute_mosfet_all_off(node_control_result_t *result);
esp_err_t execute_mosfet_effect_alias(const char *args_json, node_control_result_t *result);
esp_err_t execute_get_status(node_control_result_t *result);
esp_err_t execute_describe_interface(node_control_result_t *result);
