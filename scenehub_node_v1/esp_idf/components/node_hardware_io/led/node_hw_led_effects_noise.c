#include "node_hw_led_internal.h"

#include "esp_random.h"

static esp_err_t run_twinkle_common(node_hw_led_strip_t *strip,
                                    const node_hw_led_effect_config_t *config,
                                    uint32_t effect_seq,
                                    bool random_colors,
                                    bool hard_flash)
{
    uint32_t step_ms = config->step_ms ? config->step_ms : 50U;
    uint32_t count = config->count;
    uint32_t density = effect_density_or_default(config, hard_flash ? 20U : 40U);
    uint32_t fade = effect_fade_or_default(config, 120U);
    esp_err_t err = ESP_OK;

    if (density > 255U) {
        density = 255U;
    }
    if (fade > 255U) {
        fade = 255U;
    }

    for (uint32_t cycle = 0; count == 0 || cycle < count; ++cycle) {
        for (uint32_t frame = 0; frame < 96U; ++frame) {
            if (effect_cancelled(strip, effect_seq) || !take_strip_lock(strip)) {
                return ESP_OK;
            }
            err = fill_background(strip, config);
            for (uint32_t pixel = 0; err == ESP_OK && pixel < strip->pixel_count; ++pixel) {
                uint32_t seed = hash_u32((cycle * 131U) ^ (frame * 911U) ^ (pixel * 313U));
                uint8_t alpha = 0;
                uint8_t red = config->red;
                uint8_t green = config->green;
                uint8_t blue = config->blue;
                uint8_t white = config->white;
                uint8_t out_red = 0;
                uint8_t out_green = 0;
                uint8_t out_blue = 0;
                uint8_t out_white = 0;

                if (random_colors) {
                    palette_color(config->palette_mode ? config->palette_mode : NODE_LED_PALETTE_RANDOM,
                                  seed,
                                  &red,
                                  &green,
                                  &blue);
                    white = 0;
                }

                if (hard_flash) {
                    alpha = ((seed >> 8) & 0xffU) < density ? 255U : 0U;
                } else {
                    uint8_t wave = triangle_wave_u8((seed + frame * 23U) & 0xffU, 256U);
                    if (wave >= (uint8_t)(255U - density)) {
                        alpha = scale_u8(wave, (uint16_t)fade);
                    }
                }

                if (alpha == 0U) {
                    continue;
                }

                mix_rgbw(config->bg_red,
                         config->bg_green,
                         config->bg_blue,
                         config->bg_white,
                         red,
                         green,
                         blue,
                         white,
                         alpha,
                         &out_red,
                         &out_green,
                         &out_blue,
                         &out_white);
                err = set_pixel_scaled(strip,
                                       pixel,
                                       out_red,
                                       out_green,
                                       out_blue,
                                       out_white,
                                       config->brightness);
            }
            if (err == ESP_OK) {
                err = refresh_strip(strip);
            }
            give_strip_lock(strip);
            if (err != ESP_OK) {
                return err;
            }
            if (!delay_effect_ms(strip, step_ms, effect_seq)) {
                return ESP_OK;
            }
        }
    }

    return ESP_OK;
}

esp_err_t run_twinkle(node_hw_led_strip_t *strip,
                      const node_hw_led_effect_config_t *config,
                      uint32_t effect_seq)
{
    return run_twinkle_common(strip, config, effect_seq, false, false);
}

esp_err_t run_twinkle_random(node_hw_led_strip_t *strip,
                             const node_hw_led_effect_config_t *config,
                             uint32_t effect_seq)
{
    return run_twinkle_common(strip, config, effect_seq, true, false);
}

esp_err_t run_sparkle(node_hw_led_strip_t *strip,
                      const node_hw_led_effect_config_t *config,
                      uint32_t effect_seq)
{
    return run_twinkle_common(strip, config, effect_seq, false, true);
}

esp_err_t run_glitter(node_hw_led_strip_t *strip,
                      const node_hw_led_effect_config_t *config,
                      uint32_t effect_seq)
{
    return run_twinkle_common(strip, config, effect_seq, true, true);
}

esp_err_t run_fire_flicker(node_hw_led_strip_t *strip,
                           const node_hw_led_effect_config_t *config,
                           uint32_t effect_seq)
{
    uint32_t step_ms = config->step_ms ? config->step_ms : 45U;
    uint32_t count = config->count;
    uint32_t intensity = effect_intensity_or_default(config, 180U);
    uint32_t density = effect_density_or_default(config, 70U);
    esp_err_t err = ESP_OK;

    if (intensity > 255U) {
        intensity = 255U;
    }
    if (density > 255U) {
        density = 255U;
    }

    for (uint32_t cycle = 0; count == 0 || cycle < count; ++cycle) {
        for (uint32_t frame = 0; frame < 96U; ++frame) {
            if (effect_cancelled(strip, effect_seq) || !take_strip_lock(strip)) {
                return ESP_OK;
            }
            for (uint32_t pixel = 0; err == ESP_OK && pixel < strip->pixel_count; ++pixel) {
                uint32_t seed = hash_u32(frame * 977U + pixel * 131U + cycle * 17U + esp_random());
                uint8_t red = 0;
                uint8_t green = 0;
                uint8_t blue = 0;
                uint8_t alpha = (uint8_t)(100U + (seed & 0x9fU));
                alpha = scale_u8(alpha, (uint16_t)intensity);
                if ((seed & 0xffU) < density / 4U) {
                    alpha = 255U;
                }
                palette_color(config->palette_mode == NODE_LED_PALETTE_NONE ? NODE_LED_PALETTE_FIRE : config->palette_mode,
                              seed,
                              &red,
                              &green,
                              &blue);
                err = set_pixel_scaled(strip, pixel, red, green, blue, 0, scale_u8(config->brightness, alpha));
            }
            if (err == ESP_OK) {
                err = refresh_strip(strip);
            }
            give_strip_lock(strip);
            if (err != ESP_OK) {
                return err;
            }
            if (!delay_effect_ms(strip, step_ms, effect_seq)) {
                return ESP_OK;
            }
        }
    }

    return ESP_OK;
}
