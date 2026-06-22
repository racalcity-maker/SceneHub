#include "node_control_internal.h"

#include <string.h>

#include "node_hardware_io.h"

static const node_output_pin_config_t *find_mosfet_config(uint8_t channel)
{
    if (channel == 0 || channel > NODE_MOSFET_MAX) {
        return NULL;
    }
    const node_output_pin_config_t *cfg = &g_node_control_config.mosfets[channel - 1];
    return cfg->enabled ? cfg : NULL;
}

static esp_err_t reject_mosfet_error(node_control_result_t *result, esp_err_t err, const char *fallback)
{
    if (err == ESP_ERR_NOT_FOUND) {
        return result_rejected(result, "not_configured");
    }
    if (err == ESP_ERR_INVALID_ARG) {
        return result_rejected(result, "invalid_args");
    }
    return result_rejected(result, fallback ? fallback : "internal_error");
}

esp_err_t execute_mosfet_set(const char *args_json, node_control_result_t *result)
{
    int channel = 0;
    uint8_t value = 0;
    if (!read_int_arg(args_json, "channel", &channel)) {
        return result_rejected(result, "missing_channel");
    }
    if (!read_pwm_arg(args_json, "value", &value)) {
        return result_rejected(result, "missing_value");
    }
    esp_err_t err = node_hardware_io_mosfet_set((uint8_t)channel, value);
    if (err != ESP_OK) {
        return reject_mosfet_error(result, err, "mosfet_set_failed");
    }
    result_started(result);
    return ESP_OK;
}

esp_err_t execute_mosfet_fade(const char *args_json, node_control_result_t *result)
{
    int channel = 0;
    uint8_t target = 255;
    uint32_t duration_ms = 500;
    const node_output_pin_config_t *cfg = NULL;
    if (!read_int_arg(args_json, "channel", &channel)) {
        return result_rejected(result, "missing_channel");
    }
    cfg = find_mosfet_config((uint8_t)channel);
    if (cfg) {
        target = cfg->default_target;
        duration_ms = cfg->fade_duration_ms;
    }
    if (!read_optional_pwm_arg(args_json, "target", target, &target)) {
        return result_rejected(result, "invalid_args");
    }
    (void)read_u32_arg(args_json, "duration_ms", &duration_ms);
    if (duration_ms > NODE_CONTROL_MAX_MOSFET_FADE_MS) {
        return result_rejected(result, "invalid_args");
    }
    esp_err_t err = node_hardware_io_mosfet_fade((uint8_t)channel, target, duration_ms);
    if (err != ESP_OK) {
        return reject_mosfet_error(result, err, "mosfet_fade_failed");
    }
    result_started(result);
    return ESP_OK;
}

esp_err_t execute_mosfet_pulse(const char *args_json, node_control_result_t *result)
{
    int channel = 0;
    uint8_t value = 255;
    uint32_t duration_ms = 300;
    const node_output_pin_config_t *cfg = NULL;
    if (!read_int_arg(args_json, "channel", &channel)) {
        return result_rejected(result, "missing_channel");
    }
    cfg = find_mosfet_config((uint8_t)channel);
    if (cfg) {
        value = cfg->default_value;
        duration_ms = cfg->pulse_duration_ms;
    }
    if (!read_optional_pwm_arg(args_json, "value", value, &value)) {
        return result_rejected(result, "invalid_args");
    }
    (void)read_u32_arg(args_json, "duration_ms", &duration_ms);
    if (duration_ms == 0) {
        return result_rejected(result, "invalid_args");
    }
    if (duration_ms > NODE_CONTROL_MAX_MOSFET_PULSE_MS) {
        return result_rejected(result, "invalid_args");
    }
    esp_err_t err = node_hardware_io_mosfet_pulse((uint8_t)channel, value, duration_ms);
    if (err != ESP_OK) {
        return reject_mosfet_error(result, err, "mosfet_pulse_failed");
    }
    result_started(result);
    return ESP_OK;
}

esp_err_t execute_mosfet_blink(const char *args_json, node_control_result_t *result)
{
    int channel = 0;
    int count = 3;
    uint8_t value = 255;
    uint8_t final_value = 0;
    uint32_t on_ms = 250;
    uint32_t off_ms = 250;
    const node_output_pin_config_t *cfg = NULL;
    if (!read_int_arg(args_json, "channel", &channel)) {
        return result_rejected(result, "missing_channel");
    }
    cfg = find_mosfet_config((uint8_t)channel);
    if (cfg) {
        value = cfg->default_value;
        final_value = cfg->default_final_value;
        on_ms = cfg->blink_on_ms;
        off_ms = cfg->blink_off_ms;
        count = cfg->blink_repeat_count;
    }
    if (!read_optional_pwm_arg(args_json, "value", value, &value)) {
        return result_rejected(result, "invalid_args");
    }
    if (!read_optional_pwm_arg(args_json, "final_value", final_value, &final_value)) {
        return result_rejected(result, "invalid_args");
    }
    (void)read_u32_arg(args_json, "on_ms", &on_ms);
    (void)read_u32_arg(args_json, "off_ms", &off_ms);
    (void)read_int_arg(args_json, "count", &count);
    if (on_ms == 0 || off_ms == 0 ||
        (count > 0 && ((uint64_t)(on_ms + off_ms) * (uint64_t)count) > NODE_CONTROL_MAX_MOSFET_EFFECT_MS)) {
        return result_rejected(result, "invalid_args");
    }
    esp_err_t err = node_hardware_io_mosfet_blink((uint8_t)channel,
                                                  value,
                                                  on_ms,
                                                  off_ms,
                                                  (uint32_t)count,
                                                  final_value);
    if (err != ESP_OK) {
        return reject_mosfet_error(result, err, "mosfet_blink_failed");
    }
    result_started(result);
    return ESP_OK;
}

esp_err_t execute_mosfet_breathe(const char *args_json, node_control_result_t *result)
{
    int channel = 0;
    int count = 1;
    uint8_t min_value = 0;
    uint8_t max_value = 255;
    uint8_t final_value = 0;
    uint32_t fade_ms = 1000;
    uint32_t hold_ms = 0;
    const node_output_pin_config_t *cfg = NULL;
    if (!read_int_arg(args_json, "channel", &channel)) {
        return result_rejected(result, "missing_channel");
    }
    cfg = find_mosfet_config((uint8_t)channel);
    if (cfg) {
        min_value = cfg->default_min;
        max_value = cfg->default_max;
        final_value = cfg->default_final_value;
        fade_ms = cfg->breathe_fade_ms;
        hold_ms = cfg->breathe_hold_ms;
        count = cfg->breathe_repeat_count;
    }
    if (!read_optional_pwm_arg(args_json, "min", min_value, &min_value) ||
        !read_optional_pwm_arg(args_json, "max", max_value, &max_value)) {
        return result_rejected(result, "invalid_args");
    }
    if (!read_optional_pwm_arg(args_json, "final_value", final_value, &final_value)) {
        return result_rejected(result, "invalid_args");
    }
    (void)read_u32_arg(args_json, "fade_ms", &fade_ms);
    (void)read_u32_arg(args_json, "hold_ms", &hold_ms);
    (void)read_int_arg(args_json, "count", &count);
    if (fade_ms == 0 || min_value > max_value ||
        (count > 0 && ((uint64_t)(fade_ms * 2U + hold_ms * 2U) * (uint64_t)count) > NODE_CONTROL_MAX_MOSFET_EFFECT_MS)) {
        return result_rejected(result, "invalid_args");
    }
    esp_err_t err = node_hardware_io_mosfet_breathe((uint8_t)channel,
                                                    min_value,
                                                    max_value,
                                                    fade_ms,
                                                    hold_ms,
                                                    (uint32_t)count,
                                                    final_value);
    if (err != ESP_OK) {
        return reject_mosfet_error(result, err, "mosfet_breathe_failed");
    }
    result_started(result);
    return ESP_OK;
}

esp_err_t execute_mosfet_all_off(node_control_result_t *result)
{
    esp_err_t err = node_hardware_io_mosfet_all_off();
    if (err != ESP_OK) {
        return reject_mosfet_error(result, err, "mosfet_all_off_failed");
    }
    result_started(result);
    return ESP_OK;
}

esp_err_t execute_mosfet_effect_alias(const char *args_json, node_control_result_t *result)
{
    int channel = 0;
    uint32_t duration_ms = 250;
    uint32_t repeat = 1;
    uint8_t value = 255;
    uint8_t target = 255;
    uint8_t min_value = 0;
    uint8_t max_value = 255;
    uint8_t final_value = 0;
    bool on = true;
    char effect[24] = {0};
    const node_output_pin_config_t *cfg = NULL;

    if (!read_int_arg(args_json, "channel", &channel)) {
        return result_rejected(result, "missing_channel");
    }
    cfg = find_mosfet_config((uint8_t)channel);
    if (cfg) {
        duration_ms = cfg->pulse_duration_ms;
        repeat = cfg->breathe_repeat_count;
        value = cfg->default_value;
        target = cfg->default_target;
        min_value = cfg->default_min;
        max_value = cfg->default_max;
        final_value = cfg->default_final_value;
    }
    if (!read_string_arg(args_json, "effect", effect, sizeof(effect))) {
        return result_rejected(result, "missing_effect");
    }
    (void)read_u32_arg(args_json, "duration_ms", &duration_ms);
    (void)read_u32_arg(args_json, "repeat", &repeat);
    (void)read_optional_pwm_arg(args_json, "value", value, &value);
    (void)read_optional_pwm_arg(args_json, "target", target, &target);
    (void)read_optional_pwm_arg(args_json, "min", min_value, &min_value);
    (void)read_optional_pwm_arg(args_json, "max", max_value, &max_value);
    (void)read_optional_pwm_arg(args_json, "final_value", final_value, &final_value);

    if (strcmp(effect, "set") == 0) {
        if (!read_pwm_arg(args_json, "value", &value)) {
            if (!read_bool_arg(args_json, "on", &on)) {
                return result_rejected(result, "missing_value");
            }
            value = on ? 255 : 0;
        }
        esp_err_t err = node_hardware_io_mosfet_set((uint8_t)channel, value);
        if (err != ESP_OK) {
            return reject_mosfet_error(result, err, "mosfet_set_failed");
        }
        result_done(result);
        return ESP_OK;
    }
    if (strcmp(effect, "pulse") == 0) {
        if (duration_ms == 0 || duration_ms > NODE_CONTROL_MAX_MOSFET_PULSE_MS) {
            return result_rejected(result, "invalid_args");
        }
        esp_err_t err = node_hardware_io_mosfet_pulse((uint8_t)channel, value, duration_ms);
        if (err != ESP_OK) {
            return reject_mosfet_error(result, err, "mosfet_pulse_failed");
        }
        result_done(result);
        return ESP_OK;
    }
    if (strcmp(effect, "fade_in") == 0 || strcmp(effect, "fade_out") == 0 || strcmp(effect, "fade") == 0) {
        uint8_t fade_target = target;
        if (strcmp(effect, "fade_in") == 0) {
            fade_target = 255;
        } else if (strcmp(effect, "fade_out") == 0) {
            fade_target = 0;
        }
        if (duration_ms > NODE_CONTROL_MAX_MOSFET_FADE_MS) {
            return result_rejected(result, "invalid_args");
        }
        esp_err_t err = node_hardware_io_mosfet_fade((uint8_t)channel, fade_target, duration_ms);
        if (err != ESP_OK) {
            return reject_mosfet_error(result, err, "mosfet_fade_failed");
        }
        result_done(result);
        return ESP_OK;
    }
    if (strcmp(effect, "blink") == 0) {
        uint32_t on_ms = duration_ms;
        uint32_t off_ms = duration_ms;
        int count = (int)repeat;
        (void)read_u32_arg(args_json, "on_ms", &on_ms);
        (void)read_u32_arg(args_json, "off_ms", &off_ms);
        (void)read_int_arg(args_json, "count", &count);
        if (on_ms == 0 || off_ms == 0 || count < 0 ||
            (count > 0 && ((uint64_t)(on_ms + off_ms) * (uint64_t)count) > NODE_CONTROL_MAX_MOSFET_EFFECT_MS)) {
            return result_rejected(result, "invalid_args");
        }
        esp_err_t err = node_hardware_io_mosfet_blink((uint8_t)channel,
                                                      value,
                                                      on_ms,
                                                      off_ms,
                                                      (uint32_t)count,
                                                      final_value);
        if (err != ESP_OK) {
            return reject_mosfet_error(result, err, "mosfet_blink_failed");
        }
        result_done(result);
        return ESP_OK;
    }
    if (strcmp(effect, "breathe") == 0) {
        uint32_t fade_ms = duration_ms;
        uint32_t hold_ms = duration_ms / 2U;
        int count = (int)repeat;
        (void)read_u32_arg(args_json, "fade_ms", &fade_ms);
        (void)read_u32_arg(args_json, "hold_ms", &hold_ms);
        (void)read_int_arg(args_json, "count", &count);
        if (fade_ms == 0 || min_value > max_value || count < 0 ||
            (count > 0 && ((uint64_t)(fade_ms * 2U + hold_ms * 2U) * (uint64_t)count) > NODE_CONTROL_MAX_MOSFET_EFFECT_MS)) {
            return result_rejected(result, "invalid_args");
        }
        esp_err_t err = node_hardware_io_mosfet_breathe((uint8_t)channel,
                                                        min_value,
                                                        max_value,
                                                        fade_ms,
                                                        hold_ms,
                                                        (uint32_t)count,
                                                        final_value);
        if (err != ESP_OK) {
            return reject_mosfet_error(result, err, "mosfet_breathe_failed");
        }
        result_done(result);
        return ESP_OK;
    }
    if (strcmp(effect, "broken_fluorescent") == 0) {
        esp_err_t err = node_hardware_io_mosfet_broken_fluorescent((uint8_t)channel, value);
        if (err != ESP_OK) {
            return reject_mosfet_error(result, err, "mosfet_effect_failed");
        }
        result_done(result);
        return ESP_OK;
    }

    return result_rejected(result, "invalid_args");
}
