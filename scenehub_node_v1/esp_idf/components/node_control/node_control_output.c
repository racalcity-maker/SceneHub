#include "node_control_internal.h"

#include "node_hardware_io.h"

esp_err_t execute_output_set(node_hw_output_kind_t kind,
                             const char *args_json,
                             node_control_result_t *result)
{
    int channel = 0;
    bool on = false;
    if (!read_int_arg(args_json, "channel", &channel)) {
        return result_rejected(result, "missing_channel");
    }
    if (!read_bool_arg(args_json, "on", &on)) {
        return result_rejected(result, "missing_on");
    }
    esp_err_t err = node_hardware_io_set_output(kind, (uint8_t)channel, on);
    if (err != ESP_OK) {
        return result_rejected(result, err == ESP_ERR_NOT_FOUND ? "not_configured" : "invalid_channel");
    }
    result_started(result);
    return ESP_OK;
}

esp_err_t execute_output_pulse(node_hw_output_kind_t kind,
                               const char *args_json,
                               node_control_result_t *result)
{
    int channel = 0;
    uint32_t duration_ms = 0;
    if (!read_int_arg(args_json, "channel", &channel)) {
        return result_rejected(result, "missing_channel");
    }
    if (!read_u32_arg(args_json, "duration_ms", &duration_ms) || duration_ms == 0) {
        return result_rejected(result, "missing_duration_ms");
    }
    esp_err_t err = node_hardware_io_pulse_output(kind, (uint8_t)channel, duration_ms);
    if (err != ESP_OK) {
        return result_rejected(result, err == ESP_ERR_NOT_FOUND ? "not_configured" : "invalid_channel");
    }
    result_started(result);
    return ESP_OK;
}

esp_err_t execute_output_all_off(node_hw_output_kind_t kind,
                                 node_control_result_t *result,
                                 const char *fallback)
{
    esp_err_t err = node_hardware_io_all_off(kind);
    if (err != ESP_OK) {
        if (err == ESP_ERR_NOT_FOUND) {
            return result_rejected(result, "not_configured");
        }
        if (err == ESP_ERR_INVALID_ARG) {
            return result_rejected(result, "invalid_args");
        }
        return result_rejected(result, fallback ? fallback : "internal_error");
    }
    result_started(result);
    return ESP_OK;
}

esp_err_t execute_node_all_off(node_control_result_t *result)
{
    esp_err_t err = node_hardware_io_node_all_off();
    if (err != ESP_OK) {
        if (err == ESP_ERR_NOT_FOUND) {
            return result_rejected(result, "not_configured");
        }
        if (err == ESP_ERR_INVALID_ARG) {
            return result_rejected(result, "invalid_args");
        }
        return result_rejected(result, "node_all_off_failed");
    }
    result_started(result);
    return ESP_OK;
}
