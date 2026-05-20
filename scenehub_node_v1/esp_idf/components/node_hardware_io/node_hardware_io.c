#include "node_hardware_io_internal.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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
    slot->last_active = false;
    slot->last_active_valid = false;
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

esp_err_t node_hardware_io_all_off(node_hw_output_kind_t kind)
{
    switch (kind) {
    case NODE_HW_OUTPUT_RELAY:
        return node_hw_relay_all_off();
    case NODE_HW_OUTPUT_MOSFET:
        return node_hw_mosfet_all_off();
    case NODE_HW_OUTPUT_UNIVERSAL_IO:
        return node_hw_universal_output_all_off();
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t node_hardware_io_mosfet_set(uint8_t channel, uint8_t value)
{
    return node_hw_mosfet_set_value(channel, value);
}

esp_err_t node_hardware_io_mosfet_fade(uint8_t channel, uint8_t target, uint32_t duration_ms)
{
    return node_hw_mosfet_fade(channel, target, duration_ms);
}

esp_err_t node_hardware_io_mosfet_pulse(uint8_t channel, uint8_t value, uint32_t duration_ms)
{
    return node_hw_mosfet_pulse(channel, value, duration_ms);
}

esp_err_t node_hardware_io_mosfet_blink(uint8_t channel,
                                        uint8_t value,
                                        uint32_t on_ms,
                                        uint32_t off_ms,
                                        uint32_t count,
                                        uint8_t final_value)
{
    return node_hw_mosfet_blink(channel, value, on_ms, off_ms, count, final_value);
}

esp_err_t node_hardware_io_mosfet_breathe(uint8_t channel,
                                          uint8_t min_value,
                                          uint8_t max_value,
                                          uint32_t fade_ms,
                                          uint32_t hold_ms,
                                          uint32_t count,
                                          uint8_t final_value)
{
    return node_hw_mosfet_breathe(channel,
                                  min_value,
                                  max_value,
                                  fade_ms,
                                  hold_ms,
                                  count,
                                  final_value);
}

esp_err_t node_hardware_io_mosfet_all_off(void)
{
    return node_hw_mosfet_all_off();
}

esp_err_t node_hardware_io_led_all_off(void)
{
    return node_hw_led_all_off();
}

esp_err_t node_hardware_io_led_off(uint8_t strip)
{
    return node_hw_led_off(strip);
}

esp_err_t node_hardware_io_led_solid(uint8_t strip,
                                     uint8_t red,
                                     uint8_t green,
                                     uint8_t blue,
                                     uint8_t white,
                                     uint8_t brightness)
{
    return node_hw_led_solid(strip, red, green, blue, white, brightness);
}

esp_err_t node_hardware_io_led_run_effect(uint8_t strip,
                                          node_hw_led_effect_t effect,
                                          const node_hw_led_effect_config_t *config)
{
    return node_hw_led_run_effect(strip, effect, config);
}

esp_err_t node_hardware_io_node_all_off(void)
{
    esp_err_t first_err = ESP_OK;
    esp_err_t err = node_hw_relay_all_off();
    if (first_err == ESP_OK && err != ESP_OK) {
        first_err = err;
    }
    err = node_hw_mosfet_all_off();
    if (first_err == ESP_OK && err != ESP_OK) {
        first_err = err;
    }
    err = node_hw_universal_output_all_off();
    if (first_err == ESP_OK && err != ESP_OK) {
        first_err = err;
    }
    err = node_hw_led_all_off();
    if (first_err == ESP_OK && err != ESP_OK) {
        first_err = err;
    }
    return first_err;
}

static node_hw_output_slot_t *find_output_slot(node_hw_output_kind_t kind, uint8_t channel)
{
    if (channel == 0) {
        return NULL;
    }
    switch (kind) {
    case NODE_HW_OUTPUT_RELAY:
        return channel <= NODE_RELAY_MAX ? &g_node_hw.relays[channel - 1] : NULL;
    case NODE_HW_OUTPUT_MOSFET:
        return channel <= NODE_MOSFET_MAX ? &g_node_hw.mosfets[channel - 1] : NULL;
    case NODE_HW_OUTPUT_UNIVERSAL_IO:
        return channel <= NODE_UNIVERSAL_IO_MAX ? &g_node_hw.universal_outputs[channel - 1] : NULL;
    default:
        return NULL;
    }
}

esp_err_t node_hardware_io_pulse_output(node_hw_output_kind_t kind, uint8_t channel, uint32_t duration_ms)
{
    node_hw_output_slot_t *slot = find_output_slot(kind, channel);
    if (!slot) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!slot->configured) {
        return ESP_ERR_NOT_FOUND;
    }
    if (kind == NODE_HW_OUTPUT_MOSFET) {
        esp_err_t err = node_hw_mosfet_pulse(channel, 255, duration_ms);
        if (err == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(duration_ms));
        }
        return err;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(node_hw_output_slot_set(slot, true));
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    return node_hw_output_slot_set(slot, false);
}

esp_err_t node_hardware_io_identify(uint32_t duration_ms, uint8_t repeat_count)
{
    node_hw_output_kind_t kind = NODE_HW_OUTPUT_RELAY;
    uint8_t channel = 0;
    for (size_t i = 0; i < NODE_RELAY_MAX && channel == 0; ++i) {
        if (g_node_hw.relays[i].configured) {
            kind = NODE_HW_OUTPUT_RELAY;
            channel = (uint8_t)(i + 1);
        }
    }
    for (size_t i = 0; i < NODE_MOSFET_MAX && channel == 0; ++i) {
        if (g_node_hw.mosfets[i].configured) {
            kind = NODE_HW_OUTPUT_MOSFET;
            channel = (uint8_t)(i + 1);
        }
    }
    for (size_t i = 0; i < NODE_UNIVERSAL_IO_MAX && channel == 0; ++i) {
        if (g_node_hw.universal_outputs[i].configured) {
            kind = NODE_HW_OUTPUT_UNIVERSAL_IO;
            channel = (uint8_t)(i + 1);
        }
    }
    if (channel == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t loops = repeat_count == 0 ? 1 : repeat_count;
    uint32_t on_ms = duration_ms == 0 ? 150 : duration_ms;
    uint32_t off_ms = on_ms / 2;
    for (uint8_t i = 0; i < loops; ++i) {
        if (kind == NODE_HW_OUTPUT_MOSFET) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(node_hw_mosfet_pulse(channel, 255, on_ms));
            vTaskDelay(pdMS_TO_TICKS(on_ms));
        } else {
            ESP_ERROR_CHECK_WITHOUT_ABORT(node_hardware_io_set_output(kind, channel, true));
            vTaskDelay(pdMS_TO_TICKS(on_ms));
            ESP_ERROR_CHECK_WITHOUT_ABORT(node_hardware_io_set_output(kind, channel, false));
        }
        if (i + 1U < loops && off_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(off_ms));
        }
    }
    return ESP_OK;
}

esp_err_t node_hardware_io_read_input(uint8_t channel, bool *out_active)
{
    if (!out_active) {
        return ESP_ERR_INVALID_ARG;
    }
    if (channel == 0 || channel > NODE_UNIVERSAL_IO_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    node_hw_input_slot_t *slot = &g_node_hw.universal_inputs[channel - 1];
    if (!slot->configured) {
        return ESP_ERR_NOT_FOUND;
    }

    int raw_level = gpio_get_level(slot->gpio);
    *out_active = slot->active_low ? (raw_level == 0) : (raw_level != 0);
    return ESP_OK;
}

esp_err_t node_hardware_io_observe_input_change(uint8_t channel,
                                                bool current_active,
                                                bool *out_changed)
{
    if (!out_changed) {
        return ESP_ERR_INVALID_ARG;
    }
    if (channel == 0 || channel > NODE_UNIVERSAL_IO_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    node_hw_input_slot_t *slot = &g_node_hw.universal_inputs[channel - 1];
    if (!slot->configured) {
        return ESP_ERR_NOT_FOUND;
    }

    if (!slot->last_active_valid) {
        slot->last_active = current_active;
        slot->last_active_valid = true;
        *out_changed = false;
        return ESP_OK;
    }

    *out_changed = slot->last_active != current_active;
    slot->last_active = current_active;
    return ESP_OK;
}

node_hardware_io_status_t node_hardware_io_get_status(void)
{
    return g_node_hw.status;
}
