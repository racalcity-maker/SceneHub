#include "node_hardware_io_internal.h"

static esp_err_t configure_universal_slot(size_t idx, const node_universal_pin_config_t *pin)
{
    if (!pin->enabled || pin->gpio < 0) {
        return ESP_OK;
    }
    if (pin->role == NODE_PIN_UNIVERSAL_OUTPUT) {
        esp_err_t err = node_hw_configure_output_gpio(pin->gpio, pin->active_low);
        if (err != ESP_OK) {
            return err;
        }
        node_hw_assign_output_slot(&g_node_hw.universal_outputs[idx], pin->gpio, pin->active_low);
        g_node_hw.status.configured_universal_outputs++;
    } else if (pin->role == NODE_PIN_UNIVERSAL_INPUT) {
        esp_err_t err = node_hw_configure_input_gpio(pin->gpio, pin->active_low);
        if (err != ESP_OK) {
            return err;
        }
        node_hw_assign_input_slot(&g_node_hw.universal_inputs[idx], pin->gpio, pin->active_low);
        g_node_hw.status.configured_universal_inputs++;
    }
    return ESP_OK;
}

esp_err_t node_hw_universal_io_init(const node_config_t *config)
{
    for (size_t i = 0; i < NODE_UNIVERSAL_IO_MAX; ++i) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(configure_universal_slot(i, &config->universal_io[i]));
    }
    return ESP_OK;
}

esp_err_t node_hw_universal_output_set(uint8_t channel, bool on)
{
    if (channel == 0 || channel > NODE_UNIVERSAL_IO_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    return node_hw_output_slot_set(&g_node_hw.universal_outputs[channel - 1], on);
}

esp_err_t node_hw_universal_output_all_off(void)
{
    esp_err_t first_err = ESP_OK;

    for (size_t i = 0; i < NODE_UNIVERSAL_IO_MAX; ++i) {
        if (!g_node_hw.universal_outputs[i].configured) {
            continue;
        }
        esp_err_t err = node_hw_output_slot_set(&g_node_hw.universal_outputs[i], false);
        if (first_err == ESP_OK && err != ESP_OK) {
            first_err = err;
        }
    }

    return first_err;
}
