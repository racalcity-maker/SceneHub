#include "node_control.h"
#include "node_control_internal.h"

#include <stdio.h>
#include <string.h>

#include "node_hardware_io.h"

node_config_t g_node_control_config;

void copy_text(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) {
        return;
    }
    snprintf(dst, dst_size, "%s", src ? src : "");
}

const char *command_source_text(node_control_command_source_t source)
{
    switch (source) {
    case NODE_CONTROL_SOURCE_HUB:
        return "hub";
    case NODE_CONTROL_SOURCE_LOCAL_PREVIEW:
        return "preview";
    case NODE_CONTROL_SOURCE_LOCAL_UI:
        return "local_ui";
    case NODE_CONTROL_SOURCE_UNKNOWN:
    default:
        return "unknown";
    }
}

const char *command_request_id_text(const node_control_command_t *command)
{
    return (command && command->request_id && command->request_id[0]) ? command->request_id : "-";
}

const char *command_source_safe(const node_control_command_t *command)
{
    return command_source_text(command ? command->source : NODE_CONTROL_SOURCE_UNKNOWN);
}

void result_done(node_control_result_t *result)
{
    copy_text(result->status, sizeof(result->status), "done");
    result->error_code[0] = '\0';
}

void result_started(node_control_result_t *result)
{
    copy_text(result->status, sizeof(result->status), "started");
    result->error_code[0] = '\0';
}

esp_err_t result_rejected(node_control_result_t *result, const char *code)
{
    copy_text(result->status, sizeof(result->status), "rejected");
    copy_text(result->error_code, sizeof(result->error_code), code);
    return ESP_ERR_INVALID_ARG;
}

esp_err_t node_control_init(const node_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    g_node_control_config = *config;
    return ESP_OK;
}

esp_err_t node_control_update_led_config(const node_led_strip_config_t *led_strips, size_t count)
{
    if (!led_strips || count > NODE_LED_STRIP_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < count; ++i) {
        g_node_control_config.led_strips[i].blink = led_strips[i].blink;
        g_node_control_config.led_strips[i].breathe = led_strips[i].breathe;
        g_node_control_config.led_strips[i].effects = led_strips[i].effects;
    }
    return ESP_OK;
}

esp_err_t node_control_execute(const node_control_command_t *command, node_control_result_t *out_result)
{
    if (!command || !command->command || !out_result) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_result, 0, sizeof(*out_result));

    if (strcmp(command->command, "node.get_status") == 0) {
        return execute_get_status(out_result);
    }
    if (strcmp(command->command, "node.identify") == 0) {
        esp_err_t err = node_hardware_io_identify(150, 2);
        if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
            return result_rejected(out_result, "internal_error");
        }
        result_done(out_result);
        return ESP_OK;
    }
    if (strcmp(command->command, "describe_interface") == 0) {
        return execute_describe_interface(out_result);
    }
    if (strcmp(command->command, "relay.set") == 0) {
        return execute_output_set(NODE_HW_OUTPUT_RELAY, command->args_json, out_result);
    }
    if (strcmp(command->command, "relay.pulse") == 0) {
        return execute_output_pulse(NODE_HW_OUTPUT_RELAY, command->args_json, out_result);
    }
    if (strcmp(command->command, "relay.all_off") == 0) {
        return execute_output_all_off(NODE_HW_OUTPUT_RELAY, out_result, "relay_all_off_failed");
    }
    if (strcmp(command->command, "mosfet.set") == 0) {
        return execute_mosfet_set(command->args_json, out_result);
    }
    if (strcmp(command->command, "mosfet.fade") == 0) {
        return execute_mosfet_fade(command->args_json, out_result);
    }
    if (strcmp(command->command, "mosfet.pulse") == 0) {
        return execute_mosfet_pulse(command->args_json, out_result);
    }
    if (strcmp(command->command, "mosfet.blink") == 0) {
        return execute_mosfet_blink(command->args_json, out_result);
    }
    if (strcmp(command->command, "mosfet.breathe") == 0) {
        return execute_mosfet_breathe(command->args_json, out_result);
    }
    if (strcmp(command->command, "mosfet.all_off") == 0) {
        return execute_mosfet_all_off(out_result);
    }
    if (strcmp(command->command, "mosfet.effect") == 0) {
        return execute_mosfet_effect_alias(command->args_json, out_result);
    }
    if (strcmp(command->command, "io.set") == 0) {
        return execute_output_set(NODE_HW_OUTPUT_UNIVERSAL_IO, command->args_json, out_result);
    }
    if (strcmp(command->command, "io.all_off") == 0) {
        return execute_output_all_off(NODE_HW_OUTPUT_UNIVERSAL_IO, out_result, "io_all_off_failed");
    }
    if (strcmp(command->command, "node.all_off") == 0) {
        return execute_node_all_off(out_result);
    }
    if (strcmp(command->command, "led.off") == 0) {
        return execute_led_off(command, out_result);
    }
    if (strcmp(command->command, "led.solid") == 0) {
        return execute_led_solid(command, out_result);
    }
    if (strcmp(command->command, "led.blink") == 0) {
        return execute_led_blink(command, out_result, false);
    }
    if (strcmp(command->command, "led.breathe") == 0) {
        return execute_led_breathe(command, out_result, false);
    }
    if (strcmp(command->command, "led.effect") == 0) {
        return execute_led_effect(command, out_result, false);
    }
    if (strcmp(command->command, "led.preview.blink") == 0) {
        return execute_led_blink(command, out_result, true);
    }
    if (strcmp(command->command, "led.preview.breathe") == 0) {
        return execute_led_breathe(command, out_result, true);
    }
    if (strcmp(command->command, "led.preview.effect") == 0) {
        return execute_led_effect(command, out_result, true);
    }
    return result_rejected(out_result, "not_supported");
}
