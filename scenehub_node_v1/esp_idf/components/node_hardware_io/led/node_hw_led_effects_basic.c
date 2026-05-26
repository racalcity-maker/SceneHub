#include "node_hw_led_internal.h"

#include "esp_check.h"

static const char *TAG = "node_hw_led_basic";

esp_err_t run_blink(node_hw_led_strip_t *strip,
                    const node_hw_led_effect_config_t *config,
                    uint32_t effect_seq)
{
    uint32_t on_ms = config->duration_ms;
    uint32_t off_ms = config->step_ms ? config->step_ms : config->duration_ms;
    uint32_t count = config->count;
    for (uint32_t i = 0; count == 0 || i < count; ++i) {
        if (effect_cancelled(strip, effect_seq)) {
            return ESP_OK;
        }
        ESP_RETURN_ON_ERROR(fill_strip_locked(strip,
                                              config->red,
                                              config->green,
                                              config->blue,
                                              config->white,
                                              config->brightness),
                            TAG,
                            "blink fill failed");
        if (!delay_effect_ms(strip, on_ms, effect_seq)) {
            return ESP_OK;
        }
        ESP_RETURN_ON_ERROR(clear_strip_locked(strip), TAG, "blink clear failed");
        if ((count == 0 || i + 1U < count) && !delay_effect_ms(strip, off_ms, effect_seq)) {
            return ESP_OK;
        }
    }
    return ESP_OK;
}

esp_err_t run_breathe(node_hw_led_strip_t *strip,
                      const node_hw_led_effect_config_t *config,
                      uint32_t effect_seq)
{
    uint32_t cycle_ms = config->duration_ms ? config->duration_ms : 1000U;
    uint32_t step_ms = config->step_ms ? config->step_ms : 40U;
    uint32_t count = config->count;
    uint32_t half_steps = cycle_ms / (step_ms * 2U);

    if (half_steps == 0) {
        half_steps = 1;
    }

    for (uint32_t cycle = 0; count == 0 || cycle < count; ++cycle) {
        for (uint32_t step = 0; step <= half_steps; ++step) {
            uint8_t brightness = (uint8_t)((255U * step) / half_steps);
            if (effect_cancelled(strip, effect_seq)) {
                return ESP_OK;
            }
            ESP_RETURN_ON_ERROR(fill_strip_locked(strip,
                                                  config->red,
                                                  config->green,
                                                  config->blue,
                                                  config->white,
                                                  brightness),
                                TAG,
                                "breathe up failed");
            if (!delay_effect_ms(strip, step_ms, effect_seq)) {
                return ESP_OK;
            }
        }
        for (uint32_t step = half_steps; step > 0; --step) {
            uint8_t brightness = (uint8_t)((255U * (step - 1U)) / half_steps);
            if (effect_cancelled(strip, effect_seq)) {
                return ESP_OK;
            }
            ESP_RETURN_ON_ERROR(fill_strip_locked(strip,
                                                  config->red,
                                                  config->green,
                                                  config->blue,
                                                  config->white,
                                                  brightness),
                                TAG,
                                "breathe down failed");
            if (!delay_effect_ms(strip, step_ms, effect_seq)) {
                return ESP_OK;
            }
        }
    }
    return ESP_OK;
}

esp_err_t run_strobe(node_hw_led_strip_t *strip,
                     const node_hw_led_effect_config_t *config,
                     uint32_t effect_seq)
{
    uint32_t on_ms = config->duration_ms ? config->duration_ms : 60U;
    uint32_t off_ms = config->step_ms ? config->step_ms : 60U;
    uint32_t count = config->count;

    for (uint32_t cycle = 0; count == 0 || cycle < count; ++cycle) {
        if (effect_cancelled(strip, effect_seq)) {
            return ESP_OK;
        }
        ESP_RETURN_ON_ERROR(fill_strip_locked(strip,
                                              config->red,
                                              config->green,
                                              config->blue,
                                              config->white,
                                              config->brightness),
                            TAG,
                            "strobe fill failed");
        if (!delay_effect_ms(strip, on_ms, effect_seq)) {
            return ESP_OK;
        }
        ESP_RETURN_ON_ERROR(fill_strip_locked(strip,
                                              config->bg_red,
                                              config->bg_green,
                                              config->bg_blue,
                                              config->bg_white,
                                              config->brightness),
                            TAG,
                            "strobe fill2 failed");
        if ((count == 0 || cycle + 1U < count) && !delay_effect_ms(strip, off_ms, effect_seq)) {
            return ESP_OK;
        }
    }
    return ESP_OK;
}

esp_err_t run_pulse(node_hw_led_strip_t *strip,
                    const node_hw_led_effect_config_t *config,
                    uint32_t effect_seq)
{
    uint32_t step_ms = config->step_ms ? config->step_ms : 45U;
    uint32_t count = config->count;
    uint32_t frames = 64U;
    uint8_t max_brightness = scale_u8(config->brightness, (uint16_t)effect_intensity_or_default(config, 180U));

    for (uint32_t cycle = 0; count == 0 || cycle < count; ++cycle) {
        for (uint32_t frame = 0; frame < frames; ++frame) {
            uint8_t wave = triangle_wave_u8(frame * 8U, frames * 8U);
            uint8_t brightness = scale_u8(max_brightness, wave);
            if (effect_cancelled(strip, effect_seq)) {
                return ESP_OK;
            }
            ESP_RETURN_ON_ERROR(fill_strip_locked(strip,
                                                  config->red,
                                                  config->green,
                                                  config->blue,
                                                  config->white,
                                                  brightness),
                                TAG,
                                "pulse fill failed");
            if (!delay_effect_ms(strip, step_ms, effect_seq)) {
                return ESP_OK;
            }
        }
    }
    return ESP_OK;
}

esp_err_t run_fade_in_out(node_hw_led_strip_t *strip,
                          const node_hw_led_effect_config_t *config,
                          uint32_t effect_seq)
{
    uint32_t step_ms = config->step_ms ? config->step_ms : 40U;
    uint32_t count = config->count;
    uint32_t frames = 72U;
    uint8_t floor = scale_u8(config->brightness, (uint16_t)effect_fade_or_default(config, 96U));

    for (uint32_t cycle = 0; count == 0 || cycle < count; ++cycle) {
        for (uint32_t frame = 0; frame < frames; ++frame) {
            uint8_t wave = triangle_wave_u8(frame * 8U, frames * 8U);
            uint8_t brightness = blend_u8(floor, config->brightness, wave);
            if (effect_cancelled(strip, effect_seq)) {
                return ESP_OK;
            }
            ESP_RETURN_ON_ERROR(fill_strip_locked(strip,
                                                  config->red,
                                                  config->green,
                                                  config->blue,
                                                  config->white,
                                                  brightness),
                                TAG,
                                "fade fill failed");
            if (!delay_effect_ms(strip, step_ms, effect_seq)) {
                return ESP_OK;
            }
        }
    }
    return ESP_OK;
}
