#include "node_hardware_io_internal.h"

static esp_err_t configure_relay_slot(size_t idx, const node_output_pin_config_t *pin)
{
    if (!pin->enabled || pin->gpio < 0) {
        return ESP_OK;
    }
    esp_err_t err = node_hw_configure_output_gpio(pin->gpio, pin->active_low);
    if (err != ESP_OK) {
        return err;
    }
    node_hw_assign_output_slot(&g_node_hw.relays[idx], pin->gpio, pin->active_low);
    g_node_hw.status.configured_relays++;
    return ESP_OK;
}

esp_err_t node_hw_relay_init(const node_config_t *config)
{
    for (size_t i = 0; i < NODE_RELAY_MAX; ++i) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(configure_relay_slot(i, &config->relays[i]));
    }
    return ESP_OK;
}

esp_err_t node_hw_relay_set(uint8_t channel, bool on)
{
    if (channel == 0 || channel > NODE_RELAY_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    return node_hw_output_slot_set(&g_node_hw.relays[channel - 1], on);
}

esp_err_t node_hw_relay_all_off(void)
{
    esp_err_t first_err = ESP_OK;

    for (size_t i = 0; i < NODE_RELAY_MAX; ++i) {
        if (!g_node_hw.relays[i].configured) {
            continue;
        }
        esp_err_t err = node_hw_output_slot_set(&g_node_hw.relays[i], false);
        if (first_err == ESP_OK && err != ESP_OK) {
            first_err = err;
        }
    }

    return first_err;
}
