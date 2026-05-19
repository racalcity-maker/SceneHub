#include "node_hardware_io_internal.h"

static esp_err_t configure_mosfet_slot(size_t idx, const node_output_pin_config_t *pin)
{
    if (!pin->enabled || pin->gpio < 0) {
        return ESP_OK;
    }
    esp_err_t err = node_hw_configure_output_gpio(pin->gpio, pin->active_low);
    if (err != ESP_OK) {
        return err;
    }
    node_hw_assign_output_slot(&g_node_hw.mosfets[idx], pin->gpio, pin->active_low);
    g_node_hw.status.configured_mosfets++;
    return ESP_OK;
}

esp_err_t node_hw_mosfet_init(const node_config_t *config)
{
    for (size_t i = 0; i < NODE_MOSFET_MAX; ++i) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(configure_mosfet_slot(i, &config->mosfets[i]));
    }
    return ESP_OK;
}

esp_err_t node_hw_mosfet_set(uint8_t channel, bool on)
{
    if (channel == 0 || channel > NODE_MOSFET_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    return node_hw_output_slot_set(&g_node_hw.mosfets[channel - 1], on);
}
