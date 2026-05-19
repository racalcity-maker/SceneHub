#include "node_hardware_io_internal.h"

#include "node_board.h"

esp_err_t node_hw_led_init(const node_config_t *config)
{
    for (size_t i = 0; i < NODE_LED_STRIP_MAX; ++i) {
        const node_led_strip_config_t *pin = &config->led_strips[i];
        if (pin->enabled && pin->gpio >= 0 && node_board_gpio_is_allowed(pin->gpio)) {
            g_node_hw.status.configured_led_strips++;
        }
    }
    return ESP_OK;
}
