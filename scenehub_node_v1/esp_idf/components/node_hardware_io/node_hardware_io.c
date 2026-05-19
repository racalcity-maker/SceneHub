#include "node_hardware_io_internal.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "node_board.h"

static const char *TAG = "node_hardware_io";

node_hw_state_t g_node_hw;

esp_err_t node_hw_configure_output_gpio(int gpio, bool active_low)
{
    if (!node_board_gpio_is_allowed(gpio)) {
        ESP_LOGW(TAG, "GPIO %d is not allowed for output", gpio);
        return ESP_ERR_INVALID_ARG;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return err;
    }

    return gpio_set_level(gpio, active_low ? 1 : 0);
}

esp_err_t node_hw_configure_input_gpio(int gpio, bool active_low)
{
    if (!node_board_gpio_is_allowed(gpio)) {
        ESP_LOGW(TAG, "GPIO %d is not allowed for input", gpio);
        return ESP_ERR_INVALID_ARG;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&cfg);
}

void node_hw_assign_output_slot(node_hw_output_slot_t *slot, int gpio, bool active_low)
{
    slot->configured = true;
    slot->gpio = gpio;
    slot->active_low = active_low;
    slot->state_on = false;
}

void node_hw_assign_input_slot(node_hw_input_slot_t *slot, int gpio, bool active_low)
{
    slot->configured = true;
    slot->gpio = gpio;
    slot->active_low = active_low;
}

esp_err_t node_hw_output_slot_set(node_hw_output_slot_t *slot, bool on)
{
    if (!slot || !slot->configured) {
        return ESP_ERR_NOT_FOUND;
    }

    int level = slot->active_low ? !on : on;
    esp_err_t err = gpio_set_level(slot->gpio, level);
    if (err == ESP_OK) {
        slot->state_on = on;
    }
    return err;
}

esp_err_t node_hardware_io_init(const node_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(&g_node_hw, 0, sizeof(g_node_hw));
    ESP_ERROR_CHECK_WITHOUT_ABORT(node_hw_relay_init(config));
    ESP_ERROR_CHECK_WITHOUT_ABORT(node_hw_mosfet_init(config));
    ESP_ERROR_CHECK_WITHOUT_ABORT(node_hw_universal_io_init(config));
    ESP_ERROR_CHECK_WITHOUT_ABORT(node_hw_led_init(config));

    ESP_LOGI(TAG,
             "hardware configured relays=%u mosfets=%u io_in=%u io_out=%u led=%u",
             (unsigned)g_node_hw.status.configured_relays,
             (unsigned)g_node_hw.status.configured_mosfets,
             (unsigned)g_node_hw.status.configured_universal_inputs,
             (unsigned)g_node_hw.status.configured_universal_outputs,
             (unsigned)g_node_hw.status.configured_led_strips);
    return ESP_OK;
}

esp_err_t node_hardware_io_set_output(node_hw_output_kind_t kind, uint8_t channel, bool on)
{
    switch (kind) {
    case NODE_HW_OUTPUT_RELAY:
        return node_hw_relay_set(channel, on);
    case NODE_HW_OUTPUT_MOSFET:
        return node_hw_mosfet_set(channel, on);
    case NODE_HW_OUTPUT_UNIVERSAL_IO:
        return node_hw_universal_output_set(channel, on);
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

node_hardware_io_status_t node_hardware_io_get_status(void)
{
    return g_node_hw.status;
}
